#ifndef FUR_SERIAL_H
#define FUR_SERIAL_H

#ifndef FUR_OS_SERIAL_DEBUG
#define FUR_OS_SERIAL_DEBUG 0
#endif

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char *str);

#endif
