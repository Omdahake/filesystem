#include "virt_disk.h"
#include "fsops.c"
#include "dir.c"
#include <stdio.h>
#define Read_buff_size 65536

int main(int argc,char **argv) {
    if (access(DISK_PATH, F_OK) != 0) {
        printf("Formatting new filesystem...\n");
        if (format_fs() < 0) {
	      printf("failed to load the format_fs");
	      return 1;
       	}
    }
    if (load_fs() < 0) {
	    printf("failed to load the fs");
	    return 1;
    }

    printf("Loaded filesystem.\n");
    fs_info();
    if(argc==1){
      printf("No arguments Given\n");
      printf("Usage: [mkdir <path> | touch <path> | rename <old_path> <new_path> | ls | find <filename> | rm <path>]\n");
    }
    if(argc >=2 ){
      if (strcmp(argv[1], "mkdir") == 0 && argc==3) {
        if (fs_create_dir(argv[2])>0) printf("mkdir %s\n", argv[2]);
      } else if (strcmp(argv[1], "touch") == 0 && argc==3){
        if (fs_create_file(argv[2])>0) printf("touch %s\n",argv[2]);
      } else if (strcmp(argv[1], "rm") == 0 && argc==3){
        if (fs_unlink(argv[2]) == 0) printf("rm %s\n",argv[2]);
      }else if (strcmp(argv[1], "rename") == 0 && argc==4){
        if (fs_rename(argv[2],argv[3]) == 0) printf("rename %s to %s \n",argv[2],argv[3]);
      }else if (strcmp(argv[1], "ls") == 0) {
        fs_list_dir_recursive(0,0);
      }else if (strcmp(argv[1], "find") == 0 && argc == 3) {
        fs_find_paths(argv[2]);
      }else if (strcmp(argv[1], "write") == 0 && argc==4) {
        FILE *f = fopen(argv[3], "rb"); if (!f) { perror("open src"); return 1; }
        fseek(f,0,SEEK_END); long L = ftell(f); fseek(f,0,SEEK_SET);
        uint8_t *buf = malloc(L);
        fread(buf,1,L,f); fclose(f);
        if (fs_write_file(argv[2], buf, L) > 0) printf("wrote %ld bytes to %s\n", L, argv[2]);
        free(buf);
      }
      else if (strcmp(argv[1], "read") == 0 && argc==3) {
        uint8_t buf[Read_buff_size]; 
        ssize_t r = fs_read_file(argv[2], buf, sizeof(buf));
        if (r>0) printf("%.*s", (int)r, buf);
      }else if (strcmp(argv[1], "rm") == 0 && argc==4 && strcmp(argv[2], "-r")==0) { 
        if (fs_delete_dir_recursive(argv[3])==0) printf("rm -r %s done\n", argv[3]);
      }else {
        printf("Unknown command or incorrect arguments.\n");
        printf("Usage: %s [mkdir <path> | touch <path> | rename <old_path> <new_path> | ls | find <filename>\nrm <path>]\n", argv[0]);
      }
    }
    return 0;
}
