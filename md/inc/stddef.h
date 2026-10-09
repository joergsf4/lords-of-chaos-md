/* Shim: SGDK defines size_t/ptrdiff_t as macros for u32, which clashes with the
 * compiler's <stddef.h> typedefs. With SGDK types in scope only NULL/offsetof. */
#ifdef _TYPES_H_
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#else
#include_next <stddef.h>
#endif
