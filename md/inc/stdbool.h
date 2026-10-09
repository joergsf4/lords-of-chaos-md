/* Shim: with SGDK types in scope (_TYPES_H_), bool is SGDK's u8 typedef;
 * the core compiles against the real C99 _Bool (see stdint.h). */
#ifdef _TYPES_H_
#else
#include_next <stdbool.h>
#endif
