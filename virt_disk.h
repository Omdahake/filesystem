#ifndef VIRT_DISK_H
#define VIRT_DISK_H

#include <stdint.h> //for types like uint32_t, uint8_t 
#include <stddef.h> //size_t type and offsetof macro
#include <unistd.h> // for ssize_t 
#include <sys/types.h> // off_t (file offset type)
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t   usize;
typedef ssize_t  ssize;

#define DISK_PATH "virtual_disk.img" //name for disk
#define FS_MAGIC 0x47525346 //some random string 'G' 'R' 'S' 'F' 

#define BLOCK_SIZE    1024        // bytes per block 
#define TOTAL_BLOCKS  8192        // number of blocks 
#define MAX_INODES    1024        // number of inodes 
#define MAX_FILENAME  60          // max filename length 
#define DIRECT_PTRS   20          // direct block pointers 

// derived disk size from block size and number of blocks 
#define DISK_SIZE   ((u64)BLOCK_SIZE * (u64)TOTAL_BLOCKS)
#define BITMAP_SIZE ((TOTAL_BLOCKS + 7) / 8)  // some additional space intentionaly

// ---------------- SuperBlock ----------------

#define SB_U32_FIELDS 9  //there are total 9 attributes of superblock
#define SB_FIXED_BYTES (SB_U32_FIELDS * sizeof(u32)) //since all are of size u32

typedef struct SuperBlock {
    u32 magic;
    u32 block_size;
    u32 total_blocks;
    u32 free_blocks;
    u32 total_inodes;
    u32 free_inodes;
    u32 inode_table_block;   // first block of inode table 
    u32 block_bitmap_block;  // first block of bitmap 
    u32 data_block_start;    // first usable data block 
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
    INODE_PREFIX_BYTES = sizeof(u32) +
        sizeof(u32) +
        (sizeof(u32) * DIRECT_PTRS) +
        sizeof(u32) +
        sizeof(uint8_t)  +
        sizeof(uint8_t)  
};

#define INODE_PADDING_BYTES (INODE_NAME_OFFSET - INODE_PREFIX_BYTES)

typedef struct Inode {
    u32 id;                    // inode number 
    u32 size;                  // size in bytes 
    u32 direct[DIRECT_PTRS];   // direct block pointers 
    u32 parent;                // parent inode id 

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
    u32 inode_id;
} DirEntry;

// ---------------- Globals ---------------- 

extern SuperBlock sb;
extern Inode inode_table[MAX_INODES];
extern u8 block_bitmap[TOTAL_BLOCKS/8 + 1];
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
int free_inode(u32 ino);
u32 allocate_block(void);

// low-level block read/write 
ssize read_block(u32 block_idx, void *buf);
ssize write_block(u32 block_idx, const void *buf);

// debug 
void fs_info(void);

#endif 
