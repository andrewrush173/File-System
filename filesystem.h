#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdint.h>
#include <stddef.h>

// Constants
#define MAGIC_NUMBER 0xFADEBEEF  // Magic number for file system validation
#define FAT_FREE 0xFFFF          // Indicates a free block in the FAT
#define FAT_EOF  0xFFFE          // Indicates the end of a file chain
#define MAX_FILENAME_LENGTH 15   // Maximum length for file names
#define MAX_FILES 64             // Maximum number of files
#define MAX_OPEN_FILES 32        // Maximum number of open files

// Structures
typedef struct {
    int file_index;     // Index of the file in the root directory
    uint32_t offset;    // Current offset within the file
    uint8_t in_use;     // flag indicating if this descriptor is in use
} file_descriptor_t;

typedef struct {
    char filename[MAX_FILENAME_LENGTH]; // 15 bytes for file name
    uint8_t attribute;                  // 1 byte for file attribute

    uint16_t create_time;               // 2 bytes for create time
    uint16_t create_date;               // 2 bytes for create date
    uint16_t last_access_date;          // 2 bytes for last access date
    uint16_t last_modified_time;        // 2 bytes for last modified time
    uint16_t last_modified_date;        // 2 bytes for last modified date

    uint16_t starting_cluster;          // 2 bytes for starting cluster number
    uint32_t file_size;                 // 4 bytes for file size
} dir_entry_t;

typedef struct {
    dir_entry_t entries[MAX_FILES];
} root_directory_t;

typedef struct {
    uint32_t magic;             // Magic number for file system identification
    uint32_t total_blocks;      // Total number of blocks
    uint32_t block_size;        // Size of each block in bytes

    uint32_t fat1_start_block;   // Starting block of FAT 1 
    uint32_t fat_blocks_count;  // Number of blocks reserved for each FAT

    uint32_t fat2_start_block;  // Starting block of FAT 2 (duplicate)
    uint32_t root_dir_block;    // Start block of the root directory
    uint32_t root_dir_blocks;   // Number of blocks reserved for the root directory

    uint32_t data_start_block;  // Starting block of data blocks
    uint32_t data_blocks_count; // Number of blocks reserved for data

    uint32_t free_blocks_count; // Count of free data blocks
} superblock_t;

// Extern declarations for global variables
extern superblock_t superblock;
extern uint32_t *fat;
extern root_directory_t root_directory;
extern file_descriptor_t file_descriptors[MAX_OPEN_FILES];

// Function prototypes
int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int umount_fs();

int fs_create(const char *filename);
int fs_open(const char *filename);
int fs_close(int fd);
int fs_read(int fd, void *buf, size_t count);
int fs_write(int fd, const void *buf, size_t count);
int fs_delete(const char *filename);
int fs_get_filesize(int fd);
int fs_lseek(int fd, size_t offset);
int fs_trunc(int fd, size_t length);

uint32_t find_free_block();

#endif
