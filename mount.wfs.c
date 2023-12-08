#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

#define MAX_PATH_LENGTH 1024

extern FILE *disk;  // File pointer to disk image
extern struct wfs_sb superblock;  // Superblock of filesystem
// Global variable to keep track of the next inode number
static uint32_t next_inode_number = 1;  // Starting from 1 since 0 is reserved for root inode

// Function to get the next unique inode number
static uint32_t get_next_inode_number() {
    return next_inode_number++;  // Return the current number and increment it for next use
}

// Function to find directory entry by name within directory's log entry
static struct wfs_dentry *get_dentry_by_name(struct wfs_log_entry *dir_entry, const char *name) {
    if (dir_entry == NULL || dir_entry->inode.mode != S_IFDIR) {
        return NULL;  // Not a directory or invalid entry
    }

    // Assuming directory entries are stored immediately after inode in log entry
    struct wfs_dentry *dentry = (struct wfs_dentry *) dir_entry->data;

    // Iterate over directory entries
    for (unsigned int i = 0; i < dir_entry->inode.size / sizeof(struct wfs_dentry); ++i) {
        if (strcmp(dentry[i].name, name) == 0) {
            return &dentry[i];  // Found matching entry
        }
    }

    return NULL;  // No matching entry found
}


// Function to read a log entry from disk at a given offset
static int read_log_entry(struct wfs_log_entry *entry, uint32_t offset) {
    fseek(disk, offset, SEEK_SET);
    if (fread(entry, sizeof(struct wfs_log_entry), 1, disk) != 1) {
        return -1;  // Error reading log entry
    }
    return 0;
}

// Utility function to find a log entry by inode number
static struct wfs_log_entry *find_log_entry_by_inode(uint32_t inode_num) {
    struct wfs_log_entry *entry = malloc(sizeof(struct wfs_log_entry));
    uint32_t offset = superblock.head;  // Start at head of log

    while (offset > sizeof(struct wfs_sb)) {  // Traverse backwards through log
        if (read_log_entry(entry, offset) != 0) {
            free(entry);
            return NULL;  // Error reading log entry
        }

        if (entry->inode.inode_number == inode_num && !entry->inode.deleted) {
            // Found the latest valid entry for the inode
            // free(entry);
            return entry;
        }

        // Move to previous log entry (assuming fixed-size entries for simplicity)
        offset -= sizeof(struct wfs_log_entry);
    }

    free(entry);
    return NULL;  // Inode not found
}

// Function to find latest log entry for a given path
static struct wfs_log_entry *find_latest_log_entry(const char *path) {
    // Split path into components and traverse log to find each component
    // Start with root inode (inode number 0)
    uint32_t current_inode = 0;

    // Tokenize path and traverse each component
    char *token;
    char temp_path[MAX_PATH_LENGTH];
    strcpy(temp_path, path);
    token = strtok(temp_path, "/");

    while (token != NULL) {
        struct wfs_log_entry *entry = find_log_entry_by_inode(current_inode);
        if (entry == NULL || entry->inode.mode != S_IFDIR) {
            return NULL;  // Not found or not a directory
        }

        // Search for the token in the directory entries
        // Assume a function get_dentry_by_name() that searches directory entries
        struct wfs_dentry *dentry = get_dentry_by_name(entry, token);
        free(entry);
        if (dentry == NULL) {
            return NULL;  // Directory entry not found
        }

        current_inode = dentry->inode_number;
        token = strtok(NULL, "/");
    }

    return find_log_entry_by_inode(current_inode);
}


// Define FUSE operation functions
static int wfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    memset(stbuf, 0, sizeof(struct stat));

    // Retrieve the latest log entry for the given path
    struct wfs_log_entry *entry = find_latest_log_entry(path);
    if (entry == NULL) {
        // Path not found
        return -ENOENT;
    }

    // Populate the stat structure with the inode information
    struct wfs_inode *inode = &entry->inode;
    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->links;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_atime = inode->atime; // Not needed?
    stbuf->st_mtime = inode->mtime;
    stbuf->st_ctime = inode->ctime;

    // Free memory since find_latest_log_entry dynamically allocates it
    free(entry);
    return 0;
}

// Helper function to append a log entry to the log
static int append_log_entry(struct wfs_log_entry *entry) {
    // Seek to the end of the log
    fseek(disk, superblock.head, SEEK_SET);

    // Write the log entry
    if (fwrite(entry, sizeof(struct wfs_log_entry), 1, disk) != 1) {
        return -errno;  // Write failed
    }

    // Update the superblock with the new head position
    superblock.head += sizeof(struct wfs_log_entry);
    fseek(disk, 0, SEEK_SET);  // Go back to the beginning of the disk
    if (fwrite(&superblock, sizeof(struct wfs_sb), 1, disk) != 1) {
        return -errno;  // Failed to update superblock
    }

    return 0;  // Success
}

// Helper function to update a directory's log entry
static int update_directory_log_entry(const char *dir_path, struct wfs_dentry *new_dentry) {
    struct wfs_log_entry *dir_entry = find_latest_log_entry(dir_path);
    if (dir_entry == NULL) {
        return -ENOENT;  // Directory not found
    }

    // Calculate the size of the new log entry
    size_t new_size = sizeof(struct wfs_inode) + dir_entry->inode.size + sizeof(struct wfs_dentry);

    // Allocate memory for the updated log entry
    struct wfs_log_entry *updated_entry = malloc(new_size);
    if (!updated_entry) {
        return -ENOMEM;  // Memory allocation failed
    }

    // Copy the existing data to the new log entry
    memcpy(updated_entry, dir_entry, sizeof(struct wfs_inode) + dir_entry->inode.size);

    // Add the new directory entry to the end of the updated entry
    struct wfs_dentry *dentry_list = (struct wfs_dentry *)(updated_entry->data);
    memcpy(&dentry_list[dir_entry->inode.size / sizeof(struct wfs_dentry)], new_dentry, sizeof(struct wfs_dentry));

    // Update inode size to reflect the new entry
    updated_entry->inode.size += sizeof(struct wfs_dentry);

    // Append the updated log entry to the log
    int append_result = append_log_entry(updated_entry);

    // Free the allocated memory for updated_entry
    free(updated_entry);

    return append_result;
}

// Helper function to parse a path into parent directory and file name
static void parse_path(const char *path, char *parent_dir, char *file_name) {
    char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        // Handle the case where there is no slash in the path
        strcpy(parent_dir, ".");
        strcpy(file_name, path);
    } else {
        strcpy(file_name, last_slash + 1);
        size_t parent_len = last_slash - path;
        strncpy(parent_dir, path, parent_len);
        parent_dir[parent_len] = '\0';  // Null-terminate the parent directory path
    }
}


static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    // Check if the path already exists
    if (find_latest_log_entry(path) != NULL) {
        // File already exists
        return -EEXIST;
    }

    // Parse the path to get the parent directory and the new file's name
    char parent_dir[MAX_PATH_LENGTH];
    char file_name[MAX_FILE_NAME_LEN];
    // Split the path into parent directory and file name
    parse_path(path, parent_dir, file_name);

    // Create a new inode for the file
    struct wfs_inode new_inode;
    new_inode.inode_number = get_next_inode_number();
    new_inode.deleted = 0;
    new_inode.mode = mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.flags = 0;
    new_inode.size = 0;
    new_inode.atime = new_inode.mtime = new_inode.ctime = time(NULL);
    new_inode.links = 1;

    // Create a new log entry for the file
    struct wfs_log_entry new_entry;
    new_entry.inode = new_inode;
    // new_entry.data = NULL; // For empty file

    // Append the new log entry
    if (append_log_entry(&new_entry) != 0) {
        // Handle error
        return -errno;
    }

    // Update the parent directory's log entry to include the new file
    struct wfs_dentry new_dentry;
    strcpy(new_dentry.name, file_name);
    new_dentry.inode_number = new_entry.inode.inode_number;

    if (update_directory_log_entry(parent_dir, &new_dentry) != 0) {
        // Handle error
        return -errno;
    }

    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    // Check if the directory already exists
    if (find_latest_log_entry(path) != NULL) {
        // Directory already exists
        return -EEXIST;
    }

    // Parse the path to get the parent directory and the new directory's name
    char parent_dir[MAX_PATH_LENGTH];
    char dir_name[MAX_FILE_NAME_LEN];
    parse_path(path, parent_dir, dir_name);

    // Create a new inode for the directory
    struct wfs_inode new_inode;
    new_inode.inode_number = get_next_inode_number();
    new_inode.deleted = 0;
    new_inode.mode = S_IFDIR | mode;  // Directory type
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.flags = 0;
    new_inode.size = 0;  // Initially, directory empty
    new_inode.atime = new_inode.mtime = new_inode.ctime = time(NULL);
    new_inode.links = 1;

    // Create a new log entry for the directory
    struct wfs_log_entry new_entry;
    new_entry.inode = new_inode;
    // new_entry.data = NULL;  // No directory entries yet

    // Append the new log entry
    if (append_log_entry(&new_entry) != 0) {
        return -errno;  // Error appending the log entry
    }

    // Update the parent directory's log entry to include the new directory
    struct wfs_dentry new_dentry;
    strcpy(new_dentry.name, dir_name);
    new_dentry.inode_number = new_inode.inode_number;

    if (update_directory_log_entry(parent_dir, &new_dentry) != 0) {
        return -errno;  // Error updating the parent directory
    }

    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Find the log entry for the file
    struct wfs_log_entry *entry = find_latest_log_entry(path);
    if (entry == NULL) {
        // File does not exist
        return -ENOENT;
    }

    // Check if offset is valid
    if (offset >= entry->inode.size) {
        // Offset is beyond the end of the file
        return 0;
    }

    // Calculate the number of bytes to read
    size_t bytes_to_read = entry->inode.size - offset;
    if (bytes_to_read > size) {
        bytes_to_read = size;
    }

    // Copy the data from the log entry to the buffer
    memcpy(buf, entry->data + offset, bytes_to_read);

    return bytes_to_read;  // Return the number of bytes read
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Find the log entry for the file
    struct wfs_log_entry *old_entry = find_latest_log_entry(path);
    if (old_entry == NULL) {
        // File does not exist
        return -ENOENT;
    }

    // Calculate new size
    size_t new_size = offset + size;
    if (new_size > old_entry->inode.size) {
        old_entry->inode.size = new_size;
    }

    // Create a new log entry with updated content
    struct wfs_log_entry *new_entry = malloc(sizeof(struct wfs_log_entry) + new_size);
    if (new_entry == NULL) {
        return -ENOMEM; // Memory allocation failed
    }

    // Copy the old data up to the offset
    memcpy(new_entry->data, old_entry->data, offset);

    // Copy the new data from buf into the new entry
    memcpy(new_entry->data + offset, buf, size);

    // Update the inode information
    new_entry->inode = old_entry->inode;

    // Append the new log entry
    int res = append_log_entry(new_entry);

    // Free the memory allocated for the new entry
    free(new_entry);

    return res < 0 ? res : size; // Return the number of bytes written
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // Find the log entry for the directory
    struct wfs_log_entry *entry = find_latest_log_entry(path);
    if (entry == NULL) {
        // Directory does not exist
        return -ENOENT;
    }

    if (entry->inode.mode & S_IFDIR == 0) {
        // Not a directory
        return -ENOTDIR;
    }

    // The '.' and '..' entries are usually added automatically, but can add them manually
    // filler(buf, ".", NULL, 0);
    // filler(buf, "..", NULL, 0);

    // Iterate over the directory entries in the log entry
    int num_entries = entry->inode.size / sizeof(struct wfs_dentry);
    struct wfs_dentry *dentry = (struct wfs_dentry *)entry->data;
    for (int i = 0; i < num_entries; ++i) {
        if (filler(buf, dentry[i].name, NULL, 0) != 0) {
            // Buffer is full, can't add more entries
            return 0;
        }
    }

    return 0;
}

static int remove_directory_entry(const char *dir_path, const char *name) {
    struct wfs_log_entry *dir_entry = find_latest_log_entry(dir_path);
    if (dir_entry == NULL) {
        // Directory does not exist
        return -ENOENT;
    }

    // Calculate the number of entries in the directory
    int num_entries = dir_entry->inode.size / sizeof(struct wfs_dentry);
    int entry_index = -1;

    // Find the index of the entry to remove
    struct wfs_dentry *dentry = (struct wfs_dentry *) dir_entry->data;
    for (int i = 0; i < num_entries; ++i) {
        if (strcmp(dentry[i].name, name) == 0) {
            entry_index = i;
            break;
        }
    }

    if (entry_index == -1) {
        // Entry not found in the directory
        return -ENOENT;
    }

    // Create a new log entry for the updated directory
    size_t new_size = dir_entry->inode.size - sizeof(struct wfs_dentry);
    struct wfs_log_entry *new_entry = malloc(sizeof(struct wfs_log_entry) + new_size);
    if (new_entry == NULL) {
        return -ENOMEM; // Memory allocation failed
    }

    // Copy the existing data to the new log entry, excluding the removed entry
    memcpy(new_entry, dir_entry, sizeof(struct wfs_inode));
    memcpy(new_entry->data, dir_entry->data, entry_index * sizeof(struct wfs_dentry));
    memcpy(new_entry->data + entry_index * sizeof(struct wfs_dentry), 
           dir_entry->data + (entry_index + 1) * sizeof(struct wfs_dentry), 
           (num_entries - entry_index - 1) * sizeof(struct wfs_dentry));

    // Update the inode size
    new_entry->inode.size = new_size;

    // Append the new log entry
    int res = append_log_entry(new_entry);

    // Free the memory for the new entry
    free(new_entry);

    return res;
}

static int wfs_unlink(const char *path) {
    // Find the log entry for the file to be deleted
    struct wfs_log_entry *entry = find_latest_log_entry(path);
    if (entry == NULL) {
        // File does not exist
        return -ENOENT;
    }

    // Create a new log entry to mark the file as deleted
    struct wfs_log_entry new_entry = *entry;
    new_entry.inode.deleted = 1;  // Mark as deleted

    // Append the new log entry to the log
    if (append_log_entry(&new_entry) != 0) {
        return -errno;  // Error appending the log entry
    }

    // Update the parent directory to remove the entry for this file
    char parent_dir[MAX_PATH_LENGTH];
    char file_name[MAX_FILE_NAME_LEN];
    // A function to split the path into parent directory and file name
    split_path(path, parent_dir, file_name);

    if (remove_directory_entry(parent_dir, file_name) != 0) {
        return -errno;  // Error removing the directory entry
    }

    return 0;
}

static struct fuse_operations wfs_ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
    .unlink = wfs_unlink,
};

int main(int argc, char *argv[]) {
    // Process and separate custom arguments and FUSE arguments
    // ...

    // Initialize filesystem state (like opening the disk image file)
    // ...

    // Call fuse_main with the FUSE operations
    // return fuse_main(argc, argv, &wfs_ops, NULL);
    return fuse_main(argc, argv, &wfs_ops);
}
