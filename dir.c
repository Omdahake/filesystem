#include"virt_disk.h"
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<stdbool.h>

//return total subdirectory/files count and DirEntry adress
DirEntry* read_dir_entries(Inode *dir, usize *out_count) {
    if (!dir->is_dir) { *out_count = 0; return NULL; }
    usize cap = 8; //initially size for arrary
    usize cnt = 0;
    DirEntry *arr = malloc(sizeof(DirEntry) * cap);
    u8 buf[BLOCK_SIZE];
    u32 remaining = dir->size;//only search dir->size bytes
    for (int i = 0; i < DIRECT_PTRS && remaining > 0; i++) {
        if (!dir->direct[i]) continue; // skip if not used
        usize r = read_block(dir->direct[i], buf); //else read block
        if (r <= 0) break; // if block is empty stop reading failed or its empty
        usize per = BLOCK_SIZE / sizeof(DirEntry);
        DirEntry *entries = (DirEntry*)buf;
        for (usize j = 0; j < per && remaining > 0; j++) {
            if (entries[j].inode_id == 0) {
		    remaining -= sizeof(DirEntry);
		    continue;
	    }
            if (cnt >= cap) { cap *= 2;//if sized limit exceed allocate more space
		    arr = realloc(arr, sizeof(DirEntry) * cap);
	    }
            arr[cnt++] = entries[j];
            remaining -= sizeof(DirEntry);
        }
    }
    *out_count = cnt;
    return arr;
}

int write_dir_entries(Inode *dir, DirEntry *entries, usize count) {
    if (!dir->is_dir) return -1;
    usize total_bytes = count * sizeof(DirEntry);
    usize needed_blocks = (total_bytes + BLOCK_SIZE - 1)/BLOCK_SIZE;
    if (needed_blocks > DIRECT_PTRS) return -1;    

    // allocate blocks if necessary 
    for (usize i = 0; i < needed_blocks; i++) {
        if (dir->direct[i] == 0) {
            u32 b = allocate_block();
            if (!b) return -1;
            dir->direct[i] = b;
        }
    }
    // clear previous content of other blocks 
    for (usize i = needed_blocks; i < DIRECT_PTRS; i++) {
        if (dir->direct[i]) {
	       	clear_bitmap(dir->direct[i]);
	       	dir->direct[i]=0;
	       	sb.free_blocks++;
       	}
    }

    u8 block_buf[BLOCK_SIZE];
    usize written = 0;
    for (usize i = 0; i < needed_blocks; i++) {
        memset(block_buf, 0, BLOCK_SIZE);
        usize tocopy = BLOCK_SIZE;
        usize remain = total_bytes - written;
        if (remain < BLOCK_SIZE) tocopy = remain;
        memcpy(block_buf, ((u8*)entries)+written, tocopy);
        write_block(dir->direct[i], block_buf);
        written += tocopy;
    }
    dir->size = total_bytes;
    return sync_metadata();
}

u32 dir_lookup(Inode *dir, const char *name) { //reads directory entries if given name found return its inode
    usize cnt;
    DirEntry *arr = read_dir_entries(dir, &cnt);
    for (usize i = 0; i < cnt; i++) {
        if (strncmp(arr[i].name, name, MAX_FILENAME) == 0) {
            u32 ino = arr[i].inode_id;
            free(arr);
            return ino;
        }
    }
    free(arr);
    return 0;
}

int dir_add_entry(Inode *dir, const char *name, u32 inode_id) {
    usize cnt;
    DirEntry *arr = read_dir_entries(dir, &cnt);
    // check exists 
    for (usize i = 0; i < cnt; i++) {
        if (strncmp(arr[i].name, name, MAX_FILENAME) == 0) {
            arr[i].inode_id = inode_id; // update 
            int r = write_dir_entries(dir, arr, cnt);
            free(arr);
            return r;
        }
    }
    // append 
    arr = realloc(arr, sizeof(DirEntry)*(cnt+1));
    memset(arr+cnt, 0, sizeof(DirEntry));
    strncpy(arr[cnt].name, name, MAX_FILENAME-1);
    arr[cnt].inode_id = inode_id;
    int r = write_dir_entries(dir, arr, cnt+1);
    free(arr);
    return r;
}

int dir_remove_entry(Inode *dir, const char *name) {
    usize cnt;
    DirEntry *arr = read_dir_entries(dir, &cnt);
    int found = 0;
    usize out = 0;
    for (usize i = 0; i < cnt; i++) {
        if (!found && strncmp(arr[i].name, name, MAX_FILENAME) == 0) { found = 1; continue; }
        arr[out++] = arr[i];
    }
    if (!found) { free(arr); return -1; }
    int r = write_dir_entries(dir, arr, out);//except the deleting one write all others
    free(arr);
    return r;
}

char** tokenize_path(const char *path, int *components) {
    char *tmp = strdup(path); //creates mutable modifieable copy of string
    char *p = tmp;
    if (*p == '/') p++; //if start from root moving ahead 
    char *tok; 
    char **parts = NULL;
    int cnt = 0;
    while ((tok = strsep(&p, "/")) != NULL) { //strsep tokenize it on given delimeter
        if (strlen(tok) == 0) continue;
        parts = realloc(parts, sizeof(char*)*(cnt+1));
        parts[cnt++] = strdup(tok);
    }
    free(tmp);
    *components = cnt;
    return parts;
}

void free_tokens(char **parts, int cnt) {
    for (int i = 0; i < cnt; i++) free(parts[i]);
    free(parts);
}

int resolve_path(const char *path, int want_target, u32 *out_parent, char *out_name, u32 *out_target) {
    if (strcmp(path, "/") == 0) {
        *out_parent = 0;
        strcpy(out_name, "/");
        if (out_target) *out_target = 0;
        return 0;
    }
    int cnt; char **parts = tokenize_path(path, &cnt);
    if (cnt == 0) { free_tokens(parts,cnt); return -1; }
    u32 cur = 0; // start at root 
    for (int i = 0; i < cnt-1; i++) {
        u32 child = dir_lookup(&inode_table[cur], parts[i]);
        if (!child) { free_tokens(parts,cnt); return -1; }
        if (!inode_table[child].is_dir) { free_tokens(parts,cnt); return -1; }
        cur = child;
    }
    // now cur is parent directory 
    *out_parent = cur;
    strncpy(out_name, parts[cnt-1], MAX_FILENAME-1);
    if (want_target && out_target) {
        u32 tid = dir_lookup(&inode_table[cur], out_name);
        *out_target = tid;
    }
    free_tokens(parts,cnt);
    return 0;
}
