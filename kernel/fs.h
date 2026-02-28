#ifndef FUR_FS_H
#define FUR_FS_H

#include <stddef.h>
#include <stdint.h>

#define FS_MAX_NODES 128
#define FS_MAX_NAME 31
#define FS_MAX_FILE_SIZE 4096
#define FS_ACCESS_READ 4
#define FS_ACCESS_WRITE 2
#define FS_ACCESS_EXEC 1
#define FS_DEFAULT_DIR_MODE 0755
#define FS_DEFAULT_FILE_MODE 0644

void fs_init(void);
int fs_get_root(void);
int fs_get_parent(int node);
int fs_is_dir(int node);
const char *fs_get_name(int node);
int fs_get_owner(int node);
uint16_t fs_get_mode(int node);
int fs_set_owner(int node, uint16_t owner);
int fs_set_mode(int node, uint16_t mode);
int fs_check_access(int node, uint16_t user, uint8_t mask);
int fs_resolve(int cwd, const char *path);
int fs_mkdir(int cwd, const char *path);
int fs_mkdir_as(int cwd, const char *path, uint16_t owner, uint16_t mode);
int fs_touch(int cwd, const char *path);
int fs_touch_as(int cwd, const char *path, uint16_t owner, uint16_t mode);
int fs_list(int dir, int *out, size_t max_out);
int fs_read_file(int cwd, const char *path, const char **data, size_t *size);
int fs_write_file(int cwd, const char *path, const char *data, size_t size);
int fs_remove(int cwd, const char *path);
int fs_make_path(int node, char *out, size_t out_size);
size_t fs_serialize(uint8_t *out, size_t max_len);
int fs_deserialize(const uint8_t *in, size_t len);

#endif
