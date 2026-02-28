#ifndef FUR_KSTRING_H
#define FUR_KSTRING_H

#include <stddef.h>

void *kmemset(void *dst, int value, size_t count);
void *kmemcpy(void *dst, const void *src, size_t count);
size_t kstrlen(const char *s);
int kstrcmp(const char *a, const char *b);
int kstrncmp(const char *a, const char *b, size_t n);
char *kstrcpy(char *dst, const char *src);
char *kstrncpy(char *dst, const char *src, size_t n);

#endif
