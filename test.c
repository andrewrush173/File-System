#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "disk.h"

#define MAGIC_NUMBER 0xFADEBEEF
#define FAT_FREE 0xFFFFFFFF // indicates free block in the fat
#define FAT_EOF  0xFFFFFFFE // indicates the end of a file chain

#define MAX_FILENAME_LENGTH 15
#define MAX_FILES 64
#define MAX_OPEN_FILES 32

typedef struct {
    int file_index;     // Index of the file in the root directory
    uint32_t offset;    // Current offset within the file
    uint8_t in_use;     // Flag indicating if this descriptor is in use
} file_descriptor_t;


file_descriptor_t file_descriptors[MAX_OPEN_FILES];


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

// Structure of the superblock
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

// Global variables to store the file system structures in memory
superblock_t superblock;
uint32_t *fat = NULL;  // Dynamic array for FAT
root_directory_t root_directory;
file_descriptor_t file_descriptors[MAX_OPEN_FILES];

int make_fs(char *disk_name) {
    // Step 1: Create virtual disk
    if (make_disk(disk_name) < 0) {
        fprintf(stderr, "make_fs: failed to create disk\n");
        return -1;
    }

    // Step 2: Open virtual disk
    if (open_disk(disk_name) < 0) {
        fprintf(stderr, "make_fs: failed to open disk\n");
        return -1;
    }
    printf("make_fs: Disk opened successfully\n");

    // Step 3: Initialize superblock
    superblock.magic = MAGIC_NUMBER;
    superblock.total_blocks = DISK_BLOCKS;
    superblock.block_size = BLOCK_SIZE;
    superblock.fat1_start_block = 1; // FAT 1 starts at block 1
    superblock.fat_blocks_count = 4096 * sizeof(uint32_t) / BLOCK_SIZE;
    superblock.fat2_start_block = superblock.fat1_start_block + superblock.fat_blocks_count; // FAT 2 starts after FAT 1
    superblock.root_dir_block = superblock.fat2_start_block + superblock.fat_blocks_count;
    superblock.root_dir_blocks = 1; // Assume 1 block for root directory
    superblock.data_start_block = 4096;
    superblock.data_blocks_count = 4096;
    superblock.free_blocks_count = 4096;

    // Step 4: Write the superblock to block 0
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);                    // Ensure buffer is zeroed out
    memcpy(buf, &superblock, sizeof(superblock_t)); // Copy superblock into buffer

    if (block_write(0, buf) < 0) {
        fprintf(stderr, "make_fs: failed to write superblock\n");
        return -1;
    }

    // Step 5: Initialize the FAT
    fat = (uint32_t*)malloc(4096 * sizeof(uint32_t));
    if (!fat) {
        fprintf(stderr, "make_fs: failed to allocate memory for FAT\n");
        return -1;
    }
    for (uint32_t i = 0; i < 4096; i++) {
        fat[i] = FAT_FREE; // Initialize each entry as a free block
    }

    // Write FAT 1 to the reserved FAT blocks on disk
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "make_fs: failed to write FAT 1 to disk\n");
            free(fat);
            return -1;
        }
    }

    // Write FAT 2 (duplicate of FAT 1)
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat2_start_block + i, buf) < 0) {
            fprintf(stderr, "make_fs: failed to write FAT 2 to disk\n");
            free(fat);
            return -1;
        }
    }

    // Step 6: Initialize the Root Directory
    root_directory_t root_dir;
    for (int i = 0; i < MAX_FILES; i++) {
        memset(root_dir.entries[i].filename, 0, MAX_FILENAME_LENGTH); // Empty filename
        root_dir.entries[i].attribute = 0; // Default attribute
        root_dir.entries[i].create_time = 0;
        root_dir.entries[i].create_date = 0;
        root_dir.entries[i].last_access_date = 0;
        root_dir.entries[i].last_modified_time = 0;
        root_dir.entries[i].last_modified_date = 0;
        root_dir.entries[i].starting_cluster = 0xFFFF; // No starting cluster
        root_dir.entries[i].file_size = 0; // Size 0
    }

    // Write the root directory to its designated blocks
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &root_dir, sizeof(root_directory_t));
    if (block_write(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "make_fs: failed to write root directory to disk\n");
        return -1;
    }

    // Step 7: Close the disk
    if (close_disk() < 0) {
        fprintf(stderr, "make_fs: failed to close disk\n");
       return -1;
    }
    printf("make_fs: Disk closed successfully after initialization\n");

    return 0; // Success
}


int mount_fs(char *disk_name) {
   // Step 1: Open the virtual disk
    if (open_disk(disk_name) < 0) {
        fprintf(stderr, "mount_fs: failed to open disk '%s'\n", disk_name);
        return -1;
    }
    printf("mount_fs: Disk '%s' opened successfully.\n", disk_name);

    // Step 2: Read and verify the superblock
    char buf[BLOCK_SIZE];
    if (block_read(0, buf) < 0) {
        fprintf(stderr, "mount_fs: failed to read superblock\n");
        close_disk();
        return -1;
    }
    memcpy(&superblock, buf, sizeof(superblock_t));

    if (superblock.magic != MAGIC_NUMBER) {
        fprintf(stderr, "mount_fs: invalid magic number\n");
        close_disk();
        return -1;
    }
    printf("mount_fs: Superblock loaded successfully.\n");

    // Step 3: Load the FAT into memory
    fat = (uint32_t *)malloc(superblock.data_blocks_count * sizeof(uint32_t));
    if (!fat) {
        fprintf(stderr, "mount_fs: failed to allocate memory for FAT\n");
        close_disk();
        return -1;
    }

    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        if (block_read(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "mount_fs: failed to read FAT block\n");
            free(fat);
            close_disk();
            return -1;
        }
        memcpy(fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), buf, BLOCK_SIZE);
    }
    printf("mount_fs: FAT loaded successfully.\n");
    

    
    // Step 4: Load the root directory into memory
    if (block_read(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "mount_fs: failed to read root directory block\n");
        free(fat);
        close_disk();
        return -1;
    }

    memcpy(&root_directory, buf, sizeof(root_directory_t));

    printf("mount_fs: Root directory loaded successfully.\n");

    // Step 5: Initialize the file descriptor table
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        file_descriptors[i].in_use = 0;
        file_descriptors[i].file_index = -1;
        file_descriptors[i].offset = 0;
    }
    printf("mount_fs: File descriptor table initialized.\n");

    return 0;  // Success
}

int umount_fs() {
    char buf[BLOCK_SIZE];  // Temporary buffer for block writes

    printf("umount_fs: Writing FAT to disk...\n");

    // Step 1: Write the FAT back to disk (FAT 1)
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write FAT 1 to disk\n");
            return -1;
        }
    }

    // Step 2: Write the duplicate FAT (FAT 2) back to disk
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat2_start_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write FAT 2 to disk\n");
            return -1;
        }
    }
    

    
    // Step 3: Write the root directory back to disk
    printf("umount_fs: Writing root directory to disk...\n");
    for (uint32_t i = 0; i < superblock.root_dir_blocks; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, ((char *)&root_directory) + (i * BLOCK_SIZE), BLOCK_SIZE);
        if (block_write(superblock.root_dir_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write root directory to disk\n");
            return -1;
        }
    }
    

    // Step 4: Close any open file descriptors
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_descriptors[i].in_use) {
            file_descriptors[i].in_use = 0;
            file_descriptors[i].file_index = -1;
            file_descriptors[i].offset = 0;
        }
    }

    // Step 5: Free any dynamically allocated memory for FAT
    free(fat);
    fat = NULL;

    // Step 6: Close the disk
    printf("umount_fs: Closing disk...\n");
    if (close_disk() < 0) {
        fprintf(stderr, "umount_fs: failed to close disk\n");
        return -1;
    }
    printf("umount_fs: Disk closed successfully.\n");

    return 0;  // Success
}


int main() {
    char *disk_name = "virtual_disk";

    // 1. Create file system
    if (make_fs(disk_name) == 0) {
        printf("File system created successfully on '%s'.\n", disk_name);
    } else {
        printf("Failed to create file system on '%s'.\n", disk_name);
        return -1;
    }

    // 2. Mount the file system
    if (mount_fs(disk_name) == 0) {
        printf("File system mounted successfully from '%s'.\n", disk_name);
    } else {
        printf("Failed to mount file system from '%s'.\n", disk_name);
        return -1;
    }

    // 3. Unmount the file system
    if (umount_fs() == 0) {
        printf("File system unmounted successfully.\n");
    } else {
        printf("Failed to unmount file system.\n");
        return -1;
    }

    return 0;
}