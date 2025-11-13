#include "virt_disk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t   usize;
typedef ssize_t  ssize;

SuperBlock sb;
Inode inode_table[MAX_INODES];
u8 block_bitmap[TOTAL_BLOCKS/8 + 1];
int disk_fd = -1;

static ssize write_data(int fd, const void *buf, usize count, off_t offset) {
    usize written = 0;
    u8 *p = (u8*)buf;//since but was void* need to type cast it
    while (written < count) {
        ssize w = pwrite(fd, p + written, count - written, offset + (off_t)written);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return (ssize)written;
        written += (usize)w;
    }
    return (ssize)written;
}
//writing the data till some error occurs due to interrupt or other means
//and returnig the number of written data count 

static ssize read_data(int fd, void *buf, usize count, off_t offset) {
    usize done = 0;
    u8 *p = (u8*)buf;
    while (done < count) {
        ssize r = pread(fd, p + done, count - done, offset + (off_t)done);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) break; //eof
        done += (usize)r;
    }
    return (ssize)done;
}
// read function (loop until all bytes read or error/eof) 

// format the virtual disk file and initialize metdata
int format_fs() {
    int fd = open(DISK_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return -1; //failed to create the disk

    //first remove all garbage data from disk 
    u64 target_size = DISK_SIZE;
    if (ftruncate(fd, (off_t)target_size) < 0) { 
        close(fd); 
        return -1; //if failed to write an image file then exiting 
    }
   
    memset(&sb, 0, sizeof(sb)); //mapping the memory of size superblock initially with zero
    sb.magic = FS_MAGIC;
    sb.block_size = BLOCK_SIZE;
    sb.total_blocks = TOTAL_BLOCKS;
    sb.total_inodes = MAX_INODES;

    // calculting inode table size in blocks 
    u32 inode_table_size_bytes  = MAX_INODES * sizeof(Inode);
    u32 inode_table_blocks      = (inode_table_size_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // calculating bitmap size in blocks 
    u32 bitmap_size_bytes  = (TOTAL_BLOCKS + 7) / 8; // same as BITMAP_SIZE
    u32 bitmap_blocks      = (bitmap_size_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;

    // set metadata block positions 
    sb.inode_table_block   = 1;                               // block 0 = superblock
    sb.block_bitmap_block  = sb.inode_table_block + inode_table_blocks;
    sb.data_block_start    = sb.block_bitmap_block + bitmap_blocks;

    // debug printig 
    printf("debug: sizeof(Inode)          = %zu\n", sizeof(Inode));
    printf("debug: inode_table_size_bytes = %u\n", inode_table_size_bytes);
    printf("debug: inode_table_blocks     = %u\n", inode_table_blocks);
    printf("debug: bitmap_size_bytes      = %u\n", bitmap_size_bytes);
    printf("debug: bitmap_blocks          = %u\n", bitmap_blocks);
    printf("debug: inode_table_block      = %u\n", sb.inode_table_block);
    printf("debug: block_bitmap_block     = %u\n", sb.block_bitmap_block);
    printf("debug: data_block_start       = %u\n", sb.data_block_start);

    sb.free_blocks = sb.total_blocks - sb.data_block_start;
    sb.free_inodes = sb.total_inodes - 1; // reserve inode 0 for root 

    // write superblock 
    if (write_data(fd, &sb, sizeof(sb), 0) != (ssize)sizeof(sb)) { 
        close(fd); 
        return -1; 
    }

    // Create and write empty inode table 
    Inode empty_inode;
    memset(&empty_inode, 0, sizeof(empty_inode));
    off_t inode_pos = (off_t)sb.inode_table_block * BLOCK_SIZE; //offset byte position 

    for (u32 i = 0; i < (u32)MAX_INODES; ++i) {
        empty_inode.id = i;   
        if (write_data(fd, &empty_inode, sizeof(empty_inode), 
                       inode_pos + (off_t)i * sizeof(Inode)) != (ssize)sizeof(Inode)) {
            //base address + offset here base_addr = inode_pos , offset = i * sizeof(Inode)
	    //where starting from i=1 since first one written for root inode
            close(fd);
            return -1;
        }
    }

    // Initialize and write bitmap - mark metadata blocks as used 
    usize bitmap_bytes = (sb.total_blocks + 7) / 8;
    usize bitmap_blocks_needed = (bitmap_bytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    usize full_bitmap_bytes = bitmap_blocks_needed * BLOCK_SIZE;
    
    u8 *bitmap_buf = calloc(1, full_bitmap_bytes);
    if (!bitmap_buf) { 
        close(fd); 
        return -1; 
    }

    // Mark metadata blocks [0, data_block_start) as used 
    for (u32 block = 0; block < sb.data_block_start && block < sb.total_blocks; ++block) {
        usize byte_index = block / 8;
        usize bit_index = block % 8;
        if (byte_index < full_bitmap_bytes) {
            bitmap_buf[byte_index] |= (u8)(1u << bit_index);
        }
    }

    // Write bitmap blocks 
    off_t bitmap_pos = (off_t)sb.block_bitmap_block * BLOCK_SIZE;
    if (write_data(fd, bitmap_buf, full_bitmap_bytes, bitmap_pos) != (ssize)full_bitmap_bytes) {
        free(bitmap_buf); 
        close(fd); 
        return -1;
    }
    free(bitmap_buf);

    // Create root inode 
    Inode root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.id = 0;
    root_inode.used = 1;
    root_inode.is_dir = 1;
    root_inode.size = 0;
    root_inode.parent = 0;
    strcpy(root_inode.name,"/");

    // Write root inode 
    if (write_data(fd, &root_inode, sizeof(root_inode), 
                   inode_pos + (off_t)0 * sizeof(Inode)) != (ssize)sizeof(root_inode)) {
        close(fd);
        return -1;
    }

    // flush to disk 
    if (fsync(fd) < 0) { 
        close(fd); 
        return -1; 
    }

    close(fd);
    
    printf("Filesystem formatted successfully!\n");
    printf("Final layout: superblock(0), Inodetabel(1-%u), Bitmap(%u), Datablocks(%u-%u)\n",
           sb.inode_table_block + inode_table_blocks - 1,
           sb.block_bitmap_block,
           sb.data_block_start, 
           sb.total_blocks - 1);
    
    return 0;
}

// load metadata into memory - only superblock 
int load_fs() {
    disk_fd = open(DISK_PATH, O_RDWR);
    if (disk_fd < 0) return -1;

    // read superblock 
    ssize r = read_data(disk_fd, &sb, sizeof(sb), 0);
    if (r != (ssize)sizeof(sb)) { 
        close(disk_fd); 
        disk_fd = -1; 
        return -1; 
    }
    if (sb.magic != FS_MAGIC) { 
        close(disk_fd); 
        disk_fd = -1; 
        return -1; 
    }

    return 0;
}


// Debug function to show filesystem info 
void fs_info() {
    printf("Filesystem Information:\n");
    printf("  Total blocks: %u\n", sb.total_blocks);
    printf("  Block size: %u bytes\n", sb.block_size);
    printf("  Free blocks: %u\n", sb.free_blocks);
    printf("  Total inodes: %u\n", sb.total_inodes);
    printf("  Free inodes: %u\n", sb.free_inodes);
    printf("  Inode table starts at block: %u\n", sb.inode_table_block);
    printf("  Bitmap starts at block: %u\n", sb.block_bitmap_block);
    printf("  Data blocks start at: %u\n", sb.data_block_start);
    printf("  Disk size: %lu bytes\n", (unsigned long)DISK_SIZE);
}


int sync_metadata() {
    if (disk_fd < 0) return -1;
    if (pwrite(disk_fd, &sb, sizeof(sb), 0) != sizeof(sb)) return -1;
    off_t inode_pos = sb.inode_table_block * BLOCK_SIZE;
    if (pwrite(disk_fd, inode_table, sizeof(inode_table), inode_pos) != sizeof(inode_table)) return -1;
    int bitmap_bytes = (sb.total_blocks + 7)/8;
    off_t bitmap_pos = sb.block_bitmap_block * BLOCK_SIZE;
    if (pwrite(disk_fd, block_bitmap, bitmap_bytes, bitmap_pos) != bitmap_bytes) return -1;
    fsync(disk_fd);
    return 0;
}


// allocate a free inode, return inode id or -1 
int allocate_inode() {
    int i = 1;
    // starting  from 1 (0 is root) 
    while( i < MAX_INODES ){
        if (!inode_table[i].used) {
            inode_table[i].used = 1;
            inode_table[i].size = 0;
            inode_table[i].is_dir = 0;
            inode_table[i].parent = 0;
            memset(inode_table[i].direct, 0, sizeof(inode_table[i].direct));
            memset(inode_table[i].name, 0, sizeof(inode_table[i].name));
            sb.free_inodes--;
            sync_metadata();
            return i;
        }
	i++;
    }
    return -1;
}

// free inode 
int free_inode(u32 ino) {
    if (ino <= 0 || ino >= MAX_INODES) return -1;
    Inode *in = &inode_table[ino];
    //free blocks
    u32 i = 0;
    while(i < DIRECT_PTRS){
        if (in->direct[i]) {
            clear_bitmap(in->direct[i]);
            sb.free_blocks +=1;
            in->direct[i] = 0;
        }
	i++;
    } 
    in->used = 0;
    in->size = 0;
    memset(in->name, 0, sizeof(in->name));
    sb.free_inodes +=1;
    return sync_metadata();
}

//allocate one free block, return block idx or 0 on error (0 reserved) 
u32 allocate_block() {
    u32 b = sb.data_block_start;
    while(b < sb.total_blocks){
        if (!test_bitmap(b)) {
            set_bitmap(b);
            sb.free_blocks--;
            sync_metadata();
            return b;
        }
	b++;	
    }
    return 0;
}

// helper read/write a block 
ssize read_block(u32 block_idx, void *buf) {
    if (block_idx >= sb.total_blocks) return -1;
    off_t pos = (off_t)block_idx * BLOCK_SIZE;
    return pread(disk_fd, buf, BLOCK_SIZE, pos); //returns read bytes 
}
ssize write_block(u32 block_idx, const void *buf) {
    if (block_idx >= sb.total_blocks) return -1;
    off_t pos = (off_t)block_idx * BLOCK_SIZE;
    return pwrite(disk_fd, buf, BLOCK_SIZE, pos); //returns written bytes
}

