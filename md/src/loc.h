/*
 * Include this instead of <genesis.h> + core headers in frontend files.
 * SGDK first (its u8/bool types, see inc/stdint.h shim), then the core,
 * with SGDK's `Object` renamed out of the way of the core's world.h.
 */
#ifndef LOC_MD_H
#define LOC_MD_H

#include <genesis.h>

#define Object LocObject
#include "world.h"
#undef Object

#endif
