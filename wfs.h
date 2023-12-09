#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <libgen.h>
#include <string.h>
#include <stddef.h>

#define MAX_SIZE 1000000 // 1 MB File
#define MAX_PATH_LENGTH 128
#define MAX_INODES 1000
#define FUSE_USE_VERSION 30

#ifndef S_IFDIR
#define S_IFDIR  0040000  /* directory */
#endif
#ifndef S_IFREG
#define S_IFREG  0100000  /* regular */
#endif

#ifndef MOUNT_WFS_H_
#define MOUNT_WFS_H_

#define MAX_FILE_NAME_LEN 32
#define WFS_MAGIC 0xdeadbeef

int inodeCounter = 0; // Counter for inode numbers
int totalSize; // Total size of log
char *disk; // Path to disk image file
char *mnt; // Path to mount point
char *head; // Head of log
char *tail; // Tail of log
struct wfs_sb *superblock; // Superblock of filesystem

struct wfs_sb {
    uint32_t magic;
    uint32_t head;
};

struct wfs_inode {
    unsigned int inode_number;
    unsigned int deleted;       // 1 if deleted, 0 otherwise
    unsigned int mode;          // type. S_IFDIR if the inode represents a directory or S_IFREG if it's for a file
    unsigned int uid;           // user id
    unsigned int gid;           // group id
    unsigned int flags;         // flags
    unsigned int size;          // size in bytes
    unsigned int atime;         // last access time
    unsigned int mtime;         // last modify time
    unsigned int ctime;         // inode change time (the last time any field of inode is modified)
    unsigned int links;         // number of hard links to this file (this can always be set to 1)
};

struct wfs_dentry {
    char name[MAX_FILE_NAME_LEN];
    unsigned long inode_number;
};

struct wfs_log_entry {
    struct wfs_inode inode;
    char data[];
};

#endif
