Overview

This project implements a simple FAT-based file system in C, simulating a virtual disk within the Linux file system. The file system provides basic functionalities, including creating, reading, writing, deleting, and truncating files. It is designed for educational purposes, demonstrating concepts of file system management, such as the use of a file allocation table (FAT), superblock, and root directory structures. The project also supports mounting and unmounting the virtual disk, enabling persistent file system data management across program executions.

File System Structures:

- Superblock: Contains metadata about the file system, including block sizes, locations of key structures, and the total number of free blocks.

- File Allocation Table (FAT): Tracks the allocation of data blocks and manages file storage chains.

- Root Directory: Stores file metadata, such as filenames, sizes, and starting FAT indices.

Implemented Functionalities:

a. Disk Operations:

- make_fs: Initializes a new file system on a virtual disk.
- mount_fs: Loads an existing file system into memory for operations.
- umount_fs: Safely writes all changes back to disk and unmounts the file system.

b. File Operations:

- fs_create: Creates a new file.
- fs_open: Opens a file and returns a file descriptor.
- fs_close: Closes an open file.
- fs_read: Reads data from a file into memory.
- fs_write: Writes data from memory into a file.
- fs_delete: Deletes a file and frees its allocated space.
- fs_trunc: Truncates a file to a specified size.

c. Utility Functions:

- fs_lseek: Moves the file offset within an open file.
- fs_get_filesize: Retrieves the size of an open file.
- find_free_block: Finds the next available data block in the FAT.

Technical Details

a. Virtual Disk:

Consists of 8,192 blocks (4 KB each), with a total capacity of 32 MB.
Allocates blocks for the superblock, two FAT tables, root directory, and data storage.

b. File System Design:

Supports up to 64 files in the root directory.
Files are stored in a linked chain of data blocks managed by the FAT.
Maximum file size is 16 MB.

c. File Descriptors:

Allows up to 32 concurrent open files.
Manages offsets and in-use status for efficient access.

d. Error Handling:

Extensive validation for inputs and operations.
Provides descriptive error messages for failed operations.

How to Use
   
a. Setup:

- Clone the repository and ensure all required files are present.
- Compile the code using gcc with flags -Wall and -Werror.

b. Running the File System:

- Create a File System: Run make_fs("disk_name") to initialize a new file system.
- Mount the File System: Run mount_fs("disk_name") to load the file system.
- Perform operations like creating, reading, writing, or deleting files using provided functions.
- Unmount the File System: Run umount_fs() to save changes and safely close the virtual disk.

c. Examples:
- Create a file: fs_create("example.txt")
- Write data: fs_write(fd, data, size)
- Read data: fs_read(fd, buffer, size)
- Delete a file: fs_delete("example.txt")

Current Limitations
- Does not yet support hierarchical directories (single root directory only).
- No journaling or recovery mechanisms are implemented.
- File permissions and advanced metadata are not supported.

This project is an educational exploration of file systems, contributions and feedback are welcome!
