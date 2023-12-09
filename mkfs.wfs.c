#include "wfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// FILE *disk = NULL;
// struct wfs_sb superblock;

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s disk_path\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *disk_path = argv[1];

    // Open disk image file
    FILE *disk = fopen(disk_path, "wb");
    // *disk = fopen(disk_path, "wb");
    if (!disk) {
        perror("Error opening disk image file");
        exit(EXIT_FAILURE);
    }

    // Initialize superblock
    struct wfs_sb sb = { .magic = WFS_MAGIC, .head = sizeof(struct wfs_sb) };
    // superblock = = { .magic = WFS_MAGIC, .head = sizeof(struct wfs_sb) };

    // Write superblock to disk
    if (fwrite(&sb, sizeof(sb), 1, disk) != 1) {
        perror("Error writing superblock");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    // Create and write initial log entry for root directory
    // Initialize root directory inode
    struct wfs_inode root_inode = {
        .inode_number = 0,            // Root directory,inode number 0
        .deleted = 0,
        .mode = S_IFDIR,              // Directory
        .uid = 0,                     // Root user
        .gid = 0,                     // Root group
        .flags = 0,
        .size = 0,                    // Initially empty
        .atime = 0,                   // Access time
        .mtime = 0,                   // Modify time
        .ctime = 0,                   // Inode change time
        .links = 1                    // Number of hard links
    };

    // Create root directory log entry
    struct wfs_log_entry root_entry = {
        .inode = root_inode,
        // Data part initially empty for root directory
    };

    // Write root directory log entry to the disk
    if (fwrite(&root_entry, sizeof(root_entry), 1, disk) != 1) {
        perror("Error writing root directory log entry");
        fclose(disk);
        exit(EXIT_FAILURE);
    }

    // Close disk image file
    fclose(disk);

    return EXIT_SUCCESS;
}
