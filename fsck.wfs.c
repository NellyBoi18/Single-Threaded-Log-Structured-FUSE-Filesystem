#include "wfs.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <diskPath>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Parse disk image file path
    disk = argv[1];

    // Open disk image file
    int fd = open(disk, O_RDWR, 0666);
    if (fd == -1) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    // Get file info
    struct stat fileStat;
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

    // Create temporary file for compacted log
    char tempFilename[] = "/tmp/fsck_wfs_TEMP";
    int tempFD = mkstemp(tempFilename);
    if (tempFD == -1) {
        perror("Error creating temporary file for compaction");
        return -1;
    }

    // Data structure to track the latest valid log entry for each inode
    struct wfs_log_entry *latestEntries = calloc(MAX_INODES, sizeof(struct wfs_log_entry));
    if (!latestEntries) {
        close(tempFD);
        unlink(tempFilename);
        return -ENOMEM;
    }

    // Read through entire log from beginning
    lseek(fd, sizeof(struct wfs_sb), SEEK_SET);  // Start after superblock
    struct wfs_log_entry entry;
    // Read each log entry
    while (read(fd, &entry, sizeof(struct wfs_log_entry)) == sizeof(struct wfs_log_entry)) {
        // If the inode is not deleted, update latestEntries array
        if (!entry.inode.deleted) {
            // Copy entry into latestEntries array at position of inode number
            memcpy(&latestEntries[entry.inode.inode_number], &entry, sizeof(struct wfs_log_entry));
        }
    }

    // Write compacted log entries back to temp file
    for (int i = 0; i < MAX_INODES; i++) {
        // If the inode is not deleted, write it to the temp file
        if (latestEntries[i].inode.inode_number != 0) {
            write(tempFD, &latestEntries[i], sizeof(struct wfs_log_entry));
        }
    }

    // Copy compacted log from temp file back to original disk file
    lseek(tempFD, 0, SEEK_SET);
    lseek(fd, sizeof(struct wfs_sb), SEEK_SET);  // Start after superblock
    // Read each log entry from temp file and write to disk file
    while (read(tempFD, &entry, sizeof(struct wfs_log_entry)) == sizeof(struct wfs_log_entry)) {
        write(fd, &entry, sizeof(struct wfs_log_entry));
    }

    // Update superblock to new end of log
    superblock->head = lseek(fd, 0, SEEK_CUR);
    lseek(fd, 0, SEEK_SET);  // Go back to beginning of disk
    write(fd, &superblock, sizeof(struct wfs_sb));

    // Clean up
    close(tempFD);
    unlink(tempFilename);
    free(latestEntries);
    close(fd);
    
    return EXIT_SUCCESS;
}
