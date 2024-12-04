#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "filesystem.h"
#include "disk.h"

// Global variables to store file system strctures in memory
superblock_t superblock;
uint32_t *fat = NULL;  
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
    superblock.fat_blocks_count = (DISK_BLOCKS * sizeof(uint32_t) + BLOCK_SIZE - 1) / BLOCK_SIZE; // Number of blocks for FAT
    superblock.fat2_start_block = superblock.fat1_start_block + superblock.fat_blocks_count; // FAT 2 starts after FAT 1
    superblock.root_dir_block = superblock.fat2_start_block + superblock.fat_blocks_count; // Root directory starts after FAT 2
    superblock.root_dir_blocks = 1; // Assume 1 block for the root directory
    superblock.data_start_block = superblock.root_dir_block + superblock.root_dir_blocks; // Data blocks start after root directory
    superblock.data_blocks_count = DISK_BLOCKS - superblock.data_start_block; // Total blocks minus metadata
    superblock.free_blocks_count = superblock.data_blocks_count; // Initially all data blocks are free

    // Write the superblock to block 0
    char buf[BLOCK_SIZE];
    memset(buf, 0, BLOCK_SIZE);                    //  buffer is zeroed out
    memcpy(buf, &superblock, sizeof(superblock_t)); // Copy superblock into buffer

    if (block_write(0, buf) < 0) {
        fprintf(stderr, "make_fs: failed to write superblock\n");
        close_disk();
        return -1;
    }

    // Initialize the FAT
    fat = (uint32_t *)malloc(superblock.data_blocks_count * sizeof(uint32_t));
    if (!fat) {
        fprintf(stderr, "make_fs: failed to allocate memory for FAT\n");
        close_disk();
        return -1;
    }

    // Set all FAT entries to free
    for (uint32_t i = 0; i < superblock.data_blocks_count; i++) {
        fat[i] = FAT_FREE;
    }

    // Write FAT 1 to the reserved FAT blocks on disk
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "make_fs: failed to write FAT 1 to disk\n");
            free(fat);
            close_disk();
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
            close_disk();
            return -1;
        }
    }

    // Initialize the root Directory
    memset(&root_directory, 0, sizeof(root_directory_t));

    // Write the root directory to its designated blocks
    memset(buf, 0, BLOCK_SIZE);
    memcpy(buf, &root_directory, sizeof(root_directory_t));
    if (block_write(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "make_fs: failed to write root directory to disk\n");
        free(fat);
        close_disk();
        return -1;
    }

    // Close the disk
    if (close_disk() < 0) {
        fprintf(stderr, "make_fs: failed to close disk\n");
        free(fat);
        return -1;
    }
    printf("make_fs: Disk closed successfully after initialization\n");

    free(fat); // Free FAT memory allocation
    fat = NULL;

    return 0; // Success
}

int mount_fs(char *disk_name) {
    if (!disk_name) {
        fprintf(stderr, "mount_fs: Invalid disk name.\n");
        return -1;
    }

    // Open the virtual disk
    if (open_disk(disk_name) < 0) {
        fprintf(stderr, "mount_fs: Failed to open disk '%s'.\n", disk_name);
        return -1;
    }
    printf("mount_fs: Disk '%s' opened successfully.\n", disk_name);

    // Read and verify the superblock
    char buf[BLOCK_SIZE];
    if (block_read(0, buf) < 0) {
        fprintf(stderr, "mount_fs: Failed to read superblock.\n");
        close_disk();
        return -1;
    }

    memcpy(&superblock, buf, sizeof(superblock_t));
    if (superblock.magic != MAGIC_NUMBER) {
        fprintf(stderr, "mount_fs: Invalid magic number in superblock.\n");
        close_disk();
        return -1;
    }
    printf("mount_fs: Superblock loaded successfully.\n");

    // Allocate memory for the FAT
    fat = (uint32_t *)malloc(superblock.data_blocks_count * sizeof(uint32_t));
    if (!fat) {
        fprintf(stderr, "mount_fs: Failed to allocate memory for FAT.\n");
        close_disk();
        return -1;
    }

    // Load FAT1 into memory
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        if (block_read(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "mount_fs: Failed to read FAT block %u.\n", i);
            free(fat);
            close_disk();
            return -1;
        }
        memcpy(fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), buf, BLOCK_SIZE);
    }
    printf("mount_fs: FAT loaded successfully.\n");

    // Load the root directory
    if (block_read(superblock.root_dir_block, buf) < 0) {
        fprintf(stderr, "mount_fs: Failed to read root directory block.\n");
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

    // Ensure FAT is allocated
    if (fat == NULL) {
        fprintf(stderr, "umount_fs: FAT is not loaded in memory. Cannot unmount.\n");
        return -1;
    }

    // Write the FAT back to disk (FAT 1)
    printf("umount_fs: Writing FAT to disk...\n");
    for (uint32_t i = 0; i < superblock.fat_blocks_count; i++) {
        memset(buf, 0, BLOCK_SIZE);
        memcpy(buf, fat + (i * (BLOCK_SIZE / sizeof(uint32_t))), BLOCK_SIZE);
        if (block_write(superblock.fat1_start_block + i, buf) < 0) {
            fprintf(stderr, "umount_fs: failed to write FAT 1 to disk\n");
            return -1;
        }
    }

    // Write the duplicate FAT (FAT 2)
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

    // Free any dynamically allocated memory for FAT
    free(fat);
    fat = NULL;

    // Close any open file descriptors
    printf("umount_fs: Closing all file descriptors...\n");
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (file_descriptors[i].in_use) {
            file_descriptors[i].in_use = 0;
            file_descriptors[i].file_index = -1;
            file_descriptors[i].offset = 0;
        }
    }

    // Close the disk
    printf("umount_fs: Closing disk...\n");
    if (close_disk() < 0) {
        fprintf(stderr, "umount_fs: failed to close disk\n");
        return -1;
    }

    printf("umount_fs: Disk unmounted and closed successfully.\n");
    return 0;  // Success
}


int fs_open(const char *filename) {
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

int fs_create(const char *filename) {
    // Check for invalid filename
    if (!filename || strlen(filename) == 0 || strlen(filename) >= MAX_FILENAME_LENGTH) {
        fprintf(stderr, "fs_create: Invalid filename\n");
        return -1;
    }

    // Check if the file already exists
    for (int i = 0; i < MAX_FILES; i++) {
        dir_entry_t *entry = &root_directory.entries[i];
        if (entry->filename[0] != '\0' && strcmp(entry->filename, filename) == 0) {
            fprintf(stderr, "fs_create: File '%s' already exists\n", filename);
            return -1;
        }
    }

    // Find an empty directory entry
    for (int i = 0; i < MAX_FILES; i++) {
        dir_entry_t *entry = &root_directory.entries[i];
        if (entry->filename[0] == '\0') {  // Empty entry found
            // Initialize directory entry
            strncpy(entry->filename, filename, MAX_FILENAME_LENGTH);
            entry->filename[MAX_FILENAME_LENGTH - 1] = '\0';
            entry->file_size = 0;

            // Find a free cluster for the starting cluster
            uint32_t starting_cluster = FAT_FREE;
            for (uint32_t j = 0; j < superblock.data_blocks_count; j++) {
                if (fat[j] == FAT_FREE) {
                    fat[j] = FAT_EOF;  // Mark the cluster as EOF
                    starting_cluster = j;
                    superblock.free_blocks_count--;
                    break;
                }
            }

            // If no free cluster is found, return  error
            if (starting_cluster == FAT_FREE) {
                fprintf(stderr, "fs_create: No free clusters available\n");
                return -1;
            }

            // Update directory entry
            entry->starting_cluster = starting_cluster;

            printf("fs_create: File '%s' created successfully\n", filename);
            return 0;  // Success
        }
    }

    // If no free directory entry is found return an error
    fprintf(stderr, "fs_create: No free directory entries available\n");
    return -1;
}

int fs_delete(const char *filename) {
    if (!filename || strlen(filename) == 0) {
        fprintf(stderr, "fs_delete: Invalid filename provided.\n");
        return -1;
    }

    // Locate the file in the root directory
    int file_index = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (root_directory.entries[i].filename[0] != '\0' &&
            strcmp(root_directory.entries[i].filename, filename) == 0) {
            file_index = i;
            break;
        }
    }

    if (file_index == -1) {
        fprintf(stderr, "fs_delete: File '%s' not found.\n", filename);
        return -1;
    }

    // Start freeing clusters in the FAT chain
    uint32_t current_cluster = root_directory.entries[file_index].starting_cluster;
    printf("fs_delete: Deleting file '%s', starting at cluster %u.\n", filename, current_cluster);

    while (current_cluster != FAT_EOF) {
        uint32_t next_cluster = fat[current_cluster];

        fat[current_cluster] = FAT_FREE;  // Mark the cluster as free
        superblock.free_blocks_count++;  // Increment the free block count
        current_cluster = next_cluster;
    }

    // Clear the root directory entry
    memset(&root_directory.entries[file_index], 0, sizeof(dir_entry_t));
    printf("fs_delete: File '%s' successfully deleted.\n", filename);

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
    if (fd < 0 || fd >= MAX_OPEN_FILES || !file_descriptors[fd].in_use) {
        fprintf(stderr, "fs_write: Invalid file descriptor.\n");
        return -1;
    }

    if (!buf || count == 0) {
        fprintf(stderr, "fs_write: Invalid buffer or count.\n");
        return -1;
    }

    int file_index = file_descriptors[fd].file_index;
    uint32_t offset = file_descriptors[fd].offset;
    const char *buffer = (const char *)buf;
    size_t bytes_written = 0;

    printf("fs_write: Starting write for file '%s', offset %u, count %zu.\n",
           root_directory.entries[file_index].filename, offset, count);

    // Determine the starting cluster
    uint32_t current_cluster = root_directory.entries[file_index].starting_cluster;
    if (current_cluster == FAT_FREE) {
        current_cluster = find_free_block();
        if (current_cluster == (uint32_t)-1) {
            fprintf(stderr, "fs_write: No free blocks available.\n");
            return -1;
        }
        root_directory.entries[file_index].starting_cluster = current_cluster;
        fat[current_cluster] = FAT_EOF;
        superblock.free_blocks_count--;  // Decrement free block count
    }

    // Traverse clusters to reach the write offset
    while (offset >= BLOCK_SIZE) {
        if (fat[current_cluster] == FAT_EOF) {
            uint32_t new_block = find_free_block();
            if (new_block == (uint32_t)-1) {
                fprintf(stderr, "fs_write: No free blocks available.\n");
                return -1;
            }
            fat[current_cluster] = new_block;
            fat[new_block] = FAT_EOF;
            superblock.free_blocks_count--;  // Decrement free block count
        }
        current_cluster = fat[current_cluster];
        offset -= BLOCK_SIZE;
    }

    // Write data into clusters
    while (bytes_written < count) {
        char block_buf[BLOCK_SIZE];
        if (block_read(superblock.data_start_block + current_cluster, block_buf) < 0) {
            fprintf(stderr, "fs_write: Failed to read block %u.\n", current_cluster);
            return -1;
        }

        size_t write_size = BLOCK_SIZE - offset;
        if (write_size > count - bytes_written) {
            write_size = count - bytes_written;
        }

        memcpy(block_buf + offset, buffer + bytes_written, write_size);

        if (block_write(superblock.data_start_block + current_cluster, block_buf) < 0) {
            fprintf(stderr, "fs_write: Failed to write block %u.\n", current_cluster);
            return -1;
        }

        bytes_written += write_size;
        offset = 0;

        if (bytes_written < count) {
            if (fat[current_cluster] == FAT_EOF) {
                uint32_t new_block = find_free_block();
                if (new_block == (uint32_t)-1) {
                    fprintf(stderr, "fs_write: No free blocks available.\n");
                    return -1;
                }
                fat[current_cluster] = new_block;
                fat[new_block] = FAT_EOF;
                superblock.free_blocks_count--;  // Decrement free block count
            }
            current_cluster = fat[current_cluster];
        }
    }

    // Update the file descriptor offset
    file_descriptors[fd].offset += bytes_written;

    // Update the file size if the offset extends it
    if (file_descriptors[fd].offset > root_directory.entries[file_index].file_size) {
        root_directory.entries[file_index].file_size = file_descriptors[fd].offset;
    }

    printf("fs_write: Completed write for file '%s', total bytes written: %zu.\n",
           root_directory.entries[file_index].filename, bytes_written);

    return bytes_written;
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

uint32_t find_free_block() {
    for (uint32_t i = 0; i < superblock.data_blocks_count; i++) {
        if (fat[i] == FAT_FREE) {
            printf("find_free_block: Found free block %d.\n", i);
            return i;
        }
    }
    printf("find_free_block: No free blocks available.\n");
    return (uint32_t)-1; // Return -1 if no free block is available
}


