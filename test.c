#include "virt_disk.h"
#include <stdio.h>

int main() {
    printf("=== Virtual Disk Filesystem Formatter ===\n\n");
    
    if (format_fs() == 0) {
        printf("\nFormat successful!\n\n");
        
        if (load_fs() == 0) {
            fs_info();
        } else {
            printf("Error loading filesystem after format\n");
        }
    } else {
        printf("Format failed!\n");
        return 1;
    }
    int ino = allocate_inode();
    if (ino < 0) printf("allocate_inode() failed or no inodes available\n");
    else {
        printf("Allocated inode %d\n", ino);
        free_inode(ino);
        printf("Freed inode %d\n", ino);
    }
    
    return 0;
}
