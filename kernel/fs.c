#include "fs.h"

#include "kstring.h"

#include <stdbool.h>
#include <stdint.h>

struct fs_node {
    bool used;
    bool is_dir;
    int parent;
    uint16_t owner;
    uint16_t mode;
    char name[FS_MAX_NAME + 1];
    char data[FS_MAX_FILE_SIZE];
    size_t size;
};

static struct fs_node nodes[FS_MAX_NODES];

struct fs_disk_header {
    char magic[8];
    uint32_t version;
    uint32_t node_count;
} __attribute__((packed));

struct fs_disk_node {
    uint8_t used;
    uint8_t is_dir;
    int32_t parent;
    uint16_t owner;
    uint16_t mode;
    uint32_t size;
    char name[FS_MAX_NAME + 1];
    char data[FS_MAX_FILE_SIZE];
} __attribute__((packed));

struct fs_disk_node_v1 {
    uint8_t used;
    uint8_t is_dir;
    int32_t parent;
    uint32_t size;
    char name[FS_MAX_NAME + 1];
    char data[FS_MAX_FILE_SIZE];
} __attribute__((packed));

static bool is_valid_name(const char *name) {
    size_t i = 0;

    if (name == 0 || name[0] == '\0') {
        return false;
    }

    while (name[i] != '\0') {
        if (name[i] == '/') {
            return false;
        }
        i++;
        if (i > FS_MAX_NAME) {
            return false;
        }
    }

    return true;
}

static int alloc_node(void) {
    int i;

    for (i = 1; i < FS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            nodes[i].used = true;
            nodes[i].is_dir = false;
            nodes[i].parent = 0;
            nodes[i].owner = 0;
            nodes[i].mode = FS_DEFAULT_FILE_MODE;
            nodes[i].size = 0;
            nodes[i].name[0] = '\0';
            nodes[i].data[0] = '\0';
            return i;
        }
    }

    return -1;
}

static int find_child(int parent, const char *name) {
    int i;

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == parent && kstrcmp(nodes[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

static int resolve_parent_and_leaf(int cwd, const char *path, int *parent_out, char *leaf_out) {
    size_t len;
    size_t i;
    size_t slash = (size_t)-1;
    int parent;

    if (path == 0 || path[0] == '\0') {
        return -1;
    }

    len = kstrlen(path);
    if (len == 0) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    if (slash == (size_t)-1) {
        parent = cwd;
        if (!is_valid_name(path)) {
            return -1;
        }
        kstrcpy(leaf_out, path);
        *parent_out = parent;
        return 0;
    }

    if (slash == len - 1) {
        return -1;
    }

    if (slash == 0) {
        parent = 0;
    } else {
        char parent_path[128];
        if (slash >= sizeof(parent_path)) {
            return -1;
        }
        kmemcpy(parent_path, path, slash);
        parent_path[slash] = '\0';
        parent = fs_resolve(cwd, parent_path);
    }

    if (parent < 0 || !nodes[parent].is_dir) {
        return -1;
    }

    if (!is_valid_name(path + slash + 1)) {
        return -1;
    }

    kstrcpy(leaf_out, path + slash + 1);
    *parent_out = parent;
    return 0;
}

void fs_init(void) {
    kmemset(nodes, 0, sizeof(nodes));
    nodes[0].used = true;
    nodes[0].is_dir = true;
    nodes[0].parent = 0;
    nodes[0].owner = 0;
    nodes[0].mode = FS_DEFAULT_DIR_MODE;
    kstrcpy(nodes[0].name, "/");
}

int fs_get_root(void) {
    return 0;
}

int fs_get_parent(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return -1;
    }
    return nodes[node].parent;
}

int fs_is_dir(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return 0;
    }
    return nodes[node].is_dir ? 1 : 0;
}

const char *fs_get_name(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return 0;
    }
    return nodes[node].name;
}

int fs_get_owner(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return -1;
    }
    return (int)nodes[node].owner;
}

uint16_t fs_get_mode(int node) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return 0;
    }
    return nodes[node].mode;
}

int fs_set_owner(int node, uint16_t owner) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return -1;
    }
    nodes[node].owner = owner;
    return 0;
}

int fs_set_mode(int node, uint16_t mode) {
    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return -1;
    }
    nodes[node].mode = (uint16_t)(mode & 0777u);
    return 0;
}

int fs_check_access(int node, uint16_t user, uint8_t mask) {
    uint16_t mode;
    uint8_t allowed;

    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return 0;
    }

    if (mask == 0) {
        return 1;
    }

    if (user == 0) {
        return 1;
    }

    mode = (uint16_t)(nodes[node].mode & 0777u);
    if (user == nodes[node].owner) {
        allowed = (uint8_t)((mode >> 6) & 0x7u);
    } else {
        allowed = (uint8_t)(mode & 0x7u);
    }

    return ((allowed & mask) == mask) ? 1 : 0;
}

int fs_resolve(int cwd, const char *path) {
    int current;
    size_t i = 0;
    size_t token_len = 0;
    char token[FS_MAX_NAME + 1];

    if (path == 0 || path[0] == '\0') {
        return cwd;
    }

    current = (path[0] == '/') ? 0 : cwd;

    while (1) {
        char ch = path[i];

        if (ch == '/' || ch == '\0') {
            token[token_len] = '\0';
            if (token_len > 0) {
                if (kstrcmp(token, ".") == 0) {
                } else if (kstrcmp(token, "..") == 0) {
                    if (current != 0) {
                        current = nodes[current].parent;
                    }
                } else {
                    int child = find_child(current, token);
                    if (child < 0) {
                        return -1;
                    }
                    current = child;
                }
                token_len = 0;
            }
            if (ch == '\0') {
                break;
            }
        } else {
            if (token_len >= FS_MAX_NAME) {
                return -1;
            }
            token[token_len++] = ch;
        }

        i++;
    }

    return current;
}

int fs_mkdir_as(int cwd, const char *path, uint16_t owner, uint16_t mode) {
    int parent;
    char leaf[FS_MAX_NAME + 1];
    int existing;
    int node;

    if (resolve_parent_and_leaf(cwd, path, &parent, leaf) != 0) {
        return -1;
    }

    existing = find_child(parent, leaf);
    if (existing >= 0) {
        return nodes[existing].is_dir ? 0 : -1;
    }

    node = alloc_node();
    if (node < 0) {
        return -1;
    }

    nodes[node].is_dir = true;
    nodes[node].parent = parent;
    nodes[node].owner = owner;
    nodes[node].mode = (uint16_t)(mode & 0777u);
    kstrcpy(nodes[node].name, leaf);
    return 0;
}

int fs_mkdir(int cwd, const char *path) {
    return fs_mkdir_as(cwd, path, 0, FS_DEFAULT_DIR_MODE);
}

int fs_touch_as(int cwd, const char *path, uint16_t owner, uint16_t mode) {
    int parent;
    char leaf[FS_MAX_NAME + 1];
    int existing;
    int node;

    if (resolve_parent_and_leaf(cwd, path, &parent, leaf) != 0) {
        return -1;
    }

    existing = find_child(parent, leaf);
    if (existing >= 0) {
        return nodes[existing].is_dir ? -1 : 0;
    }

    node = alloc_node();
    if (node < 0) {
        return -1;
    }

    nodes[node].is_dir = false;
    nodes[node].parent = parent;
    nodes[node].owner = owner;
    nodes[node].mode = (uint16_t)(mode & 0777u);
    nodes[node].size = 0;
    nodes[node].data[0] = '\0';
    kstrcpy(nodes[node].name, leaf);
    return 0;
}

int fs_touch(int cwd, const char *path) {
    return fs_touch_as(cwd, path, 0, FS_DEFAULT_FILE_MODE);
}

int fs_list(int dir, int *out, size_t max_out) {
    size_t count = 0;
    int i;

    if (dir < 0 || dir >= FS_MAX_NODES || !nodes[dir].used || !nodes[dir].is_dir) {
        return -1;
    }

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == dir && i != dir) {
            if (count < max_out) {
                out[count] = i;
            }
            count++;
        }
    }

    if (count > 2147483647u) {
        return -1;
    }

    return (int)count;
}

int fs_read_file(int cwd, const char *path, const char **data, size_t *size) {
    int node = fs_resolve(cwd, path);

    if (node < 0 || nodes[node].is_dir) {
        return -1;
    }

    *data = nodes[node].data;
    *size = nodes[node].size;
    return 0;
}

int fs_write_file(int cwd, const char *path, const char *data, size_t size) {
    int node = fs_resolve(cwd, path);

    if (node < 0) {
        if (fs_touch(cwd, path) != 0) {
            return -1;
        }
        node = fs_resolve(cwd, path);
    }

    if (node < 0 || nodes[node].is_dir) {
        return -1;
    }

    if (size >= FS_MAX_FILE_SIZE) {
        size = FS_MAX_FILE_SIZE - 1;
    }

    if (size > 0 && data != 0) {
        kmemcpy(nodes[node].data, data, size);
    }
    nodes[node].data[size] = '\0';
    nodes[node].size = size;
    return 0;
}

int fs_remove(int cwd, const char *path) {
    int node = fs_resolve(cwd, path);
    int i;

    if (node <= 0) {
        return -1;
    }

    if (nodes[node].is_dir) {
        for (i = 0; i < FS_MAX_NODES; i++) {
            if (nodes[i].used && nodes[i].parent == node) {
                return -1;
            }
        }
    }

    nodes[node].used = false;
    return 0;
}

int fs_make_path(int node, char *out, size_t out_size) {
    int stack[FS_MAX_NODES];
    int depth = 0;
    size_t pos = 0;
    int i;

    if (out == 0 || out_size == 0) {
        return -1;
    }

    if (node < 0 || node >= FS_MAX_NODES || !nodes[node].used) {
        return -1;
    }

    if (node == 0) {
        if (out_size < 2) {
            return -1;
        }
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }

    while (node != 0 && depth < FS_MAX_NODES) {
        stack[depth++] = node;
        node = nodes[node].parent;
    }

    if (pos + 1 >= out_size) {
        return -1;
    }
    out[pos++] = '/';

    for (i = depth - 1; i >= 0; i--) {
        const char *name = nodes[stack[i]].name;
        size_t len = kstrlen(name);

        if (pos + len + 1 >= out_size) {
            return -1;
        }

        kmemcpy(out + pos, name, len);
        pos += len;

        if (i > 0) {
            out[pos++] = '/';
        }
    }

    out[pos] = '\0';
    return 0;
}

size_t fs_serialize(uint8_t *out, size_t max_len) {
    struct fs_disk_header header;
    size_t needed = sizeof(struct fs_disk_header) + (sizeof(struct fs_disk_node) * FS_MAX_NODES);
    size_t offset = 0;
    int i;

    if (out == 0 || max_len < needed) {
        return 0;
    }

    kstrcpy(header.magic, "FURFSv1");
    header.version = 2;
    header.node_count = FS_MAX_NODES;

    kmemcpy(out + offset, &header, sizeof(header));
    offset += sizeof(header);

    for (i = 0; i < FS_MAX_NODES; i++) {
        struct fs_disk_node dn;
        dn.used = nodes[i].used ? 1 : 0;
        dn.is_dir = nodes[i].is_dir ? 1 : 0;
        dn.parent = nodes[i].parent;
        dn.owner = nodes[i].owner;
        dn.mode = nodes[i].mode;
        dn.size = (uint32_t)nodes[i].size;
        kmemcpy(dn.name, nodes[i].name, sizeof(dn.name));
        kmemcpy(dn.data, nodes[i].data, sizeof(dn.data));
        kmemcpy(out + offset, &dn, sizeof(dn));
        offset += sizeof(dn);
    }

    return offset;
}

int fs_deserialize(const uint8_t *in, size_t len) {
    struct fs_disk_header header;
    size_t offset = 0;
    size_t required;
    int i;

    if (in == 0 || len < sizeof(struct fs_disk_header)) {
        return -1;
    }

    kmemcpy(&header, in + offset, sizeof(header));
    offset += sizeof(header);

    if (kstrncmp(header.magic, "FURFSv1", 7) != 0 || header.node_count != FS_MAX_NODES) {
        return -1;
    }

    if (header.version == 1) {
        required = sizeof(struct fs_disk_header) + (sizeof(struct fs_disk_node_v1) * FS_MAX_NODES);
    } else if (header.version == 2) {
        required = sizeof(struct fs_disk_header) + (sizeof(struct fs_disk_node) * FS_MAX_NODES);
    } else {
        return -1;
    }

    if (len < required) {
        return -1;
    }

    kmemset(nodes, 0, sizeof(nodes));

    for (i = 0; i < FS_MAX_NODES; i++) {
        if (header.version == 1) {
            struct fs_disk_node_v1 dn1;
            kmemcpy(&dn1, in + offset, sizeof(dn1));
            offset += sizeof(dn1);

            nodes[i].used = dn1.used ? true : false;
            nodes[i].is_dir = dn1.is_dir ? true : false;
            nodes[i].parent = dn1.parent;
            nodes[i].owner = 0;
            nodes[i].mode = nodes[i].is_dir ? FS_DEFAULT_DIR_MODE : FS_DEFAULT_FILE_MODE;
            nodes[i].size = dn1.size;

            if (nodes[i].size >= FS_MAX_FILE_SIZE) {
                nodes[i].size = FS_MAX_FILE_SIZE - 1;
            }

            kmemcpy(nodes[i].name, dn1.name, sizeof(nodes[i].name));
            nodes[i].name[FS_MAX_NAME] = '\0';
            kmemcpy(nodes[i].data, dn1.data, sizeof(nodes[i].data));
            nodes[i].data[nodes[i].size] = '\0';
            continue;
        } else {
            struct fs_disk_node dn2;
            kmemcpy(&dn2, in + offset, sizeof(dn2));
            offset += sizeof(dn2);

            nodes[i].used = dn2.used ? true : false;
            nodes[i].is_dir = dn2.is_dir ? true : false;
            nodes[i].parent = dn2.parent;
            nodes[i].owner = dn2.owner;
            nodes[i].mode = (uint16_t)(dn2.mode & 0777u);
            nodes[i].size = dn2.size;

            if (nodes[i].size >= FS_MAX_FILE_SIZE) {
                nodes[i].size = FS_MAX_FILE_SIZE - 1;
            }

            kmemcpy(nodes[i].name, dn2.name, sizeof(nodes[i].name));
            nodes[i].name[FS_MAX_NAME] = '\0';
            kmemcpy(nodes[i].data, dn2.data, sizeof(nodes[i].data));
            nodes[i].data[nodes[i].size] = '\0';
        }
    }

    if (!nodes[0].used || !nodes[0].is_dir) {
        fs_init();
        return -1;
    }

    nodes[0].used = true;
    nodes[0].is_dir = true;
    nodes[0].parent = 0;
    nodes[0].owner = 0;
    nodes[0].mode = FS_DEFAULT_DIR_MODE;
    kstrcpy(nodes[0].name, "/");
    return 0;
}
