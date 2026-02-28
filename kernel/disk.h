#ifndef FUR_DISK_H
#define FUR_DISK_H

struct disk_info {
    int present;
    int index;
    unsigned char interface_type;
    unsigned int sectors28;
    unsigned short io_base;
    unsigned char slave;
    char model[41];
};

void disk_init(void);
int disk_available(void);
int disk_count(void);
int disk_get_info(int index, struct disk_info *out);
int disk_select(int index);
int disk_selected(void);
int disk_install_system(void);
int disk_load_fs(void);
int disk_save_fs(void);
int disk_format_fs(void);
const char *disk_last_error(void);

#endif
