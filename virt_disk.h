#ifndef VIRT_DISK_H
#define VIRT_DISK_H

#include <stdint.h> //for types like uint32_t, uint8_t 
#include <stddef.h> //size_t type and offsetof macro
#include <unistd.h> // for ssize_t 
#include <sys/types.h> // off_t (file offset type)

#define DISK_PATH "virtual_disk.img" //name for disk
#define FS_MAGIC 0x47525346 //some random string 'G' 'R' 'S' 'F' 

#define BLOCK_SIZE    1024        // bytes per block 
#define TOTAL_BLOCKS  8192        // number of blocks 
#define MAX_INODES    1024        // number of inodes 
#define MAX_FILENAME  60          // max filename length 
#define DIRECT_PTRS   20          // direct block pointers 

// derived disk size from block size and number of blocks 
#define DISK_SIZE   ((uint64_t)BLOCK_SIZE * (uint64_t)TOTAL_BLOCKS)
#define BITMAP_SIZE ((TOTAL_BLOCKS + 7) / 8)  // bitmap size calculation

// ---------------- SuperBlock ----------------

#define SB_U32_FIELDS 9  //there are total 9 attributes of superblock
#define SB_FIXED_BYTES (SB_U32_FIELDS * sizeof(uint32_t)) //since all are of size uint32_t

typedef struct SuperBlock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t inode_table_block;   // first block of inode table 
    uint32_t block_bitmap_block;  // first block of bitmap 
    uint32_t data_block_start;    // first usable data block 
    uint8_t  reserved[BLOCK_SIZE - SB_FIXED_BYTES];
} SuperBlock;

// SuperBlock must exactly occupy one block 
_Static_assert(sizeof(SuperBlock) == BLOCK_SIZE,
               "SuperBlock must be exactly one block in size");

// ---------------- Inode ---------------- 

// just for better reading of filename setting its offset 
#define INODE_NAME_OFFSET 128

// calculating Inode size to add padding so name start at 64th byte 
enum {
    INODE_PREFIX_BYTES = sizeof(uint32_t) +
        sizeof(uint32_t) +
        (sizeof(uint32_t) * DIRECT_PTRS) +
        sizeof(uint32_t) +
        sizeof(uint8_t)  +
        sizeof(uint8_t)  
};

#define INODE_PADDING_BYTES (INODE_NAME_OFFSET - INODE_PREFIX_BYTES)

typedef struct Inode {
    uint32_t id;                    // inode number 
    uint32_t size;                  // size in bytes 
    uint32_t direct[DIRECT_PTRS];   // direct block pointers 
    uint32_t parent;                // parent inode id 

    uint8_t  is_dir;                // 1=dir, 0=file 
    uint8_t  used;                  // 1=allocated, 0=free 

    uint8_t  padding[INODE_PADDING_BYTES]; // auto-computed padding 
    char     name[MAX_FILENAME];    // filename 
} Inode;

// Check offsets and sizes 
_Static_assert(offsetof(Inode, name) == INODE_NAME_OFFSET,
               "Inode.name offset mismatch");
_Static_assert(sizeof(Inode) == (INODE_NAME_OFFSET + MAX_FILENAME),
               "Inode struct size mismatch");

// ---------------- Directory Entry ---------------- 

typedef struct DirEntry {
    char name[MAX_FILENAME];
    uint32_t inode_id;
} DirEntry;

// ---------------- Globals ---------------- 

extern SuperBlock sb;
extern Inode inode_table[MAX_INODES];
extern uint8_t block_bitmap[BITMAP_SIZE];
extern int disk_fd;

// ---------------- Bitmap Helpers ---------------- 

static inline void set_bitmap(int idx) {
    block_bitmap[idx/8] |= (1 << (idx % 8));
}
static inline void clear_bitmap(int idx) {
    block_bitmap[idx/8] &= ~(1 << (idx % 8));
}
static inline int test_bitmap(int idx) {
    return (block_bitmap[idx/8] >> (idx % 8)) & 1;
}

// ---------------- functions ---------------- 

int format_fs(void);        // create & format virtual disk file 
int load_fs(void);          // load metadata into memory 
int sync_metadata(void);    // write metadata back to disk 

// allocation helpers 
int allocate_inode(void);
int free_inode(int ino);
uint32_t allocate_block(void);

// low-level block read/write 
ssize_t read_block(uint32_t block_idx, void *buf);
ssize_t write_block(uint32_t block_idx, const void *buf);

// debug 
void fs_info(void);

#endif 

