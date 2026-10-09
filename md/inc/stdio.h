/* Shim: the core only needs snprintf (src/libc_md.c). */
#ifndef LOC_MD_STDIO_H
#define LOC_MD_STDIO_H
int loc_snprintf(char *buf, unsigned long n, const char *fmt, ...);
#define snprintf loc_snprintf
#endif
