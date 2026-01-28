#include "fastlz.h"

#define FASTLZ_SAFE_DECOMPRESS

#if defined(__GNUC__) && (__GNUC__ > 2)
#define FASTLZ_EXPECT_CONDITIONAL(c, v)    (__builtin_expect((c), (v)))
#else
#define FASTLZ_EXPECT_CONDITIONAL(c, v)    (c)
#endif

#define FASTLZ_UNEXPECT_CONDITIONAL(c)    FASTLZ_EXPECT_CONDITIONAL(c, 0)
#define FASTLZ_EXPECT_CONDITIONAL_A(c, v)  FASTLZ_EXPECT_CONDITIONAL(c, v)
#define FASTLZ_EXPECT_CONDITIONAL_B(c, v)  FASTLZ_EXPECT_CONDITIONAL(c, v)

#if defined(__GNUC__) || defined(__clang__)
#define FASTLZ_INLINE inline
#elif defined(_MSC_VER)
#define FASTLZ_INLINE __forceinline
#else
#define FASTLZ_INLINE
#endif

typedef unsigned char flz_uint8;
typedef unsigned short flz_uint16;
typedef unsigned int flz_uint32;

#define MAX_COPY       32
#define MAX_LEN       264  /* 256 + 8 */
#define MAX_DISTANCE 8192

#if !defined(FASTLZ_STRICT_ALIGN)
#define FASTLZ_READU16(p)    (*(const flz_uint16*)(p))
#define FASTLZ_READU32(p)    (*(const flz_uint32*)(p))
#else
static FASTLZ_INLINE flz_uint16 FASTLZ_READU16(const void* p) {
  const flz_uint8* q = (const flz_uint8*)p;
  return (q[1] << 8) | q[0];
}
static FASTLZ_INLINE flz_uint32 FASTLZ_READU32(const void* p) {
  const flz_uint8* q = (const flz_uint8*)p;
  return (q[3] << 24) | (q[2] << 16) | (q[1] << 8) | q[0];
}
#endif

#define HASH_LOG  13
#define HASH_SIZE (1 << HASH_LOG)
#define HASH_MASK (HASH_SIZE - 1)
#define HASH_FUNC(v, p)    (((v) * 2654435761UL) >> (32 - HASH_LOG))

static flz_uint32 fastlz_hash(flz_uint32 v) {
  return HASH_FUNC(v, 0);
}

#define FASTLZ_VERSION1    0x00
#define FLZ_LITERAL    0x00
#define FLZ_COPY1    0x01
#define FLZ_COPY2    0x00
#define FLZ_FLAG_MASK    0x01
#define FLZ_LEN_MASK    0x07
#define FLZ_LEN_SHIFT    5
#define FLZ_OFFSET_MASK    0x1F
#define FLZ_OFFSET_SHIFT  0x00
#define FLZ1_MAX_LEN    (MAX_LEN - 2)
#define FLZ1_MAX_DISTANCE  MAX_DISTANCE
#define FLZ1_MAX_REF    (FLZ1_MAX_DISTANCE - 1)
#define FLZ1_GET_LEN(p)    ((*(p) >> FLZ_LEN_SHIFT) + 1)
#define FLZ1_GET_OFFSET(p)  (((*(p) & FLZ_OFFSET_MASK) << 8) | (*((p) + 1)) + 1)
#define FLZ1_SET_LITERAL(p, c)  (*(p) = (c))
#define FLZ1_SET_COPY(p, o, l)  (*(p) = (flz_uint8)(((l)-1) << FLZ_LEN_SHIFT) | (FLZ_COPY1 << 4) | (((o)-1) >> 8), \
                *((p)+1) = (flz_uint8)(((o)-1) & 0xff))


static const flz_uint8* htab[HASH_SIZE];

int fastlz_compress(const void* input, int length, void* output) {
  const flz_uint8* ip = (const flz_uint8*)input;
  const flz_uint8* ip_bound = ip + length - 2;
  const flz_uint8* ip_limit = ip + length - 12;
  flz_uint8* op = (flz_uint8*)output;

  //const flz_uint8* htab[HASH_SIZE];
  const flz_uint8** hslot;
  flz_uint32 hval;
  flz_uint32 copy;
  
  for (hslot = htab; hslot < htab + HASH_SIZE; hslot++) {
    *hslot = ip;
  }
  
  op[0] = FASTLZ_VERSION1;
  op++;
  
  hval = fastlz_hash(FASTLZ_READU32(ip));
  hslot = htab + hval;
  *hslot = ip;
  ip += 2;
  hval = fastlz_hash(FASTLZ_READU32(ip));
  hslot = htab + hval;
  *hslot = ip;
  ip++;

  while (FASTLZ_EXPECT_CONDITIONAL_A(ip < ip_limit, 1)) {
    const flz_uint8* ref;
    flz_uint32 distance;
    
    hval = fastlz_hash(FASTLZ_READU32(ip));
    hslot = htab + hval;
    ref = *hslot;
    *hslot = ip;
    
    distance = ip - ref;
    
    if (distance > 0 && distance <= FLZ1_MAX_REF && FASTLZ_READU32(ref) == FASTLZ_READU32(ip)) {
      flz_uint32 len = 2;
      const flz_uint8* max_len = ip_bound;
      
      if (max_len > ip + FLZ1_MAX_LEN) {
        max_len = ip + FLZ1_MAX_LEN;
      }
      
      while (ip + len < max_len && ref[len] == ip[len]) {
        len++;
      }
      
      FLZ1_SET_COPY(op, distance, len);
      op += 2;
      ip += len;
      
      if (len > 2) {
        flz_uint32 i = 1;
        for (; i < len; ++i) {
          hval = fastlz_hash(FASTLZ_READU32(ip - i));
          hslot = htab + hval;
          *hslot = ip - i;
        }
      }
      continue;
    }
    
    FLZ1_SET_LITERAL(op, *ip);
    op++;
    ip++;
  }
  
  while (ip < ip_bound + 2) {
    FLZ1_SET_LITERAL(op, *ip);
    op++;
    ip++;
  }

  return (int)(op - (flz_uint8*)output);
}

int fastlz_decompress(const void* input, int length, void* output, int maxout) {
  const flz_uint8* ip = (const flz_uint8*)input;
  const flz_uint8* ip_limit = ip + length;
  flz_uint8* op = (flz_uint8*)output;
  flz_uint8* op_limit = op + maxout;
  
  if (*ip != FASTLZ_VERSION1) {
    return 0;
  }
  ip++;

  while (FASTLZ_EXPECT_CONDITIONAL_B(ip < ip_limit, 1)) {
    unsigned int ctrl = *ip;
    
    if (!(ctrl & FLZ_FLAG_MASK)) {
      *op = ctrl;
      op++;
      ip++;
      
      if (FASTLZ_UNEXPECT_CONDITIONAL(op > op_limit)) {
        return 0;
      }
      
      continue;
    }
    
    flz_uint32 len = FLZ1_GET_LEN(&ctrl);
    flz_uint32 offset = FLZ1_GET_OFFSET(&ctrl);
    const flz_uint8* ref = op - offset;
    
    if (ref < (flz_uint8*)output) {
      return 0;
    }
    
    ip += 2;
    
    if (FASTLZ_UNEXPECT_CONDITIONAL(op + len > op_limit)) {
      return 0;
    }
    
    flz_uint32 i = 0;
    for (; i < len; ++i) {
      *op = *ref;
      op++;
      ref++;
    }
  }

  return (int)(op - (flz_uint8*)output);
}

int fastlz_compress_level(int level, const void* input, int length, void* output) {
  if (level != 1) {
    /* Level 2 não está implementado, usa level 1 */
  }
  return fastlz_compress(input, length, output);
}