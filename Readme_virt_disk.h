Virtual Disk File System Header (virt_disk.h)

This header file defines the core data structures and constants for a simple virtual disk file system.

The virtual disk is organized into several key components:

SuperBlock (SuperBlock): Located at the beginning of the disk, this block contains essential
			             metadata about the file system, including its magic number, block size,
			             total blocks, and a count of free inodes and blocks.

Block Bitmap (block_bitmap): A bit-array that tracks the allocation status of every block on 
			                 the disk. A 1 indicates an allocated block, and A 0 indicates
			                 free block.

Inode Table (inode_table): A table of fixed-size Inode structures. Each Inode represents a file
			               or a directory and contains metadata such as file size, pointers to
			               data blocks, and the filename.

Data Blocks: The main storage area where the actual file content and directory entries are stored.

Key Data Structures

    SuperBlock: Contains the global state of the file system.

    Inode: Represents a file or directory. It includes:

        An id for the inode number.

        size of the file in bytes.

        direct[20] pointers to the data blocks.

        parent inode id to maintain the directory tree.

        A flag to indicate if it's a file or a directory (is_dir).

        The name of the file or directory.

    DirEntry: A simle structure to link filename to its inode id

Important Constants

    BLOCK_SIZE: The size of each block on the disk (1024 bytes).

    TOTAL_BLOCKS: The total number of blocks (8192).

    MAX_INODES: The maximum number of files or directories the system can hold (1024).

    MAX_FILENAME: The maximum length for a filename (60 characters).

    DIRECT_PTRS: The number of direct pointers in an Inode (20).

API Functions

The header file declares several functions to manage the virtual disk and its file system:

    format_fs(): Formats the virtual disk, creating the necessary metadata and file structure.

    load_fs(): Loads the file system metadata from the disk into memory.

    sync_metadata(): Writes in-memory metadata back to the disk.

    read_block(): Reads a block of data from the disk.

    write_block(): Writes a block of data to the disk.

    allocate_inode(): Allocates a new, free inode.

    allocate_block(): Allocates a new, free data block.

    fs_info(): A debug function to display information about the file system.

Usage

This header file will be used for later modules of project . here the static assertion make sure that if its included then at least the structure was fine

