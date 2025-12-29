#define FUSE_USE_VERSION 29
#define _FILE_OFFSET_BITS 64
#include <fuse.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

//empty operations structure - no filesystem operations implemented
static struct fuse_operations myfs_ops = {
  //completely empty - no operations defined
};

char *devfile = NULL;

int main(int argc, char **argv)
{
  int i;

  printf("starting fuse filesystem (no operations implemented)\n");

  //get the device or image filename from arguments (if provided)
    for (i = 1; i < argc && argv[i][0] == '-'; i++);//for now skiping flags

  if (i < argc) {
    devfile = realpath(argv[i], NULL); //device file name path is in argv[i] now 
    printf("Device/image file: %s\n", devfile);
    //remove the device file from arguments
      for (int j = i; j < argc - 1; j++) {
        argv[j] = argv[j + 1];
      }
    argc--; //remove the device file name from arugment list to pass to the fuse with only the flags and mount point name
    argv[argc] = NULL;
  }

  //pass control to FUSE
    //for FUSE 2.9.9, using the 4-argument version
    return fuse_main(argc, argv, &myfs_ops, NULL);
}
