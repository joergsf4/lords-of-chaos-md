/* Shim. With SGDK types in scope (_TYPES_H_, i.e. pulled in by genesis.h)
 * this is SGDK's own string.h. Core translation units never include SGDK
 * and get the libc subset the core needs from src/libc_md.c instead: SGDK's
 * memcpy/memset take a u16 length and return void, memcmp is missing. */
#ifdef _TYPES_H_
#include_next <string.h>
#else
#ifndef LOC_MD_STRING_H
#define LOC_MD_STRING_H
#include <stddef.h>
void *loc_memcpy(void *dst, const void *src, size_t n);
void *loc_memmove(void *dst, const void *src, size_t n);
void *loc_memset(void *dst, int c, size_t n);
int loc_memcmp(const void *a, const void *b, size_t n);
size_t loc_strlen(const char *s);
int loc_strcmp(const char *a, const char *b);
char *loc_strcpy(char *dst, const char *src);
#define memcpy loc_memcpy
#define memmove loc_memmove
#define memset loc_memset
#define memcmp loc_memcmp
#define strlen loc_strlen
#define strcmp loc_strcmp
#define strcpy loc_strcpy
#endif
#endif
