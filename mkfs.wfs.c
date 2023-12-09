#include "wfs.h"

int main(int argc, char *argv[]) {
    // Error Checking
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <diskPath>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Open disk image file
    const char *path = argv[1];
    int fd = open(path, O_RDWR, 0666);
    if (fd == -1) {
        perror("Error opening disk image file");
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
    char* mem = mmap(NULL, fileSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) {
        perror("Error mapping file to memory");
        exit(EXIT_FAILURE);
    }

    // Initialize superblock
    struct wfs_sb* superblock = (struct wfs_sb*)mem;
    superblock->magic = WFS_MAGIC;
    superblock->head = sizeof(struct wfs_sb);

    // Initialize root inode
    struct wfs_inode root;
    root.inode_number = 0;
    root.deleted = 0;
    root.mode = S_IFDIR;
    root.uid = getuid();
    root.gid = getgid();
    root.flags = 0;
    root.size = sizeof(struct wfs_inode);
    root.atime = time(NULL);
    root.mtime = time(NULL);
    root.ctime = time(NULL);
    root.links = 0;

    // Initialize root log entry
    struct wfs_log_entry* rootLogEntry = (struct wfs_log_entry *)malloc(sizeof(struct wfs_log_entry));
    rootLogEntry->inode = root;
    memcpy((char *)(mem + superblock->head), rootLogEntry, rootLogEntry->inode.size);

    superblock->head += rootLogEntry->inode.size; // Update superblock head
    totalSize += rootLogEntry->inode.size + sizeof(struct wfs_sb); // Update total size
    munmap(mem, fileStat.st_size); // Write to disk

    free(rootLogEntry);
    close(fd);

    return 0;
}