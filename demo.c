#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include "disk.h"
#include "filesystem.h"

int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int umount_fs();
int fs_create(const char *filename);
int fs_open(const char *filename);
int fs_write(int fd, const void *buf, size_t count);
int fs_read(int fd, void *buf, size_t count);
int fs_close(int fd);
int fs_delete(const char *filename);
int fs_get_filesize(int fd);
int fs_lseek(int fd, size_t offset);
int fs_trunc(int fd, size_t size);

void print_directory() {
    printf("Directory Summary:\n");

    for (int i = 0; i < MAX_FILES; i++) {
        dir_entry_t *entry = &root_directory.entries[i];
        if (entry->filename[0] != '\0') {
            printf("  File: '%s', Size: %u bytes, Starting Cluster: %u\n",
                   entry->filename, entry->file_size, entry->starting_cluster);
        }
    }
}

void print_fat() {
    printf("FAT Summary:\n");
    if (fat == NULL) {
        printf("  FAT is not loaded in memory.\n");
        return;
    }

    int free_blocks = 0;
    int used_blocks = 0;

    for (uint32_t i = 0; i < superblock.data_blocks_count; i++) {
        if (fat[i] == FAT_FREE) {
            free_blocks++;
        } else {
            used_blocks++;
        }
    }

    printf("  Total Blocks: %d\n", superblock.data_blocks_count);
    printf("  Free Blocks: %d\n", free_blocks);
    printf("  Used Blocks: %d\n", used_blocks);
}

void *read_file_thread(void *arg) {
    const char *filename = (const char *)arg;

    int fd = fs_open(filename);
    if (fd < 0) {
        printf("Thread: fs_open: Failed to open '%s'.\n", filename);
        return NULL;
    }
    printf("Thread: fs_open: File '%s' opened successfully with descriptor %d.\n", filename, fd);

    char buf[64];
    int bytes_read = fs_read(fd, buf, sizeof(buf) - 1);
    if (bytes_read > 0) {
        buf[bytes_read] = '\0';  // Null-terminate the buffer
        printf("Thread: fs_read: Read %d bytes: '%s'\n", bytes_read, buf);
    }

    if (fs_close(fd) == 0) {
        printf("Thread: fs_close: File '%s' closed successfully.\n", filename);
    }

    return NULL;
}

int main() {
    char *disk_name = "virtual_disk";

    // Create and mount the file system
    if (make_fs(disk_name) != 0 || mount_fs(disk_name) != 0) {
        printf("Failed to initialize the file system.\n");
        return -1;
    }

    // Create and write to a file
    fs_create("testfile");
    int fd = fs_open("testfile");
    fs_write(fd, "Testing file system", 19);
    fs_close(fd);

    // Print directory and FAT summaries
    print_directory();
    print_fat();

    // Create a thread to read the file
    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, read_file_thread, (void *)"testfile");
    pthread_join(reader_thread, NULL);

    // Copy the file
    fs_create("copyfile");
    int copy_fd = fs_open("copyfile");
    fd = fs_open("testfile");
    char buf[64];
    int bytes_read;
    while ((bytes_read = fs_read(fd, buf, sizeof(buf))) > 0) {
        fs_write(copy_fd, buf, bytes_read);
    }
    fs_close(fd);
    fs_close(copy_fd);
    fs_delete("testfile");

    // Print directory and FAT summaries again
    print_directory();
    print_fat();

    // Unmount the file system
    umount_fs();
    return 0;
}
