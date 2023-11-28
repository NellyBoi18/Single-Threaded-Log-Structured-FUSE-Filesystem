# Introduction

In this project, I created a straightforward single-threaded log-structured filesystem using FUSE (Filesystem in Userspace). FUSE lets regular users build their own file systems without needing special permissions, opening up new possibilities for designing and using file systems. Your filesystem will handle basic tasks like reading, writing, making directories, deleting files, and more.

# Background

## FUSE

FUSE (Filesystem in Userspace) is a powerful framework that enables the creation of custom filesystems in user space rather than requiring modifications to the kernel. This approach simplifies filesystem development and allows developers to create filesystems with standard programming languages like C, C++, Python, and others.

To use FUSE in a C-based filesystem, you typically define callback functions for various filesystem operations such as getattr, read, write, mkdir, and more. These functions are registered as handlers for specific filesystem actions and are invoked by the FUSE library when these actions occur.

Here's an example demonstrating how to register a getattr function in a basic FUSE-based filesystem:

```c
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int my_getattr(const char *path, struct stat *stbuf) {
    // Implementation of getattr function to retrieve file attributes
    // Fill stbuf structure with the attributes of the file/directory indicated by path
    // ...

    return 0; // Return 0 on success
}

static struct fuse_operations my_operations = {
    .getattr = my_getattr,
    // Add other functions (read, write, mkdir, etc.) here as needed
};

int main(int argc, char *argv[]) {
    // Initialize FUSE with specified operations
    // Filter argc and argv here and then pass it to fuse_main
    return fuse_main(argc, argv, &my_operations, NULL);
}
```

This code demonstrates a basic usage of FUSE in C. The `my_getattr` function is an example of a callback function used to retrieve file attributes like permissions, size, and type. Other functions (like read, write, mkdir, etc.) can be similarly defined and added to my_operations. 

The `fuse_main` function initializes FUSE, passing the specified operations (in this case, my_operations) to handle various filesystem operations. This code structure allows you to define and register functions tailored to your filesystem's needs, enabling you to create custom filesystem behaviors using FUSE in C.

Remember to handle any program-specific arguments separately from those intended for FUSE. Filter out or process your program's arguments before passing them to `fuse_main` to prevent unintended issues or errors in the FUSE filesystem. 

## Mounting

The mount_point is a directory in the file system where the FUSE-based file system will be attached or "mounted." Once mounted, this directory serves as the entry point to access and interact with the FUSE file system. Any file or data within this mount point is associated with the FUSE file system, allowing users to read, write, and perform file operations as if they were interacting with a traditional disk.

A file can represent a container or virtual representation of a disk image. This file, when mounted to the mount_point for the FUSE file system, effectively acts as a disk image, presenting a virtual disk within the file system.

When the FUSE file system is mounted on a file (such as an image file), it's as if the contents of that file become accessible as a disk or filesystem.

## Log-Structured Filesystem

A log-structured filesystem stores data differently than traditional ones. In a log-structured filesystem, whenever data changes, instead of altering existing information directly, all modifications are added as new entries in a continuous log. This means that rather than modifying old data in place, each change is appended to the end of the log. This method keeps data organized and makes writing faster by avoiding scattered updates throughout the storage. 

![log-structured filesystem](disk_layout.drawio.svg)

Fig. a shows the layout of an empty disk. It consists of a superblock (stores the metadata of the filesystem) and a log entry (with inode number 0) for the root directory. 

If we create a file `/file.txt`, the new layout is illustrated in Fig. b. There are 2 changes to the disk image: 

1. We need to update the root directory to contain the file name `file.txt`. Since this is a log-structured filesystem, instead of modifying original log entry 0, we append an updated log entry 0 containing the new file name (colored green) with its log entry index. The original log entry 0 is now obsolete and shouldn't be used (colored gray). 

2. We need to append a log entry for the new file (log entry 1). The file is empty for now. 

Fig. c shows what happens after writing `Hello World\n` to `file.txt`. The previous log entry 1 is invalidated and a new log entry 1 with file content is appended. To find the log entry for `/file.txt`, start by retrieving the log entry 0 (root). Then, scan through the entries to pinpoint the inode number corresponding to `file.txt`. Finally, utilize this inode number to discover the most recent log entry associated with `/file.txt`. 

Fig. d shows the disk layout after a directory `/subdir` is created. Note that the log entry for the root directory (log entry 0) is again updated. 

Your implementation should use the structures provided in `wfs.h`. 

# Project details

- `mkfs.wfs.c`\
  This C program initializes a file to an empty filesystem. The program receives a path to the disk image file as an argument, i.e., 
  ```sh
  mkfs.wfs disk_path
  ```
  initializes the existing file `disk_path` to an empty filesystem (Fig. a). 
- `mount.wfs.c`\
  This program mounts the filesystem to a mount point, which are specifed by the arguments. The usage is 
  ```sh
  mount.wfs [FUSE options] disk_path mount_point
  ```
- `fsck.wfs.c`\
  This program compacts the log by removing redundancies. The disk_path is given as its argument, i.e., `fsck disk_path`.

## Features

My filesystem implements the following features: 

- Create an empty file

- Create an empty directory
  
- Write to an existing file\
  The file isn't truncated, implying that it preserves the existing content as if the file is open in `r+` mode.
- Read an existing file\
- Read a directory
- Remove an existing file
- Get attributes of an existing file/directory\
  Fill the following fields of struct stat
  - st_uid
  - st_gid
  - st_atime
  - st_mtime
  - st_mode
  - st_nlink
  - st_size

To simplify the project, the log-structured system doesn't wrap, which means it does not overwrite old data even when the disk is full. The filesystem is only compacted if `fsck.wfs` is executed. 

## Structures

In `wfs.h`, I provide the structures used in this filesystem. 

`wfs_log_entry` holds a log entry. `inode` contains necessary meta data for this entry. 

If a log entry represents a directory, `data` (a [flexible array member](https://gcc.gnu.org/onlinedocs/gcc/extensions-to-the-c-language-family/arrays-of-length-zero.html)) includes an array of `wfs_dentry`. Each `wfs_dentry` represents a file/directory within this folder. If the log entry is for a file, `data` contains the content of this file. 

Format of the superblock is defined by `wfs_sb`. We use the magic number `0xdeadbeef` as a special mark, and head shows where the next empty space starts on the disk. 

## Utilities

To help you run your filesystem, I provided several scripts: 

- `create_disk.sh` creates a file named `disk` with size 1M whose content is zeroed. You can use this file as your disk image. 
- `umount.sh` unmounts a mount point whose path is specified in the first argument. 
- `Makefile` compiles the code

One way to compile and launch the filesystem is: 

```sh
$ make
$ ./create_disk.sh         # creates file `disk`
$ ./mkfs.wfs disk          # initialize `disk`
$ mkdir mnt
$ ./mount.wfs -f -s disk mnt # mount. -f runs FUSE in foreground
```

You should be able to interact with your filesystem once you mount it: 

```sh
$ stat mnt
$ mkdir mnt/a
$ stat mnt/a
$ mkdir mnt/a/b
$ ls mnt
$ echo asdf > mnt/x
$ cat mnt/x
```

## Error handling

If any of the following issues occur during the execution of a registered function, it's essential to return the respective error code.

- File/directory does not exist while trying to read/write a file/directory\
  return `-ENOENT`
- A file or directory with the same name already exists while trying to create a file/directory\
  return `-EEXIST`
- There is insufficient disk space while trying to append a log entry\
  return `-ENOSPC`

## Debugging

### Inspect superblock
```sh
$ ./create_disk.sh
$ xxd -e -g 4 disk | less
$ ./mkfs.wfs disk
$ xxd -e -g 4 disk | less
```
Using the above command, you can see the exact contents present in the disk image, before mounting.

### Inspect log-entries
```sh
$ ...
$ mkdir mnt
$ ./mount.wfs -f -s disk mnt
$ mkdir mnt/a
$ ./umount.wfs mnt
$ xxd -e -g 4 disk | less
```
Again for inspecting contents of the disk image, after mounting has taken place.

## Important Notes

1. A directory name and file name can't be the same.
2. A valid file/directory name consists of letters (both uppercase and lowercase), numbers, and underscores (_). 
3. Set maximum allowable file name length to 32
4. The maximum path length is 128
