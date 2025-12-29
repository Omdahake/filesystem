#include"virt_disk.h"
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<stdlib.h>
#include<regex.h>
#include<stdbool.h>
char* get_full_path(u32 ino) {
    static char path_buffer[MAX_INODES+1]; 
    char temp_name[MAX_FILENAME + 1];
    
    if (ino == 0) {
        strcpy(path_buffer, "/");
        return path_buffer;
    }
    u32 temp_stack[MAX_INODES]; //stack for holding inodes from current till root
    int stack_ptr = 0;
    u32 current = ino;

    //Traverse up to the root inode 0 and push inode ids onto the stack
    while (current != 0 && current < MAX_INODES && inode_table[current].used) {
        if (stack_ptr >= MAX_INODES) break; 
        temp_stack[stack_ptr++] = current;
        current = inode_table[current].parent;
        // Safety break for corrupted parent links self-parenting loop other than root)
        if (current == temp_stack[stack_ptr - 1] && current != 0) break;
    }

    if (current != 0) {
        // could not reach root (corrupted FS)
        strcpy(path_buffer, "<Invalid Path>");
        return path_buffer;
    }

    // build the path string by iterating backwards through the stack
    path_buffer[0] = '\0'; 
    strcat(path_buffer, "/"); 

    for (int i = stack_ptr - 1; i >= 0; i--) {
        u32 id = temp_stack[i];
        strncpy(temp_name, inode_table[id].name, MAX_FILENAME);
        temp_name[MAX_FILENAME] = '\0';
        strcat(path_buffer, temp_name);
        
        // append a slash for separation, unless it's the last component
        if (i > 0) {
            strcat(path_buffer, "/");
        }
    }
    
    // final check for the root path case 
    if (strlen(path_buffer) == 0) {
        strcpy(path_buffer, "/");
    }

    return path_buffer;
}

// Takes a compiled regex object.
static void find_paths_recursive(u32 ino, const regex_t *preg) {
    Inode *in = &inode_table[ino];

    // Check if the current inode's name matches the regex pattern
    // regexec() returns 0 on a successful match.
    if (regexec(preg, in->name, 0, NULL, 0) == 0) {
        // We found a match! Get and print its full path.
        char *full_path = get_full_path(ino);
        printf("Found: %s\n", full_path);
    }

    if (!in->is_dir) return;

    usize cnt;
    DirEntry *arr = read_dir_entries(in, &cnt);

    for (usize i = 0; i < cnt; i++) {
        u32 child_ino = arr[i].inode_id;
        if (child_ino > 0 && child_ino < MAX_INODES && inode_table[child_ino].used) {
            find_paths_recursive(child_ino, preg);
        }
    }
    free(arr);
}

void fs_find_paths(const char *pattern) {
    regex_t regex;
    int ret;
    char errbuf[100];
    printf("Searching for paths matching pattern: '%s'\n", pattern);
    ret = regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB);
    if (ret != 0) {
        // handle compilation error
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "Error compiling regex '%s': %s\n", pattern, errbuf);
        return;
    }

    // start recursion from the root inode 0
    find_paths_recursive(0, &regex);
    regfree(&regex);
}
int fs_create_file(const char *path) {
    //if found parent contain parent inode,new file name in name,target if last componenet already exist then it should zero else non zero  
    u32 parent; char name[MAX_FILENAME]; u32 target; 
    if (resolve_path(path, 1, &parent, name, &target) < 0) return -1;
    if (target) return -1;
    int ino = allocate_inode();
    if (ino < 0) return -1; //reach fs capacity
    Inode *in = &inode_table[ino];
    in->is_dir = 0;
    strncpy(in->name, name, MAX_FILENAME-1);
    in->parent = parent;
    in->size = 0;
    if (dir_add_entry(&inode_table[parent], name, ino) < 0) {
        free_inode(ino);
        return -1;
    }
    sync_metadata();//make changes permanent
    return ino;
}
int fs_create_dir(const char *path) {
    u32 parent; char name[MAX_FILENAME]; u32 target;
    if (resolve_path(path, 1, &parent, name, &target) < 0) return -1;
    if (target) return -1;
    int ino = allocate_inode();
    if (ino < 0) return -1;
    Inode *in = &inode_table[ino];
    in->is_dir = 1;
    strncpy(in->name, name, MAX_FILENAME-1);
    in->parent = parent;
    in->size = 0;
    if (dir_add_entry(&inode_table[parent], name, ino) < 0) {
        free_inode(ino);
        return -1;
    }
    sync_metadata();
    return ino;
}
int fs_rename(const char *oldpath, const char *newpath) {
    u32 old_parent; char old_name[MAX_FILENAME]; u32 old_target;
    if (resolve_path(oldpath, 1, &old_parent, old_name, &old_target) < 0) return -1;
    if (!old_target) return -1; //if it not exists return
    u32 new_parent; char new_name[MAX_FILENAME]; u32 new_target;
    if (resolve_path(newpath, 1, &new_parent, new_name, &new_target) < 0) return -1;
    if (new_target) return -1; //new name should also not exist if exist return
    dir_remove_entry(&inode_table[old_parent], old_name); //remove old name
    dir_add_entry(&inode_table[new_parent], new_name, old_target);//add new one
    Inode *in = &inode_table[old_target];
    in->parent = new_parent;
    strncpy(in->name, new_name, MAX_FILENAME-1);
    sync_metadata(); //make permanent
    return 0;
}
int fs_unlink(const char *path) {
    u32 parent; char name[MAX_FILENAME]; u32 target;
    if (resolve_path(path, 1, &parent, name, &target) < 0) return -1;
    if (!target) return -1; //it should exist to remove it
    Inode *t = &inode_table[target];
    if (t->is_dir) {
        usize cnt; DirEntry *arr = read_dir_entries(t, &cnt);
        free(arr);
        if (cnt > 0) return -1; //cnt > 0 mean more than one file/folder so can't delete
    }
    dir_remove_entry(&inode_table[parent], name); //if its file/empty_folder remove its inode entry
    free_inode(target); //makes the given inode free also all the data blocks inside it
    sync_metadata();
    return 0;
}
ssize_t fs_write_file(const char *path, const uint8_t *buf, size_t len) {
  uint32_t parent; char name[MAX_FILENAME]; uint32_t target;
  if (resolve_path(path, 1, &parent, name, &target) < 0) return -1; 
  if (!target) return -1; 
  Inode *in = &inode_table[target];
  if (in->is_dir) return -1; 
  for (int i = 0; i < DIRECT_PTRS; i++) {
    if (in->direct[i]) { clear_bitmap(in->direct[i]); sb.free_blocks++; in->direct[i]=0; }
  }   
  size_t needed = (len + BLOCK_SIZE - 1)/BLOCK_SIZE;
  if (needed > DIRECT_PTRS) return -1; 
  size_t written = 0;
  for (size_t i = 0; i < needed; i++) {
    uint32_t b = allocate_block();
    if (!b) return -1; 
    in->direct[i] = b;
    size_t tocopy = (len - written > BLOCK_SIZE) ? BLOCK_SIZE : (len - written);
    uint8_t blockbuf[BLOCK_SIZE]; memset(blockbuf,0,BLOCK_SIZE);
    memcpy(blockbuf, buf + written, tocopy);
    write_block(b, blockbuf);
    written += tocopy;
  }   
  in->size = len;
  sync_metadata();
  return written;
}

ssize_t fs_read_file(const char *path, uint8_t *buf, size_t maxlen) {
  uint32_t parent; char name[MAX_FILENAME]; uint32_t target;
  if (resolve_path(path, 1, &parent, name, &target) < 0) return -1; 
  if (!target) return -1; 
  Inode *in = &inode_table[target];
  if (in->is_dir) return -1; 
  size_t toread = (in->size < maxlen) ? in->size : maxlen;
  size_t read = 0;
  uint8_t blockbuf[BLOCK_SIZE];
  for (int i = 0; i < DIRECT_PTRS && read < toread; i++) {
    if (!in->direct[i]) break;
    ssize_t r = read_block(in->direct[i], blockbuf);
    if (r <= 0) break;
    size_t need = toread - read;
    size_t copy = (need > BLOCK_SIZE) ? BLOCK_SIZE : need;
    memcpy(buf + read, blockbuf, copy);
    read += copy;
  }
  return read;
}

void fs_list_dir_recursive(u32 ino, int depth) {
  Inode *in = &inode_table[ino]; 
  for (int i = 0; i < depth; i++) printf("  "); //content in same directory at same depth
  printf("%s%s\n", in->name, in->is_dir ? "/" : ""); //printing name of folder/file
  if (!in->is_dir) return; // stoping recursion if its a file
  usize cnt; DirEntry *arr = read_dir_entries(in, &cnt);
  for (usize i = 0; i < cnt; i++) {
    fs_list_dir_recursive(arr[i].inode_id, depth+1); //for each subdirectory repating proces with depth+1
  }
  free(arr);
}
int delete_inode_recursive(uint32_t ino) {
  if (ino == 0) return -1; // don't delete root 
  if (ino >= MAX_INODES) return -1;

  Inode *node = &inode_table[ino];
  if (!node->used) return 0; // already gone 

  if (!node->is_dir) {
    uint32_t parent = node->parent;
    if (parent < MAX_INODES){ 
      dir_remove_entry(&inode_table[parent], node->name);
    }
    free_inode(ino);
    return 0;
  }

  size_t cnt = 0;
  DirEntry *arr = read_dir_entries(node, &cnt);
  for (size_t i = 0; i < cnt; i++) {
    uint32_t child = arr[i].inode_id;
    delete_inode_recursive(child);
  }
  free(arr);

  uint32_t parent = node->parent;
  if (parent < MAX_INODES){
    dir_remove_entry(&inode_table[parent], node->name);
  }
  free_inode(ino);
  return 0;
}

int fs_delete_dir_recursive(const char *path) {
  uint32_t parent; char name[MAX_FILENAME]; uint32_t target;
  if (resolve_path(path, 1, &parent, name, &target) < 0) return -1;
  if (!target) return -1;
  if (target == 0) return -1; // don't delete root 
  delete_inode_recursive(target);
  sync_metadata();
  return 0;
}
