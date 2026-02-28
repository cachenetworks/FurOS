#include "kstring.h"

void *kmemset(void *dst, int value, size_t count) {
    unsigned char *p = (unsigned char *)dst;
    size_t i;

    for (i = 0; i < count; i++) {
        p[i] = (unsigned char)value;
    }

    return dst;
}

// lol
void *kmemcpy(void *dst, const void *src, size_t count) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;

    for (i = 0; i < count; i++) {
        d[i] = s[i];
    }

    return dst;
}

size_t kstrlen(const char *s) {
    size_t len = 0;

    if (s == 0) {
        return 0;
    }

    while (s[len] != '\0') {
        len++;
    }

    return len;
}

int kstrcmp(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0' && *a == *b) {
        a++;
        b++;
    }

    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    size_t i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i] || a[i] == '\0' || b[i] == '\0') {
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        }
    }

    return 0;
}

char *kstrcpy(char *dst, const char *src) {
    size_t i = 0;

    while (src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';

    return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    size_t i = 0;

    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }

    while (i < n) {
        dst[i] = '\0';
        i++;
    }

    return dst;
}
