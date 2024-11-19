#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "disk.h"

#define MAGIC_NUMBER 0xFADEBEEF
#define FAT_FREE 0xFFFE // indicates free block in the fat
#define FAT_EOF  0xFFFE // indicates the end of a file chain

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
    // Create virtual disk
    if (make_disk(disk_name) < 0) {
        fprintf(stderr, "make_fs: failed to create disk\n");
        return -1;
    }

    // Open virtual disk
    if (open_disk(disk_name) < 0) {
        fprintf(stderr, "make_fs: failed to open disk\n");
        return -1;
    }
    printf("make_fs: Disk opened successfully\n");

    // Initialize superblock
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

    // Write the superblock to block 0
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);                    // Ensure buffer is zeroed out
    memcpy(buf, &superblock, sizeof(superblock_t)); // Copy superblock into buffer

    if (block_write(0, buf) < 0) {
        fprintf(stderr, "make_fs: failed to write superblock\n");
        return -1;
    }

    // Initialize the FAT
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

    // Initialize the Root Directory
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

    // Close the disk
    if (close_disk() < 0) {
        fprintf(stderr, "make_fs: failed to close disk\n");
       return -1;
    }
    printf("make_fs: Disk closed successfully after initialization\n");

    return 0; // Success
}


int mount_fs(char *disk_name) {
   // Open the virtual disk
    if (open_disk(disk_name) < 0) {
        fprintf(stderr, "mount_fs: failed to open disk '%s'\n", disk_name);
        return -1;
    }
    printf("mount_fs: Disk '%s' opened successfully.\n", disk_name);

    // Read and verify the superblock
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

    // Load the FAT into memory
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
    

    
    // Load the root directory into memory
    if (block_read(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "mount_fs: failed to read root directory block\n");
        free(fat);
        close_disk();
        return -1;
    }

    memcpy(&root_directory, buf, sizeof(root_directory_t));

    printf("mount_fs: Root directory loaded successfully.\n");

    // Initialize the file descriptor table
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

    // Write the FAT back to disk (FAT 1)
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write FAT 1 to disk\n");
            return -1;
        }
    }

    // Write the duplicate FAT (FAT 2) back to disk
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat2_start_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write FAT 2 to disk\n");
            return -1;
        }
    }
    

    
    // Write the root directory back to disk
    printf("umount_fs: Writing root directory to disk...\n");
    for (uint32_t i = 0; i < superblock.root_dir_blocks; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, ((char *)&root_directory) + (i * BLOCK_SIZE), BLOCK_SIZE);
        if (block_write(superblock.root_dir_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write root directory to disk\n");
            return -1;
        }
    }
    

    // Close any open file descriptors
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_descriptors[i].in_use) {
            file_descriptors[i].in_use = 0;
            file_descriptors[i].file_index = -1;
            file_descriptors[i].offset = 0;
        }
    }

    // Free any dynamically allocated memory for FAT
    free(fat);
    fat = NULL;

    // Close the disk
    printf("umount_fs: Closing disk...\n");
    if (close_disk() < 0) {
        fprintf(stderr, "umount_fs: failed to close disk\n");
        return -1;
    }
    printf("umount_fs: Disk closed successfully.\n");

    return 0;  // Success
}

int fs_open(char *filename) {
    // Validate input filename
    if (!filename || strlen(filename) == 0) {
        fprintf(stderr, "fs_open: invalid filename\n");
        return -1;
    }

    // Search root directory for the file
    int file_index = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_directory.entries[i].filename[0] != '\0' && // valid entry
        strcmp(root_directory.entries[i].filename, filename) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index == -1) {
        fprintf(stderr, "fs_open: file '%s' not found\n", filename);
        return -1;
    }

    // Find an unused file descriptor
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if(!file_descriptors[i].in_use) { // unused descriptor
            fd = i;
            break;
        }
    }

    if (fd == -1) {
        fprintf(stderr, "fs_open: no available file descriptors\n");
        return -1;
    }

    // Initialize file descriptor
    file_descriptors[fd].file_index = file_index;
    file_descriptors[fd].offset = 0; // Start at beginning of file
    file_descriptors[fd].in_use = 1; // Mark as in use

    printf("fs_open: file '%s' opened successfully with descriptor %d\n", filename, fd);

    // Return the file descriptor index
    return fd;
}

int fs_close(int fd) {
    // Check if the file descriptor is within range
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        fprintf(stderr, "fs_close: invalid file descriptor %d\n", fd);
        return -1;
    }

    // Check if the file descriptor is in use
    if (!file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_close: file descriptor %d is not in use\n", fd);
        return -1;
    }

    // Mark the descriptor as unused
    file_descriptors[fd].in_use = 0;
    file_descriptors[fd].file_index = -1;
    file_descriptors[fd].offset = 0;

    printf("fs_close: file descriptor %d closed successfully\n", fd);
    return 0; // Success
}

int fs_create(char *filename) {
    if (!filename || strlen(filename) >= MAX_FILENAME_LENGTH) {
        fprintf(stderr, "fs_create: invalid filename\n");
        return -1;
    }

    // Check if a file with the same name already exists
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(root_directory.entries[i].filename, filename) == 0) {
            fprintf(stderr, "fs_create: file '%s' already exists\n", filename);
            return -1;
        }
    }

    // Find an available entry in the root directory
    int free_entry = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_directory.entries[i].starting_cluster == 0xFFFF) {
            free_entry = i;
            break;
        }
    }

    if (free_entry == -1) {
        fprintf(stderr, "fs_create: no available directory entries\n");
        return -1;
    }

    // Populate the new file entry
    dir_entry_t *entry = &root_directory.entries[free_entry];
    strncpy(entry->filename, filename, MAX_FILENAME_LENGTH - 1);
    entry->filename[MAX_FILENAME_LENGTH - 1] = '\0';  // Ensure null-terminated
    entry->attribute = 0;  // Default attribute
    entry->create_time = 0;  // Set creation time 
    entry->create_date = 0;  // Set creation date 
    entry->last_access_date = 0;
    entry->last_modified_time = 0;
    entry->last_modified_date = 0;
    entry->starting_cluster = FAT_EOF;  // No clusters allocated yet
    entry->file_size = 0;  // File is empty

    // Write the updated root directory back to disk
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &root_directory, sizeof(root_directory_t));
    if (block_write(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "fs_create: failed to write root directory to disk\n");
        return -1;
    }

    printf("fs_create: file '%s' created successfully\n", filename);
    return 0;  // Success
}

int fs_delete(char *filename) {
    if (!filename || strlen(filename) == 0) {
        fprintf(stderr, "fs_delete: invalid filename\n");
        return -1;
    }

    // Find the file in the root directory
    int file_index = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (strcmp(root_directory.entries[i].filename, filename) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index == -1) {
        fprintf(stderr, "fs_delete: file '%s' not found\n", filename);
        return -1;
    }

    // Free the FAT chain
    uint16_t cluster = root_directory.entries[file_index].starting_cluster;
    while (cluster != FAT_EOF && cluster != FAT_FREE) {
        uint16_t next_cluster = fat[cluster];
        fat[cluster] = FAT_FREE;  // Mark the current cluster as free
        cluster = next_cluster;  // Move to the next cluster
    }

    // Clear the file entry in the root directory
    memset(&root_directory.entries[file_index], 0, sizeof(dir_entry_t));
    root_directory.entries[file_index].starting_cluster = 0xFFFF;  // Mark as unused

    // Write the updated root directory back to disk
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &root_directory, sizeof(root_directory_t));
    if (block_write(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "fs_delete: failed to write root directory to disk\n");
        return -1;
    }

    // Write the updated FAT back to disk
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "fs_delete: failed to write FAT to disk\n");
            return -1;
        }
    }

    printf("fs_delete: file '%s' deleted successfully\n", filename);
    return 0;  // Success
}

int fs_read(int fd, void *buf, size_t count) {
    // Validate inputs
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_read: invalid file descriptor %d\n", fd);
        return -1;
    }
    if (!buf || count == 0) {
        fprintf(stderr, "fs_read: invalid buffer or count\n");
        return -1;
    }

    // Locate the file and check the offset
    file_descriptor_t *descriptor = &file_descriptors[fd];
    dir_entry_t *file_entry = &root_directory.entries[descriptor->file_index];
    if (descriptor->offset >= file_entry->file_size) {
        fprintf(stderr, "fs_read: offset beyond end of file\n");
        return 0;  // No more data to read
    }

    // Adjust count to not exceed file size
    size_t bytes_to_read = count;
    if (descriptor->offset + count > file_entry->file_size) {
        bytes_to_read = file_entry->file_size - descriptor->offset;
    }

    // Traverse the FAT to locate the starting cluster
    uint16_t cluster = file_entry->starting_cluster;
    size_t cluster_size = BLOCK_SIZE;
    size_t cluster_offset = descriptor->offset / cluster_size;
    size_t intra_cluster_offset = descriptor->offset % cluster_size;

    // Move to the correct cluster based on offset
    for (size_t i = 0; i < cluster_offset; i++) {
        if (cluster == FAT_EOF || fat[cluster] == FAT_FREE) {
            fprintf(stderr, "fs_read: corrupted FAT chain\n");
            return -1;
        }
        cluster = fat[cluster];
    }

    // Read data from the file
    char temp_buf[BLOCK_SIZE];
    size_t bytes_read = 0;

    while (bytes_to_read > 0) {
        if (block_read(superblock.data_start_block + cluster, temp_buf) < 0) {
            fprintf(stderr, "fs_read: failed to read block %d\n", cluster);
            return -1;
        }

        size_t bytes_in_cluster = cluster_size - intra_cluster_offset;
        size_t bytes_to_copy = (bytes_to_read < bytes_in_cluster) ? bytes_to_read : bytes_in_cluster;

        memcpy((char *)buf + bytes_read, temp_buf + intra_cluster_offset, bytes_to_copy);

        bytes_read += bytes_to_copy;
        bytes_to_read -= bytes_to_copy;

        intra_cluster_offset = 0;  // Only the first cluster read has an offset

        // Move to the next cluster if necessary
        if (bytes_to_read > 0) {
            if (fat[cluster] == FAT_EOF) {
                break;  // No more clusters in the chain
            }
            cluster = fat[cluster];
        }
    }

    // Update the file descriptor offset
    descriptor->offset += bytes_read;

    return bytes_read;  // Return the number of bytes read
}

int fs_write(int fd, const void *buf, size_t count) {
    // Validate inputs
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_write: invalid file descriptor %d\n", fd);
        return -1;
    }
    if (!buf || count == 0) {
        fprintf(stderr, "fs_write: invalid buffer or count\n");
        return -1;
    }

    // Locate the file
    file_descriptor_t *descriptor = &file_descriptors[fd];
    dir_entry_t *file_entry = &root_directory.entries[descriptor->file_index];
    uint32_t cluster = file_entry->starting_cluster;

    // Allocate first cluster if the file is empty
    if (cluster == FAT_FREE) {
        for (uint32_t i = 0; i < superblock.data_blocks_count; i++) {
            if (fat[i] == FAT_FREE) {
                fat[i] = FAT_EOF;
                file_entry->starting_cluster = i;
                cluster = i;
                break;
            }
        }
        if (cluster == FAT_FREE) {
            fprintf(stderr, "fs_write: no free clusters available\n");
            return -1;
        }
    }

    // Traverse FAT to locate the current cluster
    size_t cluster_size = BLOCK_SIZE;
    size_t cluster_offset = descriptor->offset / cluster_size;
    size_t intra_cluster_offset = descriptor->offset % cluster_size;

    for (size_t i = 0; i < cluster_offset; i++) {
        if (fat[cluster] == FAT_EOF) {
            // Allocate a new cluster if needed
            for (uint32_t j = 0; j < superblock.data_blocks_count; j++) {
                if (fat[j] == FAT_FREE) {
                    fat[cluster] = j;
                    fat[j] = FAT_EOF;
                    cluster = j;
                    break;
                }
            }
            if (fat[cluster] == FAT_EOF) {
                fprintf(stderr, "fs_write: no free clusters available\n");
                return -1;
            }
        } else {
            cluster = fat[cluster];
        }
    }

    // Write data to the file
    char temp_buf[BLOCK_SIZE];
    size_t bytes_written = 0;

    while (count > 0) {
        // Read the cluster into a buffer to preserve existing data
        if (block_read(superblock.data_start_block + cluster, temp_buf) < 0) {
            fprintf(stderr, "fs_write: failed to read cluster %d\n", cluster);
            return -1;
        }

        size_t bytes_in_cluster = cluster_size - intra_cluster_offset;
        size_t bytes_to_copy = (count < bytes_in_cluster) ? count : bytes_in_cluster;

        // Write new data into the buffer
        memcpy(temp_buf + intra_cluster_offset, (char *)buf + bytes_written, bytes_to_copy);

        // Write the updated buffer back to the cluster
        if (block_write(superblock.data_start_block + cluster, temp_buf) < 0) {
            fprintf(stderr, "fs_write: failed to write cluster %d\n", cluster);
            return -1;
        }

        bytes_written += bytes_to_copy;
        count -= bytes_to_copy;
        intra_cluster_offset = 0;  // Only the first cluster write has an offset

        // Move to the next cluster if necessary
        if (count > 0) {
            if (fat[cluster] == FAT_EOF) {
                // Allocate a new cluster
                for (uint32_t j = 0; j < superblock.data_blocks_count; j++) {
                    if (fat[j] == FAT_FREE) {
                        fat[cluster] = j;
                        fat[j] = FAT_EOF;
                        cluster = j;
                        break;
                    }
                }
                if (fat[cluster] == FAT_EOF) {
                    fprintf(stderr, "fs_write: no free clusters available\n");
                    return -1;
                }
            } else {
                cluster = fat[cluster];
            }
        }
    }

    // Update file size and descriptor offset
    descriptor->offset += bytes_written;
    if (descriptor->offset > file_entry->file_size) {
        file_entry->file_size = descriptor->offset;
    }

    return bytes_written;  // Return the number of bytes written
}

int fs_get_filesize(int fd) {
    // Validate file descriptor
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_get_filesize: invalid file descriptor %d\n", fd);
        return -1;
    }

    // Access the file's metadata
    file_descriptor_t *descriptor = &file_descriptors[fd];
    dir_entry_t *file_entry = &root_directory.entries[descriptor->file_index];

    // Return the file size
    return file_entry->file_size;
}

int fs_lseek(int fd, size_t offset) {
    // Validate the file descriptor
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_lseek: invalid file descriptor %d\n", fd);
        return -1;
    }

    // Validate the offset
    file_descriptor_t *descriptor = &file_descriptors[fd];
    dir_entry_t *file_entry = &root_directory.entries[descriptor->file_index];
    if (offset > file_entry->file_size) {
        fprintf(stderr, "fs_lseek: offset %zu exceeds file size %u\n", offset, file_entry->file_size);
        return -1;
    }

    // Update the file descriptor's offset
    descriptor->offset = offset;
    printf("fs_lseek: file descriptor %d moved to offset %zu\n", fd, offset);

    return 0;  // Success
}

int fs_trunc(int fd, size_t new_size) {
    // Validate the file descriptor
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_trunc: invalid file descriptor %d\n", fd);
        return -1;
    }

    // Access the file's metadata
    file_descriptor_t *descriptor = &file_descriptors[fd];
    dir_entry_t *file_entry = &root_directory.entries[descriptor->file_index];

    // Validate the new size
    if (new_size > file_entry->file_size) {
        fprintf(stderr, "fs_trunc: new size %zu is greater than file size %u\n", new_size, file_entry->file_size);
        return -1;
    }

    // Calculate the required number of clusters
    size_t current_clusters = (file_entry->file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t new_clusters = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // Truncate the FAT chain if needed
    uint16_t cluster = file_entry->starting_cluster;
    uint16_t prev_cluster = FAT_EOF;
    for (size_t i = 0; i < new_clusters; i++) {
        prev_cluster = cluster;
        cluster = fat[cluster];
    }

    if (new_clusters < current_clusters) {
        // Free unused clusters
        while (cluster != FAT_EOF && cluster != FAT_FREE) {
            uint16_t next_cluster = fat[cluster];
            fat[cluster] = FAT_FREE; // Mark as free
            cluster = next_cluster;
        }

        // Terminate the FAT chain
        fat[prev_cluster] = FAT_EOF;
    }

    // Update file size
    file_entry->file_size = new_size;

    printf("fs_trunc: file descriptor %d truncated to %zu bytes\n", fd, new_size);
    return 0; // Success
}



int main() {
    char *disk_name = "virtual_disk";

    // Create and mount the file system
    if (make_fs(disk_name) == 0) {
        printf("File system created successfully.\n");
    } else {
        printf("Failed to create file system.\n");
        return -1;
    }
    if (mount_fs(disk_name) == 0) {
        printf("File system mounted successfully.\n");
    } else {
        printf("Failed to mount file system.\n");
        return -1;
    }

    // fs_create test: Create a file
    if (fs_create("testfile") == 0) {
        printf("fs_create: 'testfile' created successfully.\n");
    }

    // fs_open test: Open the file
    int fd = fs_open("testfile");
    if (fd >= 0) {
        printf("fs_open: 'testfile' opened successfully with descriptor %d.\n", fd);
    }

    // fs_write: Write data to the file
    const char *data = "Testing file system";
    int bytes_written = fs_write(fd, data, strlen(data));
    if (bytes_written > 0) {
        printf("fs_write: Wrote %d bytes to 'testfile'.\n", bytes_written);
    }

    // fs_get_filesize test: Get the file size
    int filesize = fs_get_filesize(fd);
    if (filesize >= 0) {
        printf("fs_get_filesize: File size of 'testfile' is %d bytes.\n", filesize);
    } else {
        printf("fs_get_filesize: Failed to get file size for 'testfile'.\n");
    }

    // fs_lseek test: Move the offset
    if (fs_lseek(fd, 7) == 0) {
        printf("fs_lseek: Successfully moved offset to 7.\n");
    }

    // fs_lseek test: Read data from the new offset
    char buf[64];
    int bytes_read = fs_read(fd, buf, sizeof(buf) - 1);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0'; // Null-terminate the buffer
        printf("fs_read: Read %d bytes from offset 7: '%s'\n", bytes_read, buf);
    }

    // fs_trunc test: Truncate the file
    if (fs_trunc(fd, 10) == 0) {
        printf("fs_trunc: Successfully truncated 'testfile' to 10 bytes.\n");
    }

    // fs_trunc test: Get the file size after truncation
    filesize = fs_get_filesize(fd);
    if (filesize >= 0) {
        printf("fs_get_filesize: File size of 'testfile' after truncation is %d bytes.\n", filesize);
    }

    // fs_trunc test: Read data after truncation
    fs_lseek(fd, 0); // Move offset back to start
    bytes_read = fs_read(fd, buf, sizeof(buf) - 1);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0'; // Null terminate buffer
        printf("fs_read: Read %d bytes after truncation: '%s'\n", bytes_read, buf);
    }

    // fs_close test: Close the file
    if (fs_close(fd) == 0) {
        printf("fs_close: 'testfile' closed successfully.\n");
    }

    // fs_delete test: Delete the file
    if (fs_delete("testfile") == 0) {
        printf("fs_delete: 'testfile' deleted successfully.\n");
    }

    // Attempting to open a deleted file
    fd = fs_open("testfile");
    if (fd < 0) {
        printf("fs_open: As expected, failed to open deleted 'testfile'.\n");
    }

    // Unmount the file system
    if (umount_fs() == 0) {
        printf("File system unmounted successfully.\n");
    } else {
        printf("Failed to unmount file system.\n");
    }
    
    return 0;
}