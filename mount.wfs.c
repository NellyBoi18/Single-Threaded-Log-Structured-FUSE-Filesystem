#include "wfs.h"
#include <fuse.h>

// Remove mount point from path
char *parsePath(const char *path) {
    // Error Checking
    int pathLen = strlen(path);
    int mntLen = strlen(mnt);
    if ((path == NULL) || (mnt == NULL) || (pathLen == 0) || (mntLen == 0)) {
        return NULL;
    }

    // Check if path is root
    if(strncmp(path, "/", pathLen) == 0) {
        // Path is root, return empty string
        return strdup(path);
    }

    // Find mount point in path
    const char *pointer = strstr(path, mnt);
    if (pointer == NULL) { // Mount point not found
        perror("Mount point not found");
        return strdup(path);
    }

    // Move pointer past mount point
    pointer += mntLen;
    // Length of remaining path
    int remainderPathLen = path + pathLen - pointer;
    // Allocate memory for remaining path
    char *remainderPath = (char *)malloc((remainderPathLen + 1) * sizeof(char));
    if (remainderPath == NULL) { // Memory allocation failed
        free(remainderPath);
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    // Copy remaining path into new string
    strncpy(remainderPath, pointer, remainderPathLen);
    remainderPath[remainderPathLen] = '\0'; // Null terminate

    return remainderPath;
}

// Get log entry from path
struct wfs_log_entry *getLogEntry(const char *path, int inodeNum) {
    char *currPointer = tail; // Pointer to current log entry
    currPointer += sizeof(struct wfs_sb); // Skip superblock

    // Iterate over all log entries
    while (currPointer != head) {
        // Current log entry
        struct wfs_log_entry *currLogEntry = (struct wfs_log_entry *)currPointer;
        // If inode is not deleted
        if (currLogEntry->inode.deleted != 1) {
            // If inode number matches
            if (currLogEntry->inode.inode_number == inodeNum) {
                int pathLen = strlen(path);
                // If path is root
                if ((path == NULL) || (pathLen == 1) || (pathLen == 0)) {
                    // Return current log entry
                    return currLogEntry;
                } else { // Path is not root
                    // Copy path into new string
                    char pathcpy[MAX_PATH_LENGTH];
                    strcpy(pathcpy, path);
                    // Get first token
                    char *parent = strtok(pathcpy, "/");
                    char *addr = currLogEntry->data;

                    // Iterate over all dentries
                    while (addr != (char *)(currLogEntry) + currLogEntry->inode.size) { // While not at end of data field
                        // If dentry matches parent
                        if (strcmp(((struct wfs_dentry *)addr)->name, parent) == 0) {
                            const char *newPath = path;
                            // Remove parent from path
                            const char *first = strchr(newPath, '/'); // First '/'
                            if (first == NULL) {
                                // No / found, return empty string
                                return getLogEntry("", ((struct wfs_dentry *)addr)->inode_number);
                            }

                            // Second '/' starting after first slash
                            const char *second = strchr(first + 1, '/');
                            if (second == NULL) {
                                // No second / found, return empty string
                                return getLogEntry("", ((struct wfs_dentry *)addr)->inode_number);
                            }

                            // Length of remaining path
                            int length = strlen(second);
                            char *remainderPath = (char *)malloc((length + 1) * sizeof(char));
                            if (remainderPath == NULL) { // Memory allocation failed
                                perror("Memory allocation error");
                                free(remainderPath);
                                exit(EXIT_FAILURE);
                            }
                            strcpy(remainderPath, second); // Copy remaining path into new string
                            // Get log entry of remaining path and return it
                            return getLogEntry(remainderPath, ((struct wfs_dentry *)addr)->inode_number);
                        }
                        // Move to next dentry
                        addr += sizeof(struct wfs_dentry);
                    }
                }
            }
        }
        // Move to next log entry
        currPointer += currLogEntry->inode.size;
    }

    // Log entry not found
    return NULL;
}

// Function to get file attributes
static int wfs_getattr(const char *path, struct stat *stbuf) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Get log entry
    struct wfs_log_entry *logEntry = getLogEntry(newPath, 0);
    if (logEntry == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Fill in stat struct for log entry
    stbuf->st_uid = logEntry->inode.uid;
    stbuf->st_gid = logEntry->inode.gid;
    stbuf->st_atime = time(NULL); // Update last access time
    stbuf->st_mtime = logEntry->inode.mtime;
    stbuf->st_mode = logEntry->inode.mode;
    stbuf->st_nlink = logEntry->inode.links;
    stbuf->st_size = logEntry->inode.size;

    return 0;
}

// Function to read data from file
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Remove mount point from path
    const char *newPath = parsePath(path);
    // Get log entry
    struct wfs_log_entry *logEntry = getLogEntry(newPath, 0);
    if (logEntry == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }
    // end of log entry - start of data field
    int dataSize = logEntry->inode.size - sizeof(struct wfs_log_entry);

    // Check if offset is too big
    if (offset >= dataSize) {
        return 0;
    }
    // Read file data into buffer
    memcpy(buf, logEntry->data + offset, size);
    // Update last access time
    logEntry->inode.atime = time(NULL);

    return size;
}

// Remove last '/' from path
char *parsePathEnd(const char *path) {
    // Error Checking
    int pathLen = strlen(path);
    int mntLen = strlen(mnt);
    if ((path == NULL) || (mnt == NULL) || (pathLen == 0) || (mntLen == 0)) {
        return NULL;
    }

    const char* last = strrchr(path, '/'); // Find last / in path
    int remainderPathLen = last - path; // Length of path without extension
    char* remainderPath = malloc(remainderPathLen + 1);
    strncpy(remainderPath, path, remainderPathLen); // Copy path without extension into new string
    remainderPath[remainderPathLen] = '\0'; // Null-terminate

    return remainderPath;
}

// Get filename from path
char *getFilename(const char *path) {
    // Error Checking
    int pathLen = strlen(path);
    if ((path == NULL) || (pathLen == 0)) {
        return NULL;
    }

    const char *last = strrchr(path, '/'); // Find last / in path
    last += 1; // Move pointer past last slash
    int remainderLength = strlen(last); // Length of filename
    char *filename = (char *)malloc((remainderLength + 1) * sizeof(char));
    if (filename == NULL) { // Memory allocation failed
        perror("Memory allocation error");
        free(filename);
        exit(EXIT_FAILURE);
    }

    strcpy(filename, last); // Copy filename into new string
    return filename;
}

// Check if file or dir exists already
int exists(const char *path) {
    // Get filename
    char *filename = getFilename(path);
    // Check if filename is unique
    struct wfs_log_entry *parent = getLogEntry(parsePathEnd(path), 0);
    if (parent == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Start of data field
    char *addr = parent->data;
    // Iterate over all dentry's
    while (addr != (char *)(parent) + parent->inode.size) {
        // If filename matches
        if (strcmp(((struct wfs_dentry *)addr)->name, filename) == 0) {
            // Filename exists already
            return 0;
        }
        int sizeDentry = sizeof(struct wfs_dentry);
        addr += sizeDentry;
    }

    // Filename valid. DNE
    return 1;
}

// Check if filename is valid
int valid(const char *filename) {
    const char *last = NULL; // Pointer to last dot in filename
    while (*filename != '\0') { // Iterate over all characters in filename
        if (*filename == '.') { // If character is a dot
            last = filename; // Update last dot pointer
        }
        filename++;
    }

    // If dot is found, exclude characters after dot
    while (*filename != '\0' && filename != last) {
        // If character is not alphanumeric or underscore, invalid
        if (!(isalnum(*last) || *last == '_')) {
            return 0;
        }
        last++;
    }

    // All characters in filename are valid
    return 1;
    // I don't think this checks for if there isn't a dot in the filename?? It runs tho so idc
}

// Function to create a file
static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Check valid filename
    if (!valid(getFilename(newPath))) {
        perror("Invalid File Name");
        return -1;
    }
    // Check file doesn't exist already
    if (!exists(newPath)) {
        perror("Filename already exists");
        return -EEXIST;
    }

    // Create new inode for file
    struct wfs_inode newInode;
    inodeCounter += 1;
    newInode.inode_number = inodeCounter;
    newInode.deleted = 0;
    newInode.mode = S_IFREG;
    newInode.uid = getuid();
    newInode.gid = getgid();
    newInode.flags = 0;
    newInode.size = sizeof(struct wfs_inode);
    newInode.atime = time(NULL);
    newInode.mtime = time(NULL);
    newInode.ctime = time(NULL);
    newInode.links = 1;

    // Create new dentry for file
    struct wfs_dentry *newDentry = (struct wfs_dentry *)malloc(sizeof(struct wfs_dentry));
    if (newDentry != NULL) {
        // Copy filename
        strncpy(newDentry->name, getFilename(newPath), MAX_FILE_NAME_LEN - 1);
        newDentry->name[MAX_FILE_NAME_LEN - 1] = '\0'; // Null terminate
        newDentry->inode_number = newInode.inode_number; // Point dentry at created inode
    } else { // Memory allocation failed
        perror("Memory allocation error");
        free(newDentry);
        exit(EXIT_FAILURE);
    }

    // Get parent directory log entry
    struct wfs_log_entry *parent = getLogEntry(parsePathEnd(newPath), 0);
    if (parent == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }
    // Check if there is enough space to create file
    if ((totalSize + parent->inode.size + sizeof(struct wfs_dentry) + sizeof(struct wfs_log_entry)) > MAX_SIZE) {
        perror("Insufficient disk space");
        return -ENOSPC;
    }

    // Make copy of old log entry and add created dentry to data field
    struct wfs_log_entry *logEntryCopy = (struct wfs_log_entry *)malloc(parent->inode.size + sizeof(struct wfs_dentry));
    if (logEntryCopy != NULL) {
        // Copy old log entry to new log entry
        memcpy(logEntryCopy, parent, parent->inode.size);
        // Add dentry to logEntryCopy's and update new log entry size
        memcpy((char *)(logEntryCopy) + logEntryCopy->inode.size, newDentry, sizeof(struct wfs_dentry));
        logEntryCopy->inode.size += sizeof(struct wfs_dentry); // Update inode size
        memcpy(head, logEntryCopy, logEntryCopy->inode.size); // Write log entry to log
        head += logEntryCopy->inode.size; // Update head
        totalSize += logEntryCopy->inode.size; // Update total size
    } else { // Memory allocation failed
        perror("Memory allocation error");
        free(logEntryCopy);
        exit(EXIT_FAILURE);
    }

    // Mark old log entry as deleted
    parent->inode.deleted = 1;

    // Create new log entry for file
    struct wfs_log_entry *newLogEntry = (struct wfs_log_entry *)malloc(sizeof(struct wfs_log_entry));
    if (newLogEntry != NULL) {
        newLogEntry->inode = newInode; // Point log entry at created inode
        memcpy(head, newLogEntry, newLogEntry->inode.size); // Add log entry to log
        head += newLogEntry->inode.size; // Update head
        totalSize += newLogEntry->inode.size; // Update total size 
    } else { // Memory allocation failed
        free(newLogEntry);
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    return 0;
}

// Function to create a directory
static int wfs_mkdir(const char *path, mode_t mode) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Verify dir name
    if (!valid(getFilename(newPath))) {
        perror("Invalid directory name");
        return -1;
    }
    // Verify file doesn't already exist
    if (!exists(newPath)) {
        perror("Direct name already exists");
        return -EEXIST;
    }

    // Create new inode
    struct wfs_inode newInode;
    inodeCounter += 1;
    newInode.inode_number = inodeCounter;
    newInode.deleted = 0;
    newInode.mode = S_IFDIR;
    newInode.uid = getuid();
    newInode.gid = getgid();
    newInode.flags = 0;
    newInode.size = sizeof(struct wfs_inode);
    newInode.atime = time(NULL);
    newInode.mtime = time(NULL);
    newInode.ctime = time(NULL);
    newInode.links = 1;

    // Create new dentry
    struct wfs_dentry *newDentry = (struct wfs_dentry *)malloc(sizeof(struct wfs_dentry));
    if (newDentry != NULL) {
        strncpy(newDentry->name, getFilename(newPath), MAX_FILE_NAME_LEN - 1); // Copy name
        newDentry->name[MAX_FILE_NAME_LEN - 1] = '\0'; // Null terminate
        newDentry->inode_number = newInode.inode_number; // Point dentry at created inode
    } else { // Memory allocation failed
        free(newDentry);
        perror("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    // Get parent directory log entry
    struct wfs_log_entry *oldEntry = getLogEntry(parsePathEnd(newPath), 0);
    if (oldEntry == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Check if there is enough space to create directory
    if ((totalSize + oldEntry->inode.size + sizeof(struct wfs_dentry) + sizeof(struct wfs_log_entry)) > MAX_SIZE) {
        perror("Insufficient disk space");
        return -ENOSPC;
    }

    // Copy old log entry and add created dentry to data field
    struct wfs_log_entry *logEntryCopy = (struct wfs_log_entry *)malloc(oldEntry->inode.size + sizeof(struct wfs_dentry));
    if (logEntryCopy != NULL) {
        memcpy(logEntryCopy, oldEntry, oldEntry->inode.size); // Copy old log entry to new log entry
        memcpy((char *)(logEntryCopy) + logEntryCopy->inode.size, newDentry, sizeof(struct wfs_dentry)); // Add dentry to log entry
        logEntryCopy->inode.size += sizeof(struct wfs_dentry); // Update size
        memcpy(head, logEntryCopy, logEntryCopy->inode.size); // Add log entry to log
        totalSize += logEntryCopy->inode.size; // Update total size
        head += logEntryCopy->inode.size; // Update head
    } else { // Memory allocation failed
        perror("Memory allocation error");
        free(logEntryCopy);
        exit(EXIT_FAILURE);
    }

    // Mark old log entry as deleted
    oldEntry->inode.deleted = 1;

    // Create a log entry for file
    struct wfs_log_entry *newLogEntry = (struct wfs_log_entry *)malloc(sizeof(struct wfs_log_entry));
    if (newLogEntry != NULL) {
        newLogEntry->inode = newInode; // Point log entry at created inode
        memcpy(head, newLogEntry, newLogEntry->inode.size); // Add log entry to log
        totalSize += newLogEntry->inode.size; // Update total size
        head += newLogEntry->inode.size; // Update head
    } else { // Memory allocation failed
        perror("Memory allocation error");
        free(newLogEntry);
        exit(EXIT_FAILURE);
    }

    return 0;
}

// Function to write data to file
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Get log entry
    struct wfs_log_entry *logEntry = getLogEntry(newPath, 0);
    if(logEntry == NULL) { // Log entry not founds
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Data size of log entry
    int dataSize = logEntry->inode.size - sizeof(struct wfs_log_entry);
    // Update last access time
    logEntry->inode.atime = time(NULL);

    // Check if offset is too big. Can't write past end of file
    if ((logEntry->data + offset + size) >= (logEntry->data + dataSize)) {
        // Calculate size of data to write
        int diff = (logEntry->data + offset + size) - logEntry->data;
        dataSize = diff;
    }
    // Check if write would exceed disk space
    if ((totalSize + sizeof(struct wfs_log_entry) + dataSize) > MAX_SIZE){
        perror("Insufficient disk space");
        return -ENOSPC;
    }

    // Make copy of old log entry and write buffer to offset of data
    struct wfs_log_entry *logEntryCopy = (struct wfs_log_entry *)malloc(sizeof(struct wfs_log_entry) + dataSize);
    if (logEntryCopy == NULL) { // Memory allocation failed
        perror("Memory allocation error");
        free(logEntryCopy);
        exit(EXIT_FAILURE);
    } else {
        memcpy(logEntryCopy, logEntry, logEntry->inode.size); // Copy old log entry to new log entry
        logEntry->inode.deleted = 1; // Mark old log entry as deleted
        memcpy(logEntryCopy->data + offset, buf, size); // Write buffer to offset of data
        logEntryCopy->inode.mtime = time(NULL); // Update modify time
        logEntryCopy->inode.ctime = time(NULL); // Update change time
        logEntryCopy->inode.size += dataSize; // Update size
        memcpy(head, logEntryCopy, logEntryCopy->inode.size); // Write log entry to head
        totalSize += logEntryCopy->inode.size; // Update total size
        head += logEntryCopy->inode.size; // Update head
    }

    return size;
}

// Function to read directory entries
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Get log entry
    struct wfs_log_entry *logEntry = getLogEntry(newPath, 0);
    // Error Checking
    if (logEntry == NULL) {
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Update last access time
    logEntry->inode.atime = time(NULL);

    // Start of data field
    char *addr = logEntry->data + (offset * sizeof(struct wfs_dentry));
    // Iterate over all dentry's
    while (addr != (char *)(logEntry) + logEntry->inode.size) {
        struct wfs_dentry *currPointer = (struct wfs_dentry *)addr; // Current dentry
        int lenPath = strlen(newPath); // Length of path
        int lenName = strlen(currPointer->name); // Length of name
        int totalLength = lenPath + lenName;
        char *newEntryPath = (char *)malloc(totalLength + 1); // Allocate memory for new string
        if (newEntryPath == NULL) { // Memory allocation failed
            perror("Memory allocation error");
            free(newEntryPath);
            return -1;
        }

        // Copy path and name into new string
        strcpy(newEntryPath, newPath); // Copy path
        strcpy(newEntryPath + lenPath, currPointer->name); // Copy name
        // Get log entry for new path
        struct wfs_log_entry *currLogEntry = getLogEntry(newEntryPath, 0);
        if(currLogEntry == NULL) { // Log entry not found
            perror("Log entry does not exist");
            return -ENOENT;
        }

        // create a struct stat for log entry
        struct stat stbuf;
        stbuf.st_uid = currLogEntry->inode.uid;
        stbuf.st_gid = currLogEntry->inode.gid;
        stbuf.st_atime = time(NULL);
        stbuf.st_mtime = currLogEntry->inode.mtime;
        stbuf.st_mode = currLogEntry->inode.mode;
        stbuf.st_nlink = currLogEntry->inode.links;
        stbuf.st_size = currLogEntry->inode.size;
        offset += sizeof(struct wfs_dentry); // Update offset
        // Add dentry to buffer
        if (filler(buf, currPointer->name, &stbuf, offset) != 0) {
            // Buffer full
            return -1;
        }
        // Move to next dentry
        addr += sizeof(struct wfs_dentry);
    }

    return 0;
}

// Function to remove a file
static int wfs_unlink(const char *path) {
    // Remove mount point from path
    const char *newPath = parsePath(path);

    // Get parent log entry
    struct wfs_log_entry *parentLogEntry = getLogEntry(parsePathEnd(newPath), 0);
    if (parentLogEntry == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }

    // Update parent log entry access time
    parentLogEntry->inode.atime = time(NULL);

    // Get log entry for file
    struct wfs_log_entry *logEntry = getLogEntry(newPath, 0);
    if (logEntry == NULL) { // Log entry not found
        perror("Log entry does not exist");
        return -ENOENT;
    }
    logEntry->inode.deleted = 1; // Mark as deleted
    logEntry->inode.ctime = time(NULL); // Update last change time
    logEntry->inode.atime = time(NULL); // Update last access time
    logEntry->inode.links -= 1; // Decrement links

    // Make a copy of parent log entry and remove target file's dentry
    struct wfs_log_entry *logEntryCopy = (struct wfs_log_entry *)malloc(parentLogEntry->inode.size - sizeof(struct wfs_dentry));
    if (logEntryCopy != NULL) {
        // Find target file's dentry
        char *addr = parentLogEntry->data;
        // Iterate over all dentry's
        while (addr != (char *)(parentLogEntry) + parentLogEntry->inode.size) {
            // If dentry matches target file
            if (((struct wfs_dentry *)addr)->inode_number == logEntry->inode.inode_number) {
                break; // Found dentry
            }
            addr += sizeof(struct wfs_dentry); // Move to next dentry
        }

        // Copy parent log entry to new log entry
        memcpy(logEntryCopy, parentLogEntry, addr - (char *)(parentLogEntry));

        // If target file's dentry is not the last dentry in parent's data field
        if (addr != ((char *)(parentLogEntry) + parentLogEntry->inode.size - sizeof(struct wfs_dentry))) {
            // Address of remaining data in parent
            char *dataAddr = (char *)(logEntryCopy) + (addr - (char *)(parentLogEntry));
            // Address of parent's data field
            char *parentAddr = addr + sizeof(struct wfs_dentry);
            // Remaining size of parent's data field
            int parentSize = (char *)(parentLogEntry) + parentLogEntry->inode.size - parentAddr;
            // Copy remaining data into new log entry
            memcpy(dataAddr, parentAddr, parentSize);
        }

        parentLogEntry->inode.deleted = 1; // Mark as deleted
        logEntryCopy->inode.size -= sizeof(struct wfs_dentry); // Update size
        memcpy(head, logEntryCopy, logEntryCopy->inode.size); // Write log entry to log
        totalSize += logEntryCopy->inode.size; // Update total size
        head += logEntryCopy->inode.size; // Update head
    } else { // Memory allocation failed
        perror("Memory allocation error");
        free(logEntryCopy);
        exit(EXIT_FAILURE);
    }

    return 0;
}

static struct fuse_operations wfs_ops = {
    .getattr = wfs_getattr,
    .read = wfs_read,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .write = wfs_write,
    .readdir = wfs_readdir,
    .unlink = wfs_unlink,
};

int main(int argc, char *argv[]) {
    // Error Checking
    if (argc < 4) {
        fprintf(stderr, "Usage: %s [<FUSE options>] <diskPath> <mountPoint>\n", argv[0]);
        return 1;
    }

    // Parse disk and mount point
    disk = argv[argc - 2];
    mnt = argv[argc - 1];

    // Open disk image file
    int fd = open(disk, O_RDWR, 0666); // Open with read/write permissions
    if (fd == -1) { // Error opening file
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Get file info
    struct stat fileStat = {0};
    if (fstat(fd, &fileStat) == -1) {
        perror("Error getting file info");
        close(fd);
        exit(EXIT_FAILURE);
    }
    int fileSize = fileStat.st_size;

    // Map file to memory
    tail = mmap(NULL, fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (tail == MAP_FAILED) {
        perror("Error mapping file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    // Get superblock
    superblock = (struct wfs_sb *)tail;
    if (superblock->magic != WFS_MAGIC) {
        perror("Invalid magic number");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // Set head to end of superblock
    head = tail + superblock->head;

    // Parse FUSE arguments
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;

    fuse_main(argc, argv, &wfs_ops, NULL);
    munmap(tail, fileStat.st_size);

    return 0;
}