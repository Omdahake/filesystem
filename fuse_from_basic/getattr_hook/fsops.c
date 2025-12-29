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
  u32 temp_stack[MAX_INODES];
  int stack_ptr = 0;
  u32 current = ino;

  while (current != 0 && current < MAX_INODES && inode_table[current].used) {
    if (stack_ptr >= MAX_INODES) break;
    temp_stack[stack_ptr++] = current;
    current = inode_table[current].parent;
    if (current == temp_stack[stack_ptr - 1] && current != 0) break;
  }

  if (current != 0) {
    strcpy(path_buffer, "<Invalid Path>");
    return path_buffer;
  }

  path_buffer[0] = '\0';
  strcat(path_buffer, "/");

  for (int i = stack_ptr - 1; i >= 0; i--) {
    u32 id = temp_stack[i];
    strncpy(temp_name, inode_table[id].name, MAX_FILENAME);
    temp_name[MAX_FILENAME] = '\0';
    strcat(path_buffer, temp_name);

    if (i > 0) {
      strcat(path_buffer, "/");
    }
  }

  if (strlen(path_buffer) == 0) {
    strcpy(path_buffer, "/");
  }

  return path_buffer;
}

static void find_paths_recursive(u32 ino, const regex_t *preg) {
  Inode *in = &inode_table[ino];

  if (regexec(preg, in->name, 0, NULL, 0) == 0) {
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
    regerror(ret, &regex, errbuf, sizeof(errbuf));
    fprintf(stderr, "Error compiling regex '%s': %s\n", pattern, errbuf);
    return;
  }

  find_paths_recursive(0, &regex);
  regfree(&regex);
}

int fs_create_file(const char *path) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;

  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (target) return -1;
  int ino = allocate_inode();
  if (ino <= 0) return -1;
  Inode *in = &inode_table[ino];
  in->is_dir = 0;
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
ssize fs_write_file(const char *path, const u8 *buf, usize len) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;
  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (!target) return -1;
  Inode *in = &inode_table[target];
  if (in->is_dir) return -1;
  for (int i = 0; i < DIRECT_PTRS; i++) {
    if (in->direct[i]) { clear_bitmap(in->direct[i]); sb.free_blocks++; in->direct[i]=0; }
  }
  usize needed = (len + BLOCK_SIZE - 1)/BLOCK_SIZE;
  if (needed > DIRECT_PTRS) return -1;
  usize written = 0;
  for (usize i = 0; i < needed; i++) {
    u32 b = allocate_block();
    if (!b) return -1;
    in->direct[i] = b;
    usize tocopy = (len - written > BLOCK_SIZE) ? BLOCK_SIZE : (len - written);
    u8 blockbuf[BLOCK_SIZE]; memset(blockbuf,0,BLOCK_SIZE);
    memcpy(blockbuf, buf + written, tocopy);
    write_block(b, blockbuf);
    written += tocopy;
  }
  in->size = len;
  sync_metadata();
  return written;
}

ssize fs_read_file(const char *path, u8 *buf, usize maxlen) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;
  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (!target) return -1;
  Inode *in = &inode_table[target];
  if (in->is_dir) return -1;
  usize toread = (in->size < maxlen) ? in->size : maxlen;
  usize read = 0;
  u8 blockbuf[BLOCK_SIZE];
  for (int i = 0; i < DIRECT_PTRS && read < toread; i++) {
    if (!in->direct[i]) break;
    ssize r = read_block(in->direct[i], blockbuf);
    if (r <= 0) break;
    usize need = toread - read;
    usize copy = (need > BLOCK_SIZE) ? BLOCK_SIZE : need;
    memcpy(buf + read, blockbuf, copy);
    read += copy;
  }
  return read;
}

int fs_create_dir(const char *path) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;

  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (target) return -1;
  int ino = allocate_inode();
  if (ino <= 0) return -1;
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
  const char *clean_old = oldpath;
  const char *clean_new = newpath;
  if (oldpath[0] == '/') clean_old = oldpath + 1;
  if (newpath[0] == '/') clean_new = newpath + 1;

  u32 old_parent; char old_name[MAX_FILENAME]; u32 old_target;
  if (resolve_path(clean_old, 1, &old_parent, old_name, &old_target) < 0) return -1;
  if (!old_target) return -1;
  u32 new_parent; char new_name[MAX_FILENAME]; u32 new_target;
  if (resolve_path(clean_new, 1, &new_parent, new_name, &new_target) < 0) return -1;
  if (new_target) return -1;
  dir_remove_entry(&inode_table[old_parent], old_name);
  dir_add_entry(&inode_table[new_parent], new_name, old_target);
  Inode *in = &inode_table[old_target];
  in->parent = new_parent;
  strncpy(in->name, new_name, MAX_FILENAME-1);
  sync_metadata();
  return 0;
}

int fs_unlink(const char *path) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;

  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (!target) return -1;
  Inode *t = &inode_table[target];
  if (t->is_dir) {
    usize cnt; DirEntry *arr = read_dir_entries(t, &cnt);
    free(arr);
    if (cnt > 0) return -1;
  }
  dir_remove_entry(&inode_table[parent], name);
  free_inode(target);
  sync_metadata();
  return 0;
}

void fs_list_dir_recursive(u32 ino, int depth) {
  Inode *in = &inode_table[ino];
  for (int i = 0; i < depth; i++) printf("  ");
  printf("%s%s\n", in->name, in->is_dir ? "/" : "");
  if (!in->is_dir) return;
  usize cnt; DirEntry *arr = read_dir_entries(in, &cnt);
  for (usize i = 0; i < cnt; i++) {
    fs_list_dir_recursive(arr[i].inode_id, depth+1);
  }
  free(arr);
}
int delete_inode_recursive(u32 ino) {
  if (ino == 0) return -1; // don't delete root 
  if (ino >= MAX_INODES) return -1;

  Inode *node = &inode_table[ino];
  if (!node->used) return 0;//  already gone 

  if (!node->is_dir) {
    u32 parent = node->parent;
    if (parent < MAX_INODES){
      dir_remove_entry(&inode_table[parent], node->name);
    }
    free_inode(ino);
    return 0;
  }

  usize cnt = 0;
  DirEntry *arr = read_dir_entries(node, &cnt);
  for (usize i = 0; i < cnt; i++) {
    u32 child = arr[i].inode_id;
    delete_inode_recursive(child);
  }
  free(arr);

  u32 parent = node->parent;
  if (parent < MAX_INODES){
    dir_remove_entry(&inode_table[parent], node->name);
  }
  free_inode(ino);
  return 0;
}

int fs_delete_dir_recursive(const char *path) {
  const char *clean_path = path;
  if (path[0] == '/') clean_path = path + 1;
  u32 parent; char name[MAX_FILENAME]; u32 target;
  if (resolve_path(clean_path, 1, &parent, name, &target) < 0) return -1;
  if (!target) return -1;
  if (target == 0) return -1; // don't delete root 
  delete_inode_recursive(target);
  sync_metadata();
  return 0;
}
