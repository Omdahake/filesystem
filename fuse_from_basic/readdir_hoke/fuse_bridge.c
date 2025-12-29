#define FUSE_USE_VERSION 29
#define _FILE_OFFSET_BITS 64
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>

#include "virt_disk.h"

static void try_loading_fs_metadata(void){
  if (access(DISK_PATH,F_OK) != 0){
    fprintf(stderr,"fuse_bridge: Disk not found,creating read-only empty filesystem..\n");
    if(format_fs() < 0){
      fprintf(stderr,"fuse_bridge format_fs failed\n");
      return;
    }
  }
  if (load_fs() < 0 ){
    fprintf(stderr,"fuse_bridge load_fs failed\n");
  }else{
    fprintf(stderr,"fuse_bridge filesystem loaded \n");
  }
}

static void inode_to_stat(u32 ino,struct stat * st){
  memset(st,0,sizeof(*st)); //set memory to zero 
  if( ino >= MAX_INODES || !inode_table[ino].used){
  st->st_mode = S_IFREG | 0444; //read-only permissions
  st->st_nlink = 1;
  st->st_size = 0;
  st->st_uid = getuid();
  st->st_gid = getgid();
  time_t now = time(NULL);
  st->st_atime = st->st_mtime = st->st_ctime = now;
  return;
  }
  Inode *inode = &inode_table[ino];
  if(inode->is_dir) st->st_mode = S_IFDIR |0555 ; //read-only directory
  else st->st_mode = S_IFREG | 0444; //read-only file
  st->st_nlink = 1;
  st->st_size = inode->size; //actual size of dir or file
  st->st_uid = getuid();
  st->st_gid = getgid();
  time_t now = time(NULL);
  st->st_atime = st->st_mtime = st->st_ctime = now;
}

static int path_to_inode(const char *path,u32 *out_ino){
  if(strcmp(path,"/") == 0){*out_ino = 0; return 0;}
  u32 parent; char name[MAX_FILENAME]; u32 target;
  int r = resolve_path(path,1,&parent,name,&target);
  if(r < 0) return -ENOENT;
  if(!target) return -ENOENT;
  *out_ino = target;
  return 0;
}

static int fsfuse_getattr(const char *path, struct stat *stbuf){
  u32 ino;
  if(strcmp(path,"/") == 0){
    inode_to_stat(0,stbuf);
    return 0;
  }
  if(path_to_inode(path,&ino) < 0) return -ENOENT;
  inode_to_stat(ino,stbuf);
  return 0;
}

static int fsfuse_readdir(const char *path,void *buf,fuse_fill_dir_t filler,off_t offset, struct fuse_file_info *fi){
  (void) offset; (void) fi;
  if (load_fs() < 0 ){
    fprintf(stderr,"fuse_bridge load_fs failed in readdir\n");
    return -ENOENT;
  }
  u32 ino;
  if(strcmp(path,"/") == 0 ) ino = 0;
  else if(path_to_inode(path,&ino) < 0) return -ENOENT;

  Inode *dir = &inode_table[ino];
  if(!dir->is_dir) return -ENOTDIR;

  usize cnt;
  DirEntry *entries = read_dir_entries(dir,&cnt);
  if(!entries) return 0;
  filler(buf,".",NULL,0);
  filler(buf,"..",NULL,0);
  for(usize i=0; i < cnt; i++){
    filler(buf,entries[i].name,NULL,0);
  }
  free(entries);
  return 0;
}

//fuse operations hooks handler 
static struct fuse_operations myfs_ops = {
  .getattr = fsfuse_getattr,
  .readdir = fsfuse_readdir
};


char *devfile = NULL;

int main(int argc, char **argv)
{
  try_loading_fs_metadata();
  int i;

  printf("starting fuse filesystem (no operations implemented)\n");

  //Get the device or image filename from arguments (if provided)
    for (i = 1; i < argc && argv[i][0] == '-'; i++);//for now skiping flags

  if (i < argc) {
    devfile = realpath(argv[i], NULL); //device file name path is in argv[i] now 
    printf("Device/image file: %s\n", devfile);
    //Remove the device file from arguments
      for (int j = i; j < argc - 1; j++) {
        argv[j] = argv[j + 1];
      }
    argc--; //remove the device file name from arugment list to pass to the fuse with only the flags and mount point name
    argv[argc] = NULL;
  }

  //Pass control to FUSE
    //For FUSE 2.9.9, use the 4-argument version
    return fuse_main(argc, argv, &myfs_ops, NULL);
}
