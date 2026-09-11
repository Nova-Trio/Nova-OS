/* SPDX-License-Identifier: MIT */
#ifndef __NVRM_NVTYPES_H__
#define __NVRM_NVTYPES_H__

#include <stdint.h>
#include <stddef.h>

#ifndef __counted_by
#if defined(__has_attribute) && __has_attribute(__counted_by__)
#define __counted_by(member) __attribute__((__counted_by__(member)))
#else
#define __counted_by(member)
#endif
#endif

#define NV_ALIGN_BYTES(a) __attribute__ ((__aligned__(a)))
#define NV_DECLARE_ALIGNED(f,a) f __attribute__ ((__aligned__(a)))

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef u32 NvV32;

typedef u8  NvU8;
typedef u16 NvU16;
typedef u32 NvU32;
typedef u64 NvU64;

typedef s8  NvS8;
typedef s16 NvS16;
typedef s32 NvS32;
typedef s64 NvS64;

typedef void* NvP64;

typedef NvU8 NvBool;
#define NV_TRUE  ((NvBool)1)
#define NV_FALSE ((NvBool)0)

typedef NvU32 NvHandle;
typedef NvU64 NvLength;

typedef NvU64 RmPhysAddr;
typedef NvU32 NV_STATUS;

#endif
