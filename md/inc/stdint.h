/* Shim: SGDK defines uint8_t & co. as macros for its own u8 & co., which
 * clashes with the compiler's <stdint.h>. Frontend files include
 * <genesis.h> first (then _TYPES_H_ is set and this header adds nothing);
 * core files never see genesis.h and get the real header. */
#ifdef _TYPES_H_
#ifndef INT16_MAX
#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647L - 1)
#define INT32_MAX  2147483647L
#define UINT32_MAX 4294967295UL
#endif
#else
#include_next <stdint.h>
#endif
