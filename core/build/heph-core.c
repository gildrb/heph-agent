
// Imports
// =======

// The Objective-C headers take #include, not #import: a build
// (-o) reads an #import as the framework of an effect.

#pragma clang fp contract(off)

#if defined(__CUDACC_RTC__)
#define BEND_RTC 1
#endif

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#elif !defined(BEND_RTC)
#ifdef __APPLE__
#define _DARWIN_UNLIMITED_SELECT
#else
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <time.h>
#include <poll.h>
#include <sys/select.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __OBJC__
#include <Metal/Metal.h>
#include <Foundation/Foundation.h>
#elif BEND_CUDA
#include <cuda.h>
#include <nvrtc.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif
#endif

// Dialect
// =======

// Metal needs coherent(device) (MSL 3.2), or M1-class parts lose stores
// across the threadgroups of a dispatch. CUDA keeps plain data cacheable
// in L1: lanes hand off through a32 and FENCE. Only clang 19+ has both
// preserve_none and preserve_most, and compiles preserve_most soundly. A
// segment is a case of the device's switch; on the host, a preserve_none
// function (WL_SIG) entered by musttail, its words fresh at WL_OPEN.

#ifdef __METAL_VERSION__
#if __METAL_VERSION__ >= 320
#define DEV     coherent(device) device
#else
#define DEV     device
#endif
#define THR     thread
#define TG      threadgroup
#define INLINE  inline
#define OUTLINE static
#define CONSTV  constant
#define DEVICE  1
#define CLZ(x)  clz(x)
#define FENCE() atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst)
#define BAR()   threadgroup_barrier(mem_flags::mem_threadgroup)
#define BARD()  threadgroup_barrier(mem_flags::mem_device \
  | mem_flags::mem_threadgroup)
#else
#define DEV
#define THR
#define TG
#define INLINE  static inline
#define CONSTV  static const
#ifdef BEND_RTC
#define OUTLINE static __attribute__((noinline))
#define DEVICE  1
#define CLZ(x)  (u32)__clz((int)(x))
#define FENCE() __threadfence()
#define BAR()   __syncthreads()
#define BARD()  \
  { __threadfence(); __syncthreads(); }
#else
#if __has_attribute(preserve_none) && __has_attribute(preserve_most)
#define PRESERVE(A) __attribute__((A))
#else
#define PRESERVE(A)
#endif
#define OUTLINE static __attribute__((noinline, cold)) PRESERVE(preserve_most)
#define DEVICE  0
#define CLZ(x)  (u32)__builtin_clz(x)
#define FENCE() ((void)0)
#endif
#endif
#define FAR static __attribute__((noinline))

#if DEVICE
#define LOCK(l)
#define UNLOCK(l)
#define WL_CASE(F) case F:
#define WL_OPEN    {
#define WL_JMP(F)  { fid = (F); break; }
#define WL_DYN     WL_JMP
#else
#define LOCK(l)    while (__atomic_exchange_n(&(l), 1, __ATOMIC_ACQUIRE)) {}
#define UNLOCK(l)  __atomic_store_n(&(l), 0, __ATOMIC_RELEASE)
#define WL_FN      static PRESERVE(preserve_none) __attribute__((noinline)) Term
#define WL_CASE(F) WL_FN WL_##F(WL_SIG)
#define WL_OPEN    { WL_BANK u32 rn;
#define WL_JMP(F)  __attribute__((musttail)) return WL_##F(WL_ALL)
#define WL_DYN(F)  __attribute__((musttail)) return wl_tab[F](WL_ALL)
#endif
#define WL_SPIN     for (;;) { if (err_spun(e.mem, &wpoll)) { return 0; }
#define WL_SPUN     } break;
#define WL_AGAIN(F) continue

#define LANE_STEP (DEVICE ? (long)CUBE : 1)
#define STK(I)    sp[(long)(I) * LANE_STEP]

#define WL_RETN(N)  { rn = (N); sp -= LANE_STEP; WL_DYN((u32)STK(0)); }
#define WL_CONT     STK(-3)
#define WL_IDX      STK(-2)
#define WL_POPN(N)  sp -= N * LANE_STEP
#define WL_PUSHN(N) sp += N * LANE_STEP
#define WL_FRAME(T) \
  u64 wtl = task_tail(T); \
  u64 wtw = e.mem[wtl + 1]; \
  STK(0) = e.mem[wtl]; \
  STK(1) = (wtw >> 32) & 0xFFFF; \
  STK(2) = FID_EXIT; \
  sp += 3 * LANE_STEP;
#define WL_ARGS(A, N) \
  for (u32 wi = 0; wi + 1 < N; wi += 1) { \
    STK(wi) = e.mem[A + wi]; \
  } \
  sp += (N - 1) * LANE_STEP;
#define WL_ROOM(N) \
  if (DEVICE && sp + (N) * CUBE >= e.mem + STAT_OFF + CUBE) { \
    err_post(e.mem, ERR_DEEP); \
    return 0; \
  }

// Types
// =====

#ifdef __METAL_VERSION__
typedef ulong u64;
typedef uint  u32;
typedef uchar u8;
#elif defined(BEND_RTC)
typedef unsigned long long u64;
typedef unsigned int       u32;
typedef unsigned char      u8;
#else
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;
#endif
typedef float f32;

typedef u64 Term;

typedef struct {
  DEV u64* mem;
  DEV u64* alc;
} Env;

typedef struct {
  u64 off;
  u32 rd;
  u32 wr;
  u32 top;
} Bank;

#if DEVICE
typedef u32 u32a;
#else
typedef u32 __attribute__((may_alias)) u32a;
#endif

// Constants
// =========

#define TAG_PAK 1ull
#define TAG_CTR 2ull
#define TAG_CLO 3ull
#define TAG_BUF 4ull
#define TAG_TSK 5ull
#define TAG_ARR 6ull

#define TERM_HOLE (~0ull)
#define LOC_MASK  ((1ull << 40) - 1)
#define RFC_BIT   (1ull << 63)
#define RFC_CNT   ((1u << 24) - 1)
#define NAT_IMM   ((1ull << 48) - 1)

#define ERR_RING 1
#define ERR_TAGS 2
#define ERR_HEAP 3
#define ERR_FIDS 4
#define ERR_NATS 5
#define ERR_RFCS 6
#define ERR_DEEP 7
#define ERR_ARRS 8

#define LINE      16
#define PAGE_BITS 7
#define PAGE_LEN  (1ull << PAGE_BITS)
#define CUBE_T    128
#define CUBE      ((u64)CUBE_T * CUBE_T)
#define CUBE_G    (1u << CUBE_LOG)
#define LANES     ((u64)CUBE_T << CUBE_LOG)
#define RING_LOG  (17 - CUBE_LOG)
#define RING_LEN  (1ull << RING_LOG)
#define STAK_LEN  (1ull << 11)
#define NCLS      8
#define NCLS_ALL  32
#define IO_HELP   64

#define TG_HOLD   2304
#define CHUNK     256
#define CAP_WORDS 32768
#define QUANTUM   (DEVICE ? PAGE_LEN \
  : KEEP_WORDS < 32 * PAGE_LEN ? KEEP_WORDS : 32 * PAGE_LEN)
#if DEVICE
#define KEEP_WORDS CHUNK
#endif
#define RING_WORDS ((1ull << 10) + 2)

#define H_BUMP       0
#define H_CAP        1
#define H_CURSOR     LINE
#define H_ROOT_DONE  (2 * LINE)
#define H_ERROR_CODE (3 * LINE)
#define H_ROOT_WORD  (4 * LINE)
#define H_BANK       (H_ROOT_WORD + WL_RESW)

#define PAGE_UP(n) (((n) + PAGE_LEN - 1) & ~(PAGE_LEN - 1))
#define ALC_OFF  PAGE_UP(H_BANK + 3 * NCLS_ALL)
#define RING_OFF (ALC_OFF + CUBE * 2 * NCLS_ALL)
#define STAK_OFF (RING_OFF + CUBE * RING_WORDS)
#define STAT_OFF (STAK_OFF + CUBE * STAK_LEN)
#define HEAP_OFF (STAT_OFF + PAGE_UP(STAT_LEN))

// Globals
// =======

// The bag is 2^CUBE_LOG groups of CUBE_T lanes (a -D constant on the
// device). The device program compiles from the binary's own text.

#if !DEVICE

static u64*    CORPUS;
static u64    ALC[CUBE_T + 1][3 * NCLS_ALL] __attribute__((aligned(128)));
static u32    KEEP_WORDS;
static u32    CUBE_LOG = 7;
static u32    bank_lock;

static u32             pool_size;
static u32             pool_row;
static bool            pool_grow;
static u32             pool_tick;
static u32             pool_done;
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pool_wake = PTHREAD_COND_INITIALIZER;

#if BEND_METAL || BEND_CUDA
#pragma clang diagnostic ignored "-Wc23-extensions"
static const char BEND_SRC[] = {
#embed __FILE__
, 0 };
#endif

#ifdef __OBJC__
static id<MTLDevice>               gpu_dev;
static id<MTLCommandQueue>         gpu_que;
static id<MTLComputePipelineState> gpu_pso;
static id<MTLBuffer>               gpu_buf;
static id<MTLComputeCommandEncoder> gpu_enc;
#elif BEND_CUDA
static CUdevice   gpu_dev;
static CUmodule   gpu_lib;
static CUfunction gpu_pso;
#endif
static bool io_gpu;
static DEV Term*  io_stk;

static const char* CLI_HELP =
  "usage: %s [options] [arguments]\n"
  "  --threads N       worker threads, 1 to 128 (default: the CPU count)\n"
  "  --gpu on|off|4GB  run ! calls on the GPU, over this much of its memory\n"
  "                    (default: on if present, over 2GB on Metal)\n"
  "  --gpu-build       write the GPU program and exit\n"
  "  --bend-help       show this text\n"
  "  --                the rest are the program's arguments (IO.args)\n";

#endif

// Tables
// ======

#define CID_TUPLE 0
#define CID_SNIL 1
#define CID_SCON 2
#define CID_WCON 3
#define CID_EMIT 4
#define CID_HALT 5
#define CID_FAIL 6
#define CID_DONE 7
#define CID_NONE 8
#define CID_SOME 9
#define CID_FALSE 10
#define CID_TRUE 11
#define CID_UNIT 12
#define CID_CHR 13
#define CID_END 14
#define CID_BAD 15
#define CID_MUTE 16
#define CID_SAY 17
#define CID_S 18
#define CID_W 19
#define CID_TD 20
#define CID_TE 21
#define CID_TQ 22
#define CID_TR 23
#define CID_TX 24
#define CID_HDR 25
#define CID_NIL 26
#define CID_CON 27
#define CID_NUM 28
#define CID_PR 29
#define CID_CORE_CHUNK 30
#define CID_LT 31
#define CID_EQ 32
#define CID_GT 33
#define CID_CH 34
#define CID_CORE_EV 35
#define CID_CORE_SPAN 36
#define CID_CORE_BADID 37
#define CID_CORE_EMPTY 38
#define CID_CORE_MISSING 39
#define CID_CORE_FOUND 40
#define CID_CORE_HIT 41
#define CID_DG 42
#define CID_CORE_TQ 43
#define CID_CORE_RD 44
#define CID_CORE_SEL 45
#define CID_PL 46
#define CID_CORE_PAT 47
#define CID_CORE_ESTOP 48
#define CID_CORE_EBYTE 49
#define CID_CORE_CUR 50
#define CID_CORE_SLEAD 51
#define CID_CORE_SWORD 52
#define CID_CORE_SGAP 53
#define CID_CORE_ACC 54
#define CID_CORE_TAB 55
#define CID_CORE_JOB 56
#define CID_CORE_FGOT 57
#define CID_CORE_FLOOK 58
#define CID_CORE_FHOLD 59
#define CID_CORE_FBACK 60
#define CID_CORE_CEND 61
#define CID_CORE_CMORE 62
#define CID_STDIN_READ 63
#define CID_STDOUT_WRITE 64
#define FID_SCORE_FRAC 0
#define FID_SCORE_FRAC_K27 1
#define FID_NAT_POW 2
#define FID_NAT_POW_K29 3
#define FID_SCORE_ILOG2 4
#define FID_SCORE_ILOG2_K31 5
#define FID_SCORE_LOG2FX 6
#define FID_SCORE_LOG2FX_K36 7
#define FID_SCORE_LOG2FX_K37 8
#define FID_SCORE_LOG2FX_K38 9
#define FID_SCORE_IDF_GO 10
#define FID_SCORE_IDF_GO_K46 11
#define FID_SCORE_IDF_GO_K47 12
#define FID_SCORE_IDF 13
#define FID_CORE_TERM 14
#define FID_CORE_TERM_K88 15
#define FID_CORE_ADD_TERMS 16
#define FID_CORE_ADD_TERMS_K110 17
#define FID_CORE_RANK 18
#define FID_CORE_RANK_K130 19
#define FID_CORE_CHUNK_GO 20
#define FID_CORE_CHUNK_GO_K143 21
#define FID_LIST_LENGTH 22
#define FID_LIST_LENGTH_K145 23
#define FID_CORE_CHUNK_DOC 24
#define FID_IO_PURE 25
#define FID_FRAME 26
#define FID_FRAME_K228 27
#define FID_FRAME_K229 28
#define FID_FRAME_K230 29
#define FID_FRAME_K231 30
#define FID_RUN 31
#define FID_RUN_C233 32
#define FID_RUN_C234 33
#define FID_RUN_C235 34
#define FID_RUN_K236 35
#define FID_RUN_C237 36
#define FID_IO_TRY_C238 37
#define FID_IO_TRY_C239 38
#define FID_IO_PASS_C240 39
#define FID_IO_PASS_C241 40
#define FID_RUN_C242 41
#define FID_RUN_K243 42
#define FID_IO_BIND 43
#define FID_IO_BIND_C255 44
#define FID_IO_BIND_K256 45
#define FID_MAIN 46
#define FID_MAIN_C258 47
#define FID_IO_TRY_C259 48
#define FID_IO_TRY_C260 49
#define FID_IO_PASS_C261 50
#define FID_IO_PASS_C262 51
#define FID_MAIN_C263 52
#define FID_MAIN_K264 53
#define FID_STDIN_READ 54
#define FID_STDOUT_WRITE 55
#define FID_IO_EMIT 56
#define FID_CLO_APPLY 57
#define FID_EXIT 58
#define FID_ENTER 59
CONSTV u8 FID_T[][3] = { { 3, 0, 2 }, { 3, 1, 2 }, { 2, 0, 2 }, { 2, 1, 2 }, { 3, 0, 2 }, { 1, 1, 2 }, { 1, 0, 2 }, { 2, 1, 2 }, { 3, 1, 2 }, { 2, 1, 2 }, { 2, 0, 2 }, { 2, 1, 2 }, { 2, 1, 2 }, { 2, 0, 2 }, { 5, 0, 2 }, { 7, 1, 2 }, { 6, 0, 2 }, { 6, 3, 2 }, { 5, 0, 2 }, { 5, 3, 2 }, { 7, 0, 2 }, { 4, 2, 2 }, { 1, 0, 2 }, { 1, 1, 2 }, { 3, 0, 2 }, { 2, 0, 2 }, { 8, 0, 2 }, { 10, 2, 2 }, { 10, 1, 2 }, { 10, 2, 2 }, { 10, 1, 2 }, { 12, 0, 2 }, { 1, 0, 2 }, { 1, 0, 2 }, { 2, 0, 2 }, { 12, 11, 2 }, { 12, 0, 2 }, { 2, 0, 2 }, { 1, 0, 2 }, { 2, 0, 2 }, { 3, 0, 2 }, { 10, 0, 2 }, { 12, 11, 2 }, { 3, 0, 2 }, { 3, 0, 2 }, { 2, 1, 2 }, { 0, 0, 2 }, { 1, 0, 2 }, { 2, 0, 2 }, { 1, 0, 2 }, { 2, 0, 2 }, { 3, 0, 2 }, { 1, 0, 2 }, { 12, 11, 2 }, { 1, 0, 2 }, { 3, 0, 2 }, { 1, 0, 2 }, { 2, 0, 2 } };
CONSTV u8 CID_T[][2] = { { 2, 0 }, { 0, 0 }, { 2, 0 }, { 2, 0 }, { 1, 0 }, { 2, 0 }, { 1, 0 }, { 1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 1, 0 }, { 0, 0 }, { 1, 0 }, { 8, 0 }, { 10, 0 }, { 8, 0 }, { 2, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 4, 0 }, { 0, 0 }, { 2, 0 }, { 4, 0 }, { 7, 0 }, { 2, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 3, 0 }, { 4, 0 }, { 4, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 3, 0 }, { 5, 0 }, { 4, 0 }, { 6, 0 }, { 3, 0 }, { 0, 0 }, { 3, 0 }, { 2, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 2, 0 }, { 2, 0 }, { 5, 0 }, { 5, 0 }, { 7, 0 }, { 3, 0 }, { 3, 0 }, { 2, 0 }, { 5, 0 }, { 1, 0 }, { 3, 0 } };
#define STAT_LEN 458

#define WL_RESW 11
#define BANGS   0

#define WL_BANK Term r0, r1, r2, r3, r4, r5, rp, r6, r7, r8, r9, r10, r11;

#define WL_LOAD(A, N) \
  do { \
    if ((N) <= 0) break; r0 = e.mem[(A) + 0]; \
    if ((N) <= 1) break; r1 = e.mem[(A) + 1]; \
    if ((N) <= 2) break; r2 = e.mem[(A) + 2]; \
    if ((N) <= 3) break; r3 = e.mem[(A) + 3]; \
    if ((N) <= 4) break; r4 = e.mem[(A) + 4]; \
    if ((N) <= 5) break; r5 = e.mem[(A) + 5]; \
    if ((N) <= 6) break; r6 = e.mem[(A) + 6]; \
    if ((N) <= 7) break; r7 = e.mem[(A) + 7]; \
    if ((N) <= 8) break; r8 = e.mem[(A) + 8]; \
    if ((N) <= 9) break; r9 = e.mem[(A) + 9]; \
    if ((N) <= 10) break; r10 = e.mem[(A) + 10]; \
    if ((N) <= 11) break; r11 = e.mem[(A) + 11]; \
  } while (0);

#define WL_LAST(X) \
  switch (war) { \
    case 0: r0 = (X); \
      break; \
    case 1: r1 = (X); \
      break; \
    case 2: r2 = (X); \
      break; \
    case 3: r3 = (X); \
      break; \
    case 4: r4 = (X); \
      break; \
    case 5: r5 = (X); \
      break; \
    case 6: r6 = (X); \
      break; \
    case 7: r7 = (X); \
      break; \
    case 8: r8 = (X); \
      break; \
    case 9: r9 = (X); \
      break; \
    case 10: r10 = (X); \
      break; \
    case 11: r11 = (X); \
      break; \
  }

#define WL_SAVE(V) (V)[0] = r0; (V)[1] = r1; (V)[2] = r2; (V)[3] = r3; (V)[4] = r4; (V)[5] = r5; (V)[6] = r6; (V)[7] = r7; (V)[8] = r8; (V)[9] = r9; (V)[10] = r10;

#define WL_TAKE(V) r0 = (V)[0]; r1 = (V)[1]; r2 = (V)[2]; r3 = (V)[3]; r4 = (V)[4]; r5 = (V)[5]; r6 = (V)[6]; r7 = (V)[7]; r8 = (V)[8]; r9 = (V)[9]; r10 = (V)[10];

#define WL_SIG Env e, DEV Term* sp, u32 seq, u32 rn, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term rp, Term r6, Term r7, Term r8, Term r9, Term r10, Term r11

#define WL_ALL e, sp, seq, rn, r0, r1, r2, r3, r4, r5, rp, r6, r7, r8, r9, r10, r11

#define WL_TABLE WL_X(FID_SCORE_FRAC) WL_X(FID_SCORE_FRAC_K27) WL_X(FID_NAT_POW) WL_X(FID_NAT_POW_K29) WL_X(FID_SCORE_ILOG2) WL_X(FID_SCORE_ILOG2_K31) WL_X(FID_SCORE_LOG2FX) WL_X(FID_SCORE_LOG2FX_K36) WL_X(FID_SCORE_LOG2FX_K37) WL_X(FID_SCORE_LOG2FX_K38) WL_X(FID_SCORE_IDF_GO) WL_X(FID_SCORE_IDF_GO_K46) WL_X(FID_SCORE_IDF_GO_K47) WL_X(FID_SCORE_IDF) WL_X(FID_CORE_TERM) WL_X(FID_CORE_TERM_K88) WL_X(FID_CORE_ADD_TERMS) WL_X(FID_CORE_ADD_TERMS_K110) WL_X(FID_CORE_RANK) WL_X(FID_CORE_RANK_K130) WL_X(FID_CORE_CHUNK_GO) WL_X(FID_CORE_CHUNK_GO_K143) WL_X(FID_LIST_LENGTH) WL_X(FID_LIST_LENGTH_K145) WL_X(FID_CORE_CHUNK_DOC) WL_X(FID_IO_PURE) WL_X(FID_FRAME) WL_X(FID_FRAME_K228) WL_X(FID_FRAME_K229) WL_X(FID_FRAME_K230) WL_X(FID_FRAME_K231) WL_X(FID_RUN) WL_X(FID_RUN_C233) WL_X(FID_RUN_C234) WL_X(FID_RUN_C235) WL_X(FID_RUN_K236) WL_X(FID_RUN_C237) WL_X(FID_IO_TRY_C238) WL_X(FID_IO_TRY_C239) WL_X(FID_IO_PASS_C240) WL_X(FID_IO_PASS_C241) WL_X(FID_RUN_C242) WL_X(FID_RUN_K243) WL_X(FID_IO_BIND) WL_X(FID_IO_BIND_C255) WL_X(FID_IO_BIND_K256) WL_X(FID_MAIN) WL_X(FID_MAIN_C258) WL_X(FID_IO_TRY_C259) WL_X(FID_IO_TRY_C260) WL_X(FID_IO_PASS_C261) WL_X(FID_IO_PASS_C262) WL_X(FID_MAIN_C263) WL_X(FID_MAIN_K264) WL_X(FID_STDIN_READ) WL_X(FID_STDOUT_WRITE) WL_X(FID_IO_EMIT) WL_X(FID_CLO_APPLY) WL_X(FID_EXIT)
#define MAIN_FID FID_MAIN
#define MAIN_PURE 0
#define BLK_SHR 0

#define TAB_AT(T, S, I) T[S < I ? S : I]

#define fid_arity(x) ((u32)FID_T[x][0])
#define fid_resw(x)  ((u32)FID_T[x][1])
#define fid_bangs(x) ((bool)(FID_T[x][2] & 1))
#define fid_nofk(x)  ((bool)(FID_T[x][2] & 2))
#define cid_arity(x) ((u32)CID_T[x][0])
#define cid_hot(x)   ((bool)CID_T[x][1])

// A32
// ===

// C11's atomics on every lane; a device FENCE releases or acquires.
// Metal's a32_load reads through a volatile local, or the M1 pipeline
// build dies. A weak CAS may fail with the cell still x: a32_cmpx loops.

#define A32_LOOP(k, x) \
  INLINE u32 a32_##k(DEV u32* p, u32 v) { \
    u32 o = a32_load(p); \
    while (!a32_cas(p, &o, x)) { \
    } \
    return o; \
  }

#ifdef __METAL_VERSION__

INLINE DEV atomic_uint* A32(DEV u32* p) {
  return (DEV atomic_uint*)p;
}

INLINE TG atomic_uint* A32(TG u32* p) {
  return (TG atomic_uint*)p;
}

#define a32_load(p) \
  ({ volatile thread u32 _a32v = atomic_load_explicit(A32(p), RLX); _a32v; })

#else

#define a32_load(p) atomic_load_explicit(A32(p), RLX)

#ifdef BEND_RTC

#define A32(p) (p)
#define atomic_load_explicit(p, o)     (*(volatile u32*)(p))
#define atomic_store_explicit(p, v, o) (*(volatile u32*)(p) = (v))
#define atomic_fetch_add_explicit(p, v, o) atomicAdd((u32*)(p), v)
#define atomic_fetch_sub_explicit(p, v, o) atomicSub((u32*)(p), v)
#define atomic_fetch_and_explicit(p, v, o) atomicAnd((u32*)(p), v)
#define atomic_fetch_or_explicit(p, v, o) atomicOr((u32*)(p), v)
#define atomic_fetch_xor_explicit(p, v, o) atomicXor((u32*)(p), v)
#define atomic_fetch_min_explicit(p, v, o) atomicMin((u32*)(p), v)
#define atomic_fetch_max_explicit(p, v, o) atomicMax((u32*)(p), v)
#define atomic_compare_exchange_weak_explicit(p, e, v, s, f) a32_swp(p, e, v)

INLINE bool a32_swp(DEV u32* p, u32* e, u32 v) {
  u32 x = *e;
  *e = atomicCAS((u32*)p, x, v);
  return *e == x;
}

#else

#define A32(p) ((_Atomic u32*)(p))
#define atomic_fetch_min_explicit __c11_atomic_fetch_min
#define atomic_fetch_max_explicit __c11_atomic_fetch_max

#endif

#endif

#define RLX memory_order_relaxed

#if DEVICE
#define REL RLX
#define ACQ RLX
#define ACR RLX
#define a32_acq(p) FENCE()
#else
#define REL memory_order_release
#define ACQ memory_order_acquire
#define ACR memory_order_acq_rel
#define a32_acq(p) ((void)a32_load_acq(p))
#endif

#define a32_store(p, v)     atomic_store_explicit(A32(p), v, RLX)
#define a32_add(p, v) atomic_fetch_add_explicit(A32(p), v, RLX)
#define a32_sub(p, v) atomic_fetch_sub_explicit(A32(p), v, RLX)
#define a32_and(p, v) atomic_fetch_and_explicit(A32(p), v, RLX)
#define a32_or(p, v) atomic_fetch_or_explicit(A32(p), v, RLX)
#define a32_xor(p, v) atomic_fetch_xor_explicit(A32(p), v, RLX)
#define a32_min(p, v) atomic_fetch_min_explicit(A32(p), v, RLX)
#define a32_max(p, v) atomic_fetch_max_explicit(A32(p), v, RLX)
#define a32_sub_rel(p, v)   (FENCE(), atomic_fetch_sub_explicit(A32(p), v, REL))
#define a32_store_rel(p, v) (FENCE(), atomic_store_explicit(A32(p), v, REL))
#define a32_at(H, word)     ((DEV u32*)&(H)[word])

INLINE u32 a32_load_acq(DEV u32* p) {
  u32 v = atomic_load_explicit(A32(p), ACQ);
  FENCE();
  return v;
}

INLINE bool a32_cas(DEV u32* p, THR u32* e, u32 v) {
  FENCE();
  bool ok = atomic_compare_exchange_weak_explicit(A32(p), e, v, ACR, ACQ);
  FENCE();
  return ok;
}

A32_LOOP(exch, v)

INLINE u32 a32_cmpx(DEV u32* p, u32 x, u32 v) {
  u32 o = x;
  while (!a32_cas(p, &o, v) && o == x) {
  }
  return o;
}

// Err
// ===

#if DEVICE

INLINE void err_post(DEV u64* H, u32 code) {
  a32_cmpx(a32_at(H, H_ERROR_CODE), 0, code);
}

#else

static const char* ERR_TEXT[] = { "",
  "runtime fail-stop",
  "runtime fail-stop",
  "out of memory: run again with a bigger span, as in --gpu 8GB",
  "a function the device does not hold",
  "a Nat past the largest immediate 2^48-1",
  "runtime fail-stop",
  "memory fault (machine stack overflow?)",
  "an array past the deepest block class 31" };

static void err_fail(const char* msg) {
  fflush(stdout);
  fprintf(stderr, "bend: %s\n", msg);
  _exit(1);
}

static void err_post(u64* H, u32 code) {
  err_fail(ERR_TEXT[code]);
}

static void err_trap(int sig) {
  err_post(NULL, ERR_DEEP);
}

#endif

#define err_seen(H)    (DEVICE && a32_load(a32_at(H, H_ERROR_CODE)) != 0)
#define err_spun(H, n) ((++*(n) & 4095) == 0 && err_seen(H))

#ifdef __METAL_VERSION__
INLINE f32 atan2_c99(f32 y, f32 x) {
  return y == 0.0f && x == x
    ? copysign(signbit(x) ? M_PI_F : 0.0f, y) : atan2(y, x);
}
#define sqrt  precise::sqrt
#define exp   precise::exp
#define log   precise::log
#define log2  precise::log2
#define log10 precise::log10
#define sin   fast::sin
#define cos   fast::cos
#define tan   fast::tan
#define pow   precise::pow
#define fmod  precise::fmod
#define atan2 atan2_c99
#endif

#define U32_BIN(a, o, b) ((u64)((u32)(a) o (u32)(b)))

#define U32_QUO(a, b) \
  ((a) / 2 / (b) * 2 + ((a) - (a) / 2 / (b) * 2 * (b) >= (b)))

INLINE f32 f32_unbox(u64 x) {
  union { u32 u; f32 f; } p = { (u32)x };
  return p.f;
}

INLINE u64 f32_rewrap(f32 x) {
  union { f32 f; u32 u; } p = { x };
  return p.u;
}

INLINE u64 f32_to_u32(u64 a) {
  f32 v = f32_unbox(a);
  return v >= 0.0f && v < 4294967296.0f ? (u32)v : 0;
}

INLINE u64 nat_chk(Env e, u64 n) {
  if (n > NAT_IMM) {
    err_post(e.mem, ERR_NATS);
    return NAT_IMM;
  }
  return n;
}

INLINE u64 nat_mul(Env e, u64 a, u64 b) {
  return nat_chk(e, b != 0 && a > NAT_IMM / b ? NAT_IMM + 1 : a * b);
}

#if DEVICE

#define f32_show(e, x) (err_post(e.mem, ERR_FIDS), 0)
#define f32_read(e, s) (err_post(e.mem, ERR_FIDS), 0)

#else

static Term f32_show(Env e, Term x);
static Term f32_read(Env e, Term s);

#endif

A32_LOOP(fadd, f32_rewrap(f32_unbox(o) + f32_unbox(v)))

// Bank
// ====

// A stack of exact generations per class. The host pops and pushes at rd;
// a device pass pops below rd and pushes above top, compacted after it.

#define bank_at(H, c) ((DEV Bank*)((H) + H_BANK) + (c))

INLINE u64 bank_pop(DEV u64* H, u32 c) {
  DEV Bank* b = bank_at(H, c);
  u64 got = 0;
  LOCK(bank_lock);
  u32 t = a32_sub(&b->rd, 1);
  if ((int)t > 0) {
    got = H[b->off + t - 1];
  } else {
    a32_add(&b->rd, 1);
  }
  if (!DEVICE) {
    b->wr = b->top = b->rd;
  }
  UNLOCK(bank_lock);
  return got;
}

INLINE void bank_push(DEV u64* H, u32 c, u64 head) {
  DEV Bank* b = bank_at(H, c);
  LOCK(bank_lock);
  H[b->off + a32_add(&b->wr, 1)] = head;
  if (!DEVICE) {
    b->rd = b->top = b->wr;
  }
  UNLOCK(bank_lock);
}

// Heap
// ====

// Per lane and class: HOT, a LIFO free chain; LEN, its length in words;
// on the host COLD, one parked generation. A host free reaching KEEP_WORDS
// parks HOT as COLD and banks the old COLD. A miss takes COLD, a bank entry
// or a fresh quantum. A device lane banks its complete generations at the
// kernel end (dev_cut). The bump grows only when all of these are empty.

#define ALC_AT(e, i)   (e).alc[(i) * LANE_STEP]
#define ALC_LEN(e, c)  ALC_AT(e, NCLS_ALL + (c))
#define ALC_COLD(e, c) ALC_AT(e, 2 * NCLS_ALL + (c))
#define KEEP(c)        (KEEP_WORDS >> (c) ? KEEP_WORDS >> (c) : 1)

INLINE u32 cls_fit(u32 words) {
  return words > 1 ? 32 - CLZ(words - 1) : 0;
}

OUTLINE void heap_hand(Env e, u32 cls) {
  u64 cold = ALC_COLD(e, cls);
  if (cold) {
    bank_push(e.mem, cls, cold);
  }
  ALC_COLD(e, cls) = ALC_AT(e, cls);
  ALC_AT(e, cls)   = 0;
  ALC_LEN(e, cls)  = 0;
}

#if DEVICE
#define corpus_grow(H, n) false
#else
static bool corpus_grow(u64* H, u64 need);
#endif

OUTLINE u64 heap_alloc_miss(Env e, u32 cls) {
  DEV u64* H = e.mem;
  u64  got = 0;
  if (!DEVICE) {
    got = ALC_COLD(e, cls);
    ALC_COLD(e, cls) = 0;
  }
  if (!got) {
    got = bank_pop(H, cls);
  }
  u32 n = got ? KEEP(cls) : cls < NCLS ? QUANTUM >> cls : 1;
  if (!got) {
    u32 pages = (n << cls) >> PAGE_BITS;
    u32 p     = a32_add(a32_at(H, H_BUMP), pages);
    if ((u64)p + pages > a32_load_acq(a32_at(H, H_CAP))
      && !corpus_grow(H, (u64)p + pages)) {
      err_post(H, ERR_HEAP);
      return HEAP_OFF;
    }
    got = HEAP_OFF + ((u64)p << PAGE_BITS);
    for (u32 i = 1; i <= n; i += 1) {
      H[got + ((u64)(i - 1) << cls)] = i < n ? got + ((u64)i << cls) : 0;
    }
  }
  ALC_AT(e, cls)  = H[got];
  ALC_LEN(e, cls) = (u64)(n - 1) << cls;
  return got;
}

INLINE u64 heap_alloc(Env e, u32 cls) {
  u64 h = ALC_AT(e, cls);
  if (h) {
    ALC_AT(e, cls)   = e.mem[h];
    ALC_LEN(e, cls) -= 1ull << cls;
    return h;
  }
  return heap_alloc_miss(e, cls);
}

INLINE void heap_free(Env e, u32 cls, u64 loc) {
  if (err_seen(e.mem)) {
    return;
  }
  e.mem[loc]       = ALC_AT(e, cls);
  ALC_AT(e, cls)   = loc;
  ALC_LEN(e, cls) += 1ull << cls;
  if (!DEVICE && ALC_LEN(e, cls) >= KEEP_WORDS) {
    heap_hand(e, cls);
  }
}

INLINE void spare_free(Env e, u32 cls, u64 loc) {
  if (loc >= HEAP_OFF) {
    heap_free(e, cls, loc);
  }
}

// Term
// ====

// A static node (below the heap) is trivial, as is a captureless
// closure. A fork's Array handle (BLK_SHR: an Array binder is hot)
// is a redirect: loaded plainly, copied and dropped by a match.

#define term_make(tag, aux, loc) \
  (((u64)(tag) << 56) | ((u64)(aux) << 40) | (u64)(loc))

#define term_ctr(cid, loc) term_make(TAG_CTR, cid, loc)
#define term_pak(cid, loc) term_make(TAG_PAK, cid, loc)
#define term_clo(fid, loc) term_make(TAG_CLO, fid, loc)
#define term_buf(cls, loc) term_make(TAG_BUF, cls, loc)
#define term_tsk(fid, loc) term_make(TAG_TSK, fid, loc)

INLINE Term term_blk(bool arr, u32 cls, u64 loc) {
  return term_buf(cls, loc) | ((u64)arr << 57);
}

INLINE u64 term_tag(Term t) {
  return (t >> 56) & 0x7f;
}

INLINE bool term_rfc(Term t) {
  return (t & RFC_BIT) != 0;
}

INLINE u64 term_aux(Term t) {
  return (t >> 40) & 0xFFFF;
}

INLINE u64 term_loc(Term t) {
  return t & LOC_MASK;
}

INLINE bool term_triv(Term t) {
  return term_tag(t) <= TAG_PAK || t == TERM_HOLE || term_loc(t) < HEAP_OFF;
}

OUTLINE Term rfc_wrap(Env e, Term t, u32 cnt) {
  if (term_tag(t) == TAG_CLO || term_tag(t) == TAG_TSK) {
    err_post(e.mem, ERR_RFCS);
    return t;
  }
  u64 r = heap_alloc(e, 0);
  e.mem[r] = ((u64)term_loc(t) << 24) | cnt;
  return (t & ~LOC_MASK) | RFC_BIT | r;
}

INLINE Term rfc_seal(Env e, Term t) {
  if (term_tag(t) != TAG_CTR || term_rfc(t)) {
    return t;
  }
  return rfc_wrap(e, t, 1);
}

// A redirect cell holds its target's loc over a 24-bit count, which
// changes by atomic adds on the low half: the cell is read as two atomic
// halves, never as one plain word.
INLINE u64 rfc_view(DEV u64* H, u64 r) {
  DEV u32* w = a32_at(H, r);
  u64 cell = ((u64)a32_load(w + 1) << 32) | a32_load(w);
  if ((cell & RFC_CNT) == 1) {
    a32_acq(w);
  }
  return cell;
}

INLINE void rfc_bump(Env e, u64 r, u32 k) {
  u32 c = a32_add(a32_at(e.mem, r), k);
  if ((c & RFC_CNT) >= RFC_CNT - k) {
    err_post(e.mem, ERR_RFCS);
  }
}

INLINE Term term_keep(Env e, Term t, u32 k) {
  if (term_rfc(t)) {
    rfc_bump(e, term_loc(t), k);
    return t;
  }
  if (term_triv(t)) {
    return t;
  }
  return rfc_wrap(e, t, 1 + k);
}

INLINE u64 term_peek(DEV u64* H, Term t) {
  if (term_rfc(t)) {
    return rfc_view(H, term_loc(t)) >> 24;
  }
  return term_loc(t);
}

#define blk_shr(t) (BLK_SHR && term_rfc(t))

INLINE u64 blk_loc(DEV u64* H, Term a) {
  return BLK_SHR ? term_peek(H, a) : term_loc(a);
}

INLINE u32 blk_cls(Term t) {
  return (u32)term_aux(t) & 31;
}

#define buf_wcls(c) ((c) == 0 ? 0 : (c) - 1)

INLINE u32 blk_span(Term t) {
  u32 c = blk_cls(t);
  return term_tag(t) == TAG_ARR ? c : buf_wcls(c);
}

FAR void term_drop(Env e, Term t) {
  DEV u64* H = e.mem;
  u64  cur = 0;
  Term c0  = 0;
  u32  step = 0;
  for (;;) {
    if (!term_triv(t) && term_rfc(t)) {
      u64      r = term_loc(t);
      DEV u32* p = a32_at(H, r);
      if ((a32_sub_rel(p, 1) & RFC_CNT) != 1) {
        t = 0;
      } else {
        a32_acq(p);
        t = (t & ~(RFC_BIT | LOC_MASK)) | (H[r] >> 24);
        heap_free(e, 0, r);
      }
    }
    if (!term_triv(t)) {
      u64 tag = term_tag(t);
      if (tag == TAG_BUF) {
        heap_free(e, blk_span(t), term_loc(t));
      } else {
        u32 aux = (u32)term_aux(t);
        u64 loc = term_loc(t);
        u32 n   = tag == TAG_ARR ? 0 : tag == TAG_CTR ? cid_arity(aux)
          : fid_arity(aux) - (tag == TAG_CLO);
        u32 cls = tag == TAG_ARR ? 64 | blk_cls(t)
          : n > 247 ? 64 | (n - 240)
          : cls_fit(tag == TAG_TSK ? n + 2 : n);
        c0 = H[loc];
        H[loc] = cur;
        cur = loc | ((u64)n << 48) | ((u64)cls << 56);
      }
    }
    for (;;) {
      if (err_spun(H, &step)) {
        return;
      }
      if (cur == 0) {
        return;
      }
      u64  loc = cur & LOC_MASK;
      u32  i   = (u8)(cur >> 40);
      u32  n   = (u8)(cur >> 48);
      u32  cls = (u32)(cur >> 56);
      bool arr = cls > 63;
      u32  j   = i;
      if (arr) {
        cls &= 63;
        n   = 1u << cls;
        if (i == 2) {
          j = (u32)H[loc + 1];
        }
      }
      if (j < n) {
        Term c = j == 0 ? c0 : H[loc + j];
        if (arr && j > 0) {
          H[loc + 1] = j + 1;
        }
        if (!arr || i < 2) {
          cur += 1ull << 40;
        }
        if (!term_triv(c)) {
          t = c;
          break;
        }
      } else {
        u64 up = H[loc];
        heap_free(e, cls, loc);
        cur = up;
      }
    }
  }
}

INLINE void term_sink(Env e, Term t) {
  if (!term_triv(t)) {
    term_drop(e, t);
  }
}

OUTLINE void span_fade(Env e, Term t, u64 src, u32 n) {
  for (u32 j = 0; j < n; j += 1) {
    Term f = e.mem[src + j];
    if (term_rfc(f)) {
      rfc_bump(e, term_loc(f), 1);
    } else if (!term_triv(f)) {
      err_post(e.mem, ERR_RFCS);
    }
  }
  term_drop(e, t);
}

INLINE u64 ctr_take(Env e, Term t, u32 n, THR Term* out) {
  DEV u64* H = e.mem;
  if (!term_rfc(t)) {
    for (u32 j = 0; j < n; j += 1) {
      out[j] = H[term_loc(t) + j];
    }
    return term_loc(t);
  }
  u64 r    = term_loc(t);
  u64 cell = rfc_view(H, r);
  u64 src  = cell >> 24;
  for (u32 j = 0; j < n; j += 1) {
    out[j] = H[src + j];
  }
  if ((cell & RFC_CNT) == 1) {
    heap_free(e, 0, r);
    return src;
  }
  span_fade(e, t, src, n);
  return 0;
}

INLINE Term term_word(Env e, Term w) {
  u32 x = 0;
  Term t = w;
  for (u32 i = 0; i < 32 && term_aux(t) == CID_WCON; i += 1) {
    u64 l = term_peek(e.mem, t);
    x |= (u32)(e.mem[l] & 1) << i;
    t = e.mem[l + 1];
  }
  term_sink(e, w);
  return x;
}

// Blk
// ===

// A block owns one allocation in its class (an ARR 2^c Terms, a BUF 2^c
// u32). Matching ANode is blk_half twice (the high call frees the source);
// ANode{l, r} is blk_node; Array.clone is blk_copy.

#define BLK_ALLOC(n, w) \
  u64 n = heap_alloc(e, w); \
  if (err_seen(e.mem)) { \
    return term_buf(0, n); \
  }

INLINE DEV u32a* blk_ptr(DEV u64* H, u64 loc, u32 i) {
  return (DEV u32a*)(H + loc) + i;
}

INLINE Term blk_read(DEV u64* H, bool arr, u64 loc, u32 i) {
  if (arr) {
    return H[loc + i];
  }
  return (u64)*blk_ptr(H, loc, i);
}

INLINE void blk_write(DEV u64* H, bool arr, u64 loc, u32 i, Term v) {
  if (arr) {
    H[loc + i] = v;
  } else {
    *blk_ptr(H, loc, i) = (u32)v;
  }
}

INLINE u32 blk_at(Term a, u64 i, u32 lgs) {
  return ((u32)i & (u32)((1ull << (blk_cls(a) - lgs)) - 1)) << lgs;
}

INLINE Term blk_keep(Env e, u64 at) {
  Term w = e.mem[at];
  Term v = term_keep(e, w, 1);
  if (v != w) {
    e.mem[at] = v;
  }
  return v;
}

INLINE void blk_fill(Env e, u64 dst, u64 src, u64 n, bool keep) {
  for (u64 j = 0; j < n; j += 1) {
    e.mem[dst + j] = keep ? blk_keep(e, src + j) : e.mem[src + j];
  }
}

INLINE void blk_free(Env e, Term t) {
  blk_shr(t) ? term_drop(e, t) : heap_free(e, blk_span(t), term_loc(t));
}

OUTLINE Term blk_copy(Env e, Term a) {
  bool arr = term_tag(a) == TAG_ARR;
  u32 cls = blk_span(a);
  BLK_ALLOC(dst, cls)
  blk_fill(e, dst, blk_loc(e.mem, a), 1ull << cls, arr);
  return term_blk(arr, blk_cls(a), dst);
}

INLINE Term blk_node(Env e, Term l, Term r) {
  DEV u64* H = e.mem;
  bool arr = term_tag(l) == TAG_ARR;
  u32 c = blk_cls(l);
  if (c != blk_cls(r) || c + 1 >= NCLS_ALL) {
    err_post(H, ERR_TAGS);
    return l;
  }
  u64 pl = blk_loc(H, l);
  u64 pr = blk_loc(H, r);
  BLK_ALLOC(n, arr ? c + 1 : c)
  if (!arr && c == 0) {
    H[n] = (u64)*blk_ptr(H, pl, 0) | ((u64)*blk_ptr(H, pr, 0) << 32);
  } else {
    u64 cw = 1ull << blk_span(l);
    blk_fill(e, n, pl, cw, arr && blk_shr(l));
    blk_fill(e, n + cw, pr, cw, arr && blk_shr(r));
  }
  blk_free(e, l);
  blk_free(e, r);
  return term_blk(arr, c + 1, n);
}

INLINE Term blk_half(Env e, Term a, u32 hi) {
  DEV u64* H = e.mem;
  bool arr = term_tag(a) == TAG_ARR;
  u32 c = blk_cls(a);
  if (c == 0) {
    err_post(H, ERR_TAGS);
    return a;
  }
  c -= 1;
  u32 cw = arr ? c : buf_wcls(c);
  u64 src = blk_loc(H, a);
  BLK_ALLOC(n, cw)
  if (!arr && c == 0) {
    H[n] = (u64)*blk_ptr(H, src, hi);
  } else {
    blk_fill(e, n, src + ((u64)hi << cw), 1ull << cw, arr && blk_shr(a));
  }
  if (hi) {
    blk_free(e, a);
  }
  return term_blk(arr, c, n);
}

INLINE Term blk_new(Env e, bool arr, u64 d, u32 lgs, u32 n, THR Term* v) {
  DEV u64* H = e.mem;
  if (d + lgs > 31) {
    err_post(H, ERR_ARRS);
    d = 0;
  }
  u32 c = (u32)d + lgs;
  BLK_ALLOC(l, arr ? c : buf_wcls(c))
  for (u32 j = 0; arr && d > 0 && j < n; j += 1) {
    if (d >= 24 && !term_triv(v[j])) {
      err_post(H, ERR_RFCS);
    }
    v[j] = term_keep(e, v[j], (1u << d) - 1);
  }
  for (u64 i = 0; i < (1ull << c); i += 1) {
    blk_write(H, arr, l, (u32)i, i % (1u << lgs) < n ? v[i % (1u << lgs)] : 0);
  }
  return term_blk(arr, c, l);
}

// Ring
// ====

// planes LANES wide: a smaller bag has deeper rings in the same region
#define ring_word(H, r, w) ((H) + RING_OFF + (w) * LANES + (r))
#define ring_slot(H, r, p) ring_word(H, r, (p) & (RING_LEN - 1))
#define ring_get(H, r)     ((DEV u32*)ring_word(H, r, RING_LEN))
#define ring_put(H, r)     ((DEV u32*)ring_word(H, r, RING_LEN + 1))

INLINE u32 ring_lap(u32 pos) {
  return ~(u32)(pos / RING_LEN) & 1;
}

INLINE void ring_push(DEV u64* H, u32 r, Term tsk) {
  u32 pos = a32_add(ring_put(H, r), 1);
  if (pos - a32_load(ring_get(H, r)) >= RING_LEN) {
    err_post(H, ERR_RING);
    return;
  }
  DEV u32* lo = (DEV u32*)ring_slot(H, r, pos);
  a32_store(lo, (u32)tsk);
  a32_store_rel(lo + 1, (u32)(tsk >> 32) | (ring_lap(pos) << 31));
}

INLINE u32 ring_flip(u32 i) {
  return (i % CUBE_T << CUBE_LOG) + i / CUBE_T;
}

#define ring_pick(b, s, c) ((b) + (s) * (a32_add(c, 1) & (CUBE_T - 1)))

// Task
// ====

INLINE u64 task_node(Env e, u32 fid, Term cont, u32 idx, u32 rem) {
  u32 ar  = fid_arity(fid);
  u64 loc = heap_alloc(e, cls_fit(ar + 2));
  for (u32 i = 0; rem && i < ar; i += 1) {
    e.mem[loc + i] = TERM_HOLE;
  }
  e.mem[loc + ar]     = cont;
  e.mem[loc + ar + 1] = ((u64)idx << 32) | rem;
  return loc;
}

INLINE u64 task_tail(Term t) {
  return term_loc(t) + fid_arity((u32)term_aux(t));
}

INLINE Term task_deliver(DEV u64* H, Term cont, u32 idx, THR Term* v, u32 n) {
  u64 at = cont == TERM_HOLE ? H_ROOT_WORD : term_loc(cont) + idx;
  for (u32 j = 0; j < WL_RESW; j += 1) {
    if (j < n) {
      H[at + j] = v[j];
    }
  }
  if (cont == TERM_HOLE) {
    a32_store_rel(a32_at(H, H_ROOT_DONE), n + 1);
    return 0;
  }
  u64 tl = task_tail(cont);
  if (a32_sub_rel(a32_at(H, tl + 1), 1) == 1) {
    a32_acq(a32_at(H, tl + 1));
    return cont;
  }
  return 0;
}

INLINE void task_deal(DEV u64* H, Term join, u32 base, u32 stride, TG u32* cur) {
  u64 loc = term_loc(join);
  u32 ar  = fid_arity((u32)term_aux(join));
  u32 g   = 0;
  if (stride == 0) {
    u32 rem = (u32)H[loc + ar + 1];
    g = a32_add(a32_at(H, H_CURSOR), rem);
  }
  for (u32 i = 0; i < ar; i += 1) {
    Term k = H[loc + i];
    if (term_tag(k) == TAG_TSK) {
      H[loc + i] = TERM_HOLE;
      u32 to;
      if (stride != 0) {
        to = ring_pick(base, stride, cur);
      } else {
        to = ring_flip(g & (u32)(LANES - 1));
        g += 1;
      }
      ring_push(H, to, k);
    }
  }
}

// Root
// ====

INLINE bool root_done(DEV u64* H) {
  return a32_load_acq(a32_at(H, H_ROOT_DONE)) != 0;
}

static u32 root_take(DEV u64* H, THR Term* v) {
  u32 n = a32_load_acq(a32_at(H, H_ROOT_DONE)) - 1;
  for (u32 j = 0; j < n; j += 1) {
    v[j] = H[H_ROOT_WORD + j];
  }
  a32_store(a32_at(H, H_ROOT_DONE), 0);
  return n;
}

// Spins
// =====

CONSTV u64 STAT_IMG[] = { 10ull, term_pak(CID_SNIL, 0), 100ull, term_ctr(CID_SCON, STAT_OFF + 0), 105ull, term_ctr(CID_SCON, STAT_OFF + 2), 100ull, term_ctr(CID_SCON, STAT_OFF + 4), 97ull, term_ctr(CID_SCON, STAT_OFF + 6), 98ull, term_ctr(CID_SCON, STAT_OFF + 8), 32ull, term_ctr(CID_SCON, STAT_OFF + 10), 121ull, term_ctr(CID_SCON, STAT_OFF + 0), 116ull, term_ctr(CID_SCON, STAT_OFF + 14), 112ull, term_ctr(CID_SCON, STAT_OFF + 16), 109ull, term_ctr(CID_SCON, STAT_OFF + 18), 101ull, term_ctr(CID_SCON, STAT_OFF + 20), 32ull, term_ctr(CID_SCON, STAT_OFF + 22), 103ull, term_ctr(CID_SCON, STAT_OFF + 0), 110ull, term_ctr(CID_SCON, STAT_OFF + 26), 105ull, term_ctr(CID_SCON, STAT_OFF + 28), 115ull, term_ctr(CID_SCON, STAT_OFF + 30), 115ull, term_ctr(CID_SCON, STAT_OFF + 32), 105ull, term_ctr(CID_SCON, STAT_OFF + 34), 109ull, term_ctr(CID_SCON, STAT_OFF + 36), 32ull, term_ctr(CID_SCON, STAT_OFF + 38), 101ull, term_pak(CID_SNIL, 0), 103ull, term_ctr(CID_SCON, STAT_OFF + 42), 110ull, term_ctr(CID_SCON, STAT_OFF + 44), 97ull, term_ctr(CID_SCON, STAT_OFF + 46), 114ull, term_ctr(CID_SCON, STAT_OFF + 48), 32ull, term_ctr(CID_SCON, STAT_OFF + 50), 102ull, term_ctr(CID_SCON, STAT_OFF + 52), 111ull, term_ctr(CID_SCON, STAT_OFF + 54), 32ull, term_ctr(CID_SCON, STAT_OFF + 56), 116ull, term_ctr(CID_SCON, STAT_OFF + 58), 117ull, term_ctr(CID_SCON, STAT_OFF + 60), 111ull, term_ctr(CID_SCON, STAT_OFF + 62), 32ull, term_ctr(CID_SCON, STAT_OFF + 64), 115ull, term_ctr(CID_SCON, STAT_OFF + 66), 114ull, term_ctr(CID_SCON, STAT_OFF + 68), 101ull, term_ctr(CID_SCON, STAT_OFF + 70), 98ull, term_ctr(CID_SCON, STAT_OFF + 72), 109ull, term_ctr(CID_SCON, STAT_OFF + 74), 117ull, term_ctr(CID_SCON, STAT_OFF + 76), 110ull, term_ctr(CID_SCON, STAT_OFF + 78), 32ull, term_ctr(CID_SCON, STAT_OFF + 80), 82ull, term_ctr(CID_SCON, STAT_OFF + 82), 32ull, term_ctr(CID_SCON, STAT_OFF + 84), 58ull, term_ctr(CID_SCON, STAT_OFF + 86), 101ull, term_ctr(CID_SCON, STAT_OFF + 88), 114ull, term_ctr(CID_SCON, STAT_OFF + 90), 111ull, term_ctr(CID_SCON, STAT_OFF + 92), 99ull, term_ctr(CID_SCON, STAT_OFF + 94), 45ull, term_ctr(CID_SCON, STAT_OFF + 96), 104ull, term_ctr(CID_SCON, STAT_OFF + 98), 112ull, term_ctr(CID_SCON, STAT_OFF + 100), 101ull, term_ctr(CID_SCON, STAT_OFF + 102), 104ull, term_ctr(CID_SCON, STAT_OFF + 104), 115ull, term_pak(CID_SNIL, 0), 103ull, term_ctr(CID_SCON, STAT_OFF + 108), 110ull, term_ctr(CID_SCON, STAT_OFF + 110), 105ull, term_ctr(CID_SCON, STAT_OFF + 112), 116ull, term_ctr(CID_SCON, STAT_OFF + 114), 115ull, term_ctr(CID_SCON, STAT_OFF + 116), 111ull, term_ctr(CID_SCON, STAT_OFF + 118), 112ull, term_ctr(CID_SCON, STAT_OFF + 120), 32ull, term_ctr(CID_SCON, STAT_OFF + 122), 82ull, term_ctr(CID_SCON, STAT_OFF + 124), 32ull, term_ctr(CID_SCON, STAT_OFF + 126), 100ull, term_ctr(CID_SCON, STAT_OFF + 128), 97ull, term_ctr(CID_SCON, STAT_OFF + 130), 98ull, term_ctr(CID_SCON, STAT_OFF + 132), 32ull, term_ctr(CID_SCON, STAT_OFF + 134), 58ull, term_ctr(CID_SCON, STAT_OFF + 136), 101ull, term_ctr(CID_SCON, STAT_OFF + 138), 114ull, term_ctr(CID_SCON, STAT_OFF + 140), 111ull, term_ctr(CID_SCON, STAT_OFF + 142), 99ull, term_ctr(CID_SCON, STAT_OFF + 144), 45ull, term_ctr(CID_SCON, STAT_OFF + 146), 104ull, term_ctr(CID_SCON, STAT_OFF + 148), 112ull, term_ctr(CID_SCON, STAT_OFF + 150), 101ull, term_ctr(CID_SCON, STAT_OFF + 152), 104ull, term_ctr(CID_SCON, STAT_OFF + 154), 100ull, term_pak(CID_SNIL, 0), 97ull, term_ctr(CID_SCON, STAT_OFF + 158), 111ull, term_ctr(CID_SCON, STAT_OFF + 160), 108ull, term_ctr(CID_SCON, STAT_OFF + 162), 121ull, term_ctr(CID_SCON, STAT_OFF + 164), 97ull, term_ctr(CID_SCON, STAT_OFF + 166), 112ull, term_ctr(CID_SCON, STAT_OFF + 168), 32ull, term_ctr(CID_SCON, STAT_OFF + 170), 81ull, term_ctr(CID_SCON, STAT_OFF + 172), 32ull, term_ctr(CID_SCON, STAT_OFF + 174), 100ull, term_ctr(CID_SCON, STAT_OFF + 176), 101ull, term_ctr(CID_SCON, STAT_OFF + 178), 116ull, term_ctr(CID_SCON, STAT_OFF + 180), 97ull, term_ctr(CID_SCON, STAT_OFF + 182), 99ull, term_ctr(CID_SCON, STAT_OFF + 184), 110ull, term_ctr(CID_SCON, STAT_OFF + 186), 117ull, term_ctr(CID_SCON, STAT_OFF + 188), 114ull, term_ctr(CID_SCON, STAT_OFF + 190), 116ull, term_ctr(CID_SCON, STAT_OFF + 192), 32ull, term_ctr(CID_SCON, STAT_OFF + 194), 58ull, term_ctr(CID_SCON, STAT_OFF + 196), 101ull, term_ctr(CID_SCON, STAT_OFF + 198), 114ull, term_ctr(CID_SCON, STAT_OFF + 200), 111ull, term_ctr(CID_SCON, STAT_OFF + 202), 99ull, term_ctr(CID_SCON, STAT_OFF + 204), 45ull, term_ctr(CID_SCON, STAT_OFF + 206), 104ull, term_ctr(CID_SCON, STAT_OFF + 208), 112ull, term_ctr(CID_SCON, STAT_OFF + 210), 101ull, term_ctr(CID_SCON, STAT_OFF + 212), 104ull, term_ctr(CID_SCON, STAT_OFF + 214), 69ull, term_ctr(CID_SCON, STAT_OFF + 172), 32ull, term_ctr(CID_SCON, STAT_OFF + 218), 100ull, term_ctr(CID_SCON, STAT_OFF + 220), 101ull, term_ctr(CID_SCON, STAT_OFF + 222), 116ull, term_ctr(CID_SCON, STAT_OFF + 224), 97ull, term_ctr(CID_SCON, STAT_OFF + 226), 99ull, term_ctr(CID_SCON, STAT_OFF + 228), 110ull, term_ctr(CID_SCON, STAT_OFF + 230), 117ull, term_ctr(CID_SCON, STAT_OFF + 232), 114ull, term_ctr(CID_SCON, STAT_OFF + 234), 116ull, term_ctr(CID_SCON, STAT_OFF + 236), 32ull, term_ctr(CID_SCON, STAT_OFF + 238), 58ull, term_ctr(CID_SCON, STAT_OFF + 240), 101ull, term_ctr(CID_SCON, STAT_OFF + 242), 114ull, term_ctr(CID_SCON, STAT_OFF + 244), 111ull, term_ctr(CID_SCON, STAT_OFF + 246), 99ull, term_ctr(CID_SCON, STAT_OFF + 248), 45ull, term_ctr(CID_SCON, STAT_OFF + 250), 104ull, term_ctr(CID_SCON, STAT_OFF + 252), 112ull, term_ctr(CID_SCON, STAT_OFF + 254), 101ull, term_ctr(CID_SCON, STAT_OFF + 256), 104ull, term_ctr(CID_SCON, STAT_OFF + 258), 114ull, term_pak(CID_SNIL, 0), 101ull, term_ctr(CID_SCON, STAT_OFF + 262), 100ull, term_ctr(CID_SCON, STAT_OFF + 264), 97ull, term_ctr(CID_SCON, STAT_OFF + 266), 101ull, term_ctr(CID_SCON, STAT_OFF + 268), 104ull, term_ctr(CID_SCON, STAT_OFF + 270), 32ull, term_ctr(CID_SCON, STAT_OFF + 272), 101ull, term_ctr(CID_SCON, STAT_OFF + 274), 109ull, term_ctr(CID_SCON, STAT_OFF + 276), 97ull, term_ctr(CID_SCON, STAT_OFF + 278), 114ull, term_ctr(CID_SCON, STAT_OFF + 280), 102ull, term_ctr(CID_SCON, STAT_OFF + 282), 32ull, term_ctr(CID_SCON, STAT_OFF + 284), 100ull, term_ctr(CID_SCON, STAT_OFF + 286), 97ull, term_ctr(CID_SCON, STAT_OFF + 288), 98ull, term_ctr(CID_SCON, STAT_OFF + 290), 32ull, term_ctr(CID_SCON, STAT_OFF + 292), 58ull, term_ctr(CID_SCON, STAT_OFF + 294), 101ull, term_ctr(CID_SCON, STAT_OFF + 296), 114ull, term_ctr(CID_SCON, STAT_OFF + 298), 111ull, term_ctr(CID_SCON, STAT_OFF + 300), 99ull, term_ctr(CID_SCON, STAT_OFF + 302), 45ull, term_ctr(CID_SCON, STAT_OFF + 304), 104ull, term_ctr(CID_SCON, STAT_OFF + 306), 112ull, term_ctr(CID_SCON, STAT_OFF + 308), 101ull, term_ctr(CID_SCON, STAT_OFF + 310), 104ull, term_ctr(CID_SCON, STAT_OFF + 312), 68ull, term_ctr(CID_SCON, STAT_OFF + 172), 32ull, term_ctr(CID_SCON, STAT_OFF + 316), 100ull, term_ctr(CID_SCON, STAT_OFF + 318), 101ull, term_ctr(CID_SCON, STAT_OFF + 320), 116ull, term_ctr(CID_SCON, STAT_OFF + 322), 97ull, term_ctr(CID_SCON, STAT_OFF + 324), 99ull, term_ctr(CID_SCON, STAT_OFF + 326), 110ull, term_ctr(CID_SCON, STAT_OFF + 328), 117ull, term_ctr(CID_SCON, STAT_OFF + 330), 114ull, term_ctr(CID_SCON, STAT_OFF + 332), 116ull, term_ctr(CID_SCON, STAT_OFF + 334), 32ull, term_ctr(CID_SCON, STAT_OFF + 336), 58ull, term_ctr(CID_SCON, STAT_OFF + 338), 101ull, term_ctr(CID_SCON, STAT_OFF + 340), 114ull, term_ctr(CID_SCON, STAT_OFF + 342), 111ull, term_ctr(CID_SCON, STAT_OFF + 344), 99ull, term_ctr(CID_SCON, STAT_OFF + 346), 45ull, term_ctr(CID_SCON, STAT_OFF + 348), 104ull, term_ctr(CID_SCON, STAT_OFF + 350), 112ull, term_ctr(CID_SCON, STAT_OFF + 352), 101ull, term_ctr(CID_SCON, STAT_OFF + 354), 104ull, term_ctr(CID_SCON, STAT_OFF + 356), 103ull, term_pak(CID_SNIL, 0), 97ull, term_ctr(CID_SCON, STAT_OFF + 360), 116ull, term_ctr(CID_SCON, STAT_OFF + 362), 32ull, term_ctr(CID_SCON, STAT_OFF + 364), 101ull, term_ctr(CID_SCON, STAT_OFF + 366), 109ull, term_ctr(CID_SCON, STAT_OFF + 368), 97ull, term_ctr(CID_SCON, STAT_OFF + 370), 114ull, term_ctr(CID_SCON, STAT_OFF + 372), 102ull, term_ctr(CID_SCON, STAT_OFF + 374), 32ull, term_ctr(CID_SCON, STAT_OFF + 376), 100ull, term_ctr(CID_SCON, STAT_OFF + 378), 97ull, term_ctr(CID_SCON, STAT_OFF + 380), 98ull, term_ctr(CID_SCON, STAT_OFF + 382), 32ull, term_ctr(CID_SCON, STAT_OFF + 384), 58ull, term_ctr(CID_SCON, STAT_OFF + 386), 101ull, term_ctr(CID_SCON, STAT_OFF + 388), 114ull, term_ctr(CID_SCON, STAT_OFF + 390), 111ull, term_ctr(CID_SCON, STAT_OFF + 392), 99ull, term_ctr(CID_SCON, STAT_OFF + 394), 45ull, term_ctr(CID_SCON, STAT_OFF + 396), 104ull, term_ctr(CID_SCON, STAT_OFF + 398), 112ull, term_ctr(CID_SCON, STAT_OFF + 400), 101ull, term_ctr(CID_SCON, STAT_OFF + 402), 104ull, term_ctr(CID_SCON, STAT_OFF + 404), 101ull, term_ctr(CID_SCON, STAT_OFF + 108), 109ull, term_ctr(CID_SCON, STAT_OFF + 408), 97ull, term_ctr(CID_SCON, STAT_OFF + 410), 114ull, term_ctr(CID_SCON, STAT_OFF + 412), 102ull, term_ctr(CID_SCON, STAT_OFF + 414), 32ull, term_ctr(CID_SCON, STAT_OFF + 416), 121ull, term_ctr(CID_SCON, STAT_OFF + 418), 110ull, term_ctr(CID_SCON, STAT_OFF + 420), 97ull, term_ctr(CID_SCON, STAT_OFF + 422), 109ull, term_ctr(CID_SCON, STAT_OFF + 424), 32ull, term_ctr(CID_SCON, STAT_OFF + 426), 111ull, term_ctr(CID_SCON, STAT_OFF + 428), 111ull, term_ctr(CID_SCON, STAT_OFF + 430), 116ull, term_ctr(CID_SCON, STAT_OFF + 432), 32ull, term_ctr(CID_SCON, STAT_OFF + 434), 58ull, term_ctr(CID_SCON, STAT_OFF + 436), 101ull, term_ctr(CID_SCON, STAT_OFF + 438), 114ull, term_ctr(CID_SCON, STAT_OFF + 440), 111ull, term_ctr(CID_SCON, STAT_OFF + 442), 99ull, term_ctr(CID_SCON, STAT_OFF + 444), 45ull, term_ctr(CID_SCON, STAT_OFF + 446), 104ull, term_ctr(CID_SCON, STAT_OFF + 448), 112ull, term_ctr(CID_SCON, STAT_OFF + 450), 101ull, term_ctr(CID_SCON, STAT_OFF + 452), 104ull, term_ctr(CID_SCON, STAT_OFF + 454) };

INLINE Term spin_0(Env e, THR Term* o, u32 r0, Term r1, u32 r2, Term r3, u32 r4, Term r5) {
  u32 wpoll = 0;
  u32 _v_6 = 0;
  Term _v_7 = 0;
  Term _v_8 = 0;
  u32 _v_9 = 0;
  Term _v_10 = 0;
  u32 _v_11 = 0;
  u32 _eq_0 = r0;
  Term _ce_0 = r1;
  u32 _ce_1 = r2;
  Term _cq_2 = r3;
  u32 _cq_3 = r4;
  Term _a_0 = r5;
  WL_SPIN
    if (_eq_0 == 1) {
      _v_6 = 1;
      _v_7 = _a_0;
      _v_8 = _ce_0;
      _v_9 = _ce_1;
      _v_10 = _cq_2;
      _v_11 = _cq_3;
    } else {
      _v_6 = 0;
      _v_7 = _a_0;
      _v_8 = 0;
      _v_9 = 0;
      _v_10 = 0;
      _v_11 = 0;
    }
  break;
  }
  o[0] = _v_6;
  o[1] = _v_7;
  o[2] = _v_8;
  o[3] = _v_9;
  o[4] = _v_10;
  o[5] = _v_11;
  return 1;
}

INLINE Term spin_5(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_40 = 0;
  Term _qr_0 = r0;
  Term _qr_1 = r1;
  WL_SPIN
    _v_40 = _qr_0;
  break;
  }
  o[0] = _v_40;
  return 1;
}

INLINE Term spin_4(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_38 = 0;
  Term _a_3 = r0;
  Term _b_0 = r1;
  WL_SPIN
    Term _a_4 = (_b_0 == 0 ? 0 : _a_3 / _b_0);
    Term _a_5 = (_b_0 == 0 ? _a_3 : _a_3 % _b_0);
    Term _v_39 = 0;
    Term _o_0[1];
    if (spin_5(e, _o_0, _a_4, _a_5) == 0) {
      return 0;
    }
    _v_39 = _o_0[0];
    _v_38 = _v_39;
  break;
  }
  o[0] = _v_38;
  return 1;
}

INLINE Term spin_8(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_49 = 0;
  Term _qr_2 = r0;
  Term _qr_3 = r1;
  WL_SPIN
    _v_49 = _qr_3;
  break;
  }
  o[0] = _v_49;
  return 1;
}

INLINE Term spin_7(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_47 = 0;
  Term _a_6 = r0;
  Term _b_1 = r1;
  WL_SPIN
    Term _a_7 = (_b_1 == 0 ? 0 : _a_6 / _b_1);
    Term _a_8 = (_b_1 == 0 ? _a_6 : _a_6 % _b_1);
    Term _v_48 = 0;
    Term _o_2[1];
    if (spin_8(e, _o_2, _a_7, _a_8) == 0) {
      return 0;
    }
    _v_48 = _o_2[0];
    _v_47 = _v_48;
  break;
  }
  o[0] = _v_47;
  return 1;
}

INLINE Term spin_6(Env e, THR Term* o, Term r0, Term r1, u32 r2) {
  u32 wpoll = 0;
  Term _v_43 = 0;
  u32 _v_44 = 0;
  Term _p_2 = r0;
  Term _r_5 = r1;
  u32 _r_6 = r2;
  WL_SPIN
    Term _v_45 = 0;
    Term _v_46 = 0;
    Term _o_3[1];
    if (spin_7(e, _o_3, _p_2, 4ull) == 0) {
      return 0;
    }
    _v_46 = _o_3[0];
    _v_45 = _v_46;
    Term _a_9 = nat_mul(e, 8ull, _v_45);
    _v_43 = _r_5;
    _v_44 = U32_BIN((_a_9 >= 32 ? 0 : U32_BIN(_r_6, >>, _a_9)), &, 255ull);
  break;
  }
  o[0] = _v_43;
  o[1] = _v_44;
  return 1;
}

INLINE Term spin_3(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_34 = 0;
  u32 _v_35 = 0;
  Term _a_2 = r0;
  Term _p_1 = r1;
  WL_SPIN
    Term _v_36 = 0;
    Term _v_37 = 0;
    Term _o_1[1];
    if (spin_4(e, _o_1, _p_1, 4ull) == 0) {
      return 0;
    }
    _v_37 = _o_1[0];
    _v_36 = _v_37;
    Term _at_0 = blk_loc(e.mem, _a_2);
    Term _at_1 = blk_at(_a_2, ((u64)(u32)(_v_36)), 0);
    u32 _c_2 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    Term _v_41 = 0;
    u32 _v_42 = 0;
    Term _o_4[2];
    if (spin_6(e, _o_4, _p_1, _a_2, _c_2) == 0) {
      return 0;
    }
    _v_41 = _o_4[0];
    _v_42 = _o_4[1];
    _v_34 = _v_41;
    _v_35 = _v_42;
  break;
  }
  o[0] = _v_34;
  o[1] = _v_35;
  return 1;
}

INLINE Term spin_11(Env e, THR Term* o, u32 r0, u32 r1) {
  u32 wpoll = 0;
  u32 _v_65 = 0;
  u32 _a_10 = r0;
  u32 _b_3 = r1;
  WL_SPIN
    if (_a_10 == 0) {
      _v_65 = 0;
    } else {
      _v_65 = _b_3;
    }
  break;
  }
  o[0] = _v_65;
  return 1;
}

INLINE Term spin_10(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_62 = 0;
  u32 _b_2 = r0;
  WL_SPIN
    u32 _v_63 = 0;
    u32 _v_64 = 0;
    Term _o_6[1];
    if (spin_11(e, _o_6, U32_BIN(_b_2, >=, 9ull), U32_BIN(_b_2, <=, 13ull)) == 0) {
      return 0;
    }
    _v_64 = _o_6[0];
    _v_63 = _v_64;
    _v_62 = ((U32_BIN(_b_2, ==, 32ull)) | (_v_63));
  break;
  }
  o[0] = _v_62;
  return 1;
}

INLINE Term spin_12(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_68 = 0;
  u32 _b_4 = r0;
  WL_SPIN
    _v_68 = U32_BIN(U32_BIN(_b_4, &, 192ull), ==, 128ull);
  break;
  }
  o[0] = _v_68;
  return 1;
}

INLINE Term spin_9(Env e, THR Term* o, Term r0, u32 r1) {
  u32 wpoll = 0;
  Term _v_55 = 0;
  u32 _v_56 = 0;
  u32 _v_57 = 0;
  u32 _v_58 = 0;
  u32 _v_59 = 0;
  Term _r_7 = r0;
  u32 _r_8 = r1;
  WL_SPIN
    u32 _v_60 = 0;
    u32 _v_61 = 0;
    Term _o_7[1];
    if (spin_10(e, _o_7, _r_8) == 0) {
      return 0;
    }
    _v_61 = _o_7[0];
    _v_60 = _v_61;
    u32 _v_66 = 0;
    u32 _v_67 = 0;
    Term _o_8[1];
    if (spin_12(e, _o_8, _r_8) == 0) {
      return 0;
    }
    _v_67 = _o_8[0];
    _v_66 = _v_67;
    _v_55 = _r_7;
    _v_56 = _r_8;
    _v_57 = _v_60;
    _v_58 = _v_66;
    _v_59 = U32_BIN(_r_8, ==, 10ull);
  break;
  }
  o[0] = _v_55;
  o[1] = _v_56;
  o[2] = _v_57;
  o[3] = _v_58;
  o[4] = _v_59;
  return 1;
}

INLINE Term spin_2(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_25 = 0;
  u32 _v_26 = 0;
  u32 _v_27 = 0;
  u32 _v_28 = 0;
  u32 _v_29 = 0;
  Term _a_1 = r0;
  Term _p_0 = r1;
  WL_SPIN
    Term _v_30 = 0;
    u32 _v_31 = 0;
    Term _v_32 = 0;
    u32 _v_33 = 0;
    Term _o_5[2];
    if (spin_3(e, _o_5, _a_1, _p_0) == 0) {
      return 0;
    }
    _v_32 = _o_5[0];
    _v_33 = _o_5[1];
    _v_30 = _v_32;
    _v_31 = _v_33;
    Term _v_50 = 0;
    u32 _v_51 = 0;
    u32 _v_52 = 0;
    u32 _v_53 = 0;
    u32 _v_54 = 0;
    Term _o_9[5];
    if (spin_9(e, _o_9, _v_30, _v_31) == 0) {
      return 0;
    }
    _v_50 = _o_9[0];
    _v_51 = _o_9[1];
    _v_52 = _o_9[2];
    _v_53 = _o_9[3];
    _v_54 = _o_9[4];
    _v_25 = _v_50;
    _v_26 = _v_51;
    _v_27 = _v_52;
    _v_28 = _v_53;
    _v_29 = _v_54;
  break;
  }
  o[0] = _v_25;
  o[1] = _v_26;
  o[2] = _v_27;
  o[3] = _v_28;
  o[4] = _v_29;
  return 1;
}

INLINE Term spin_14(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_81 = 0;
  u32 _st_1 = r0;
  WL_SPIN
    if (_st_1 == 0) {
      _v_81 = 0;
    } else if (_st_1 == 1) {
      _v_81 = 2;
    } else {
      _v_81 = 2;
    }
  break;
  }
  o[0] = _v_81;
  return 1;
}

INLINE Term spin_15(Env e, THR Term* o, u32 r0, Term r1, u32 r2) {
  u32 wpoll = 0;
  u32 _v_100 = 0;
  u32 _v_101 = 0;
  Term _v_102 = 0;
  u32 _v_103 = 0;
  u32 _st_2 = r0;
  Term _p_4 = r1;
  u32 _b_5 = r2;
  WL_SPIN
    if (_st_2 == 0) {
      _v_100 = 1;
      _v_101 = _b_5;
      _v_102 = nat_chk(e, _p_4 + 1);
      _v_103 = 1;
    } else if (_st_2 == 1) {
      _v_100 = 1;
      _v_101 = _b_5;
      _v_102 = nat_chk(e, _p_4 + 1);
      _v_103 = 1;
    } else {
      _v_100 = 1;
      _v_101 = 32ull;
      _v_102 = _p_4;
      _v_103 = 0;
    }
  break;
  }
  o[0] = _v_100;
  o[1] = _v_101;
  o[2] = _v_102;
  o[3] = _v_103;
  return 1;
}

INLINE Term spin_13(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, u32 r4, Term r5, u32 r6, u32 r7, u32 r8, u32 r9) {
  u32 wpoll = 0;
  Term _v_74 = 0;
  u32 _v_75 = 0;
  u32 _v_76 = 0;
  Term _v_77 = 0;
  u32 _v_78 = 0;
  Term _fuel_0 = r0;
  u32 _inr_0 = r1;
  Term _p_3 = r2;
  Term _e_1 = r3;
  u32 _st_0 = r4;
  Term _x_0 = r5;
  u32 _x_1 = r6;
  u32 _x_2 = r7;
  u32 _x_3 = r8;
  u32 _x_4 = r9;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      _v_74 = _x_0;
      _v_75 = 0;
      _v_76 = 0;
      _v_77 = 0;
      _v_78 = 0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_inr_0 == 1) {
        if (_x_2 == 1) {
          u32 _v_79 = 0;
          u32 _v_80 = 0;
          Term _o_11[1];
          if (spin_14(e, _o_11, _st_0) == 0) {
            return 0;
          }
          _v_80 = _o_11[0];
          _v_79 = _v_80;
          Term _v_82 = 0;
          u32 _v_83 = 0;
          u32 _v_84 = 0;
          u32 _v_85 = 0;
          u32 _v_86 = 0;
          Term _v_87 = 0;
          u32 _v_88 = 0;
          u32 _v_89 = 0;
          u32 _v_90 = 0;
          u32 _v_91 = 0;
          Term _o_12[5];
          if (spin_2(e, _o_12, _x_0, nat_chk(e, _p_3 + 1)) == 0) {
            return 0;
          }
          _v_87 = _o_12[0];
          _v_88 = _o_12[1];
          _v_89 = _o_12[2];
          _v_90 = _o_12[3];
          _v_91 = _o_12[4];
          _v_82 = _v_87;
          _v_83 = _v_88;
          _v_84 = _v_89;
          _v_85 = _v_90;
          _v_86 = _v_91;
          r0 = _f_0;
          r1 = (nat_chk(e, _p_3 + 1) < _e_1);
          r2 = nat_chk(e, _p_3 + 1);
          r3 = _e_1;
          r4 = _v_79;
          r5 = _v_82;
          r6 = _v_83;
          r7 = _v_84;
          r8 = _v_85;
          r9 = _v_86;
          _fuel_0 = r0;
          _inr_0 = r1;
          _p_3 = r2;
          _e_1 = r3;
          _st_0 = r4;
          _x_0 = r5;
          _x_1 = r6;
          _x_2 = r7;
          _x_3 = r8;
          _x_4 = r9;
          WL_AGAIN(spin_13);
        } else {
          u32 _v_92 = 0;
          u32 _v_93 = 0;
          Term _v_94 = 0;
          u32 _v_95 = 0;
          u32 _v_96 = 0;
          u32 _v_97 = 0;
          Term _v_98 = 0;
          u32 _v_99 = 0;
          Term _o_13[4];
          if (spin_15(e, _o_13, _st_0, _p_3, _x_1) == 0) {
            return 0;
          }
          _v_96 = _o_13[0];
          _v_97 = _o_13[1];
          _v_98 = _o_13[2];
          _v_99 = _o_13[3];
          _v_92 = _v_96;
          _v_93 = _v_97;
          _v_94 = _v_98;
          _v_95 = _v_99;
          _v_74 = _x_0;
          _v_75 = _v_92;
          _v_76 = _v_93;
          _v_77 = _v_94;
          _v_78 = _v_95;
        }
      } else {
        _v_74 = _x_0;
        _v_75 = 0;
        _v_76 = 0;
        _v_77 = 0;
        _v_78 = 0;
      }
    }
  break;
  }
  o[0] = _v_74;
  o[1] = _v_75;
  o[2] = _v_76;
  o[3] = _v_77;
  o[4] = _v_78;
  return 1;
}

INLINE Term spin_1(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3) {
  u32 wpoll = 0;
  Term _v_10 = 0;
  u32 _v_11 = 0;
  u32 _v_12 = 0;
  Term _v_13 = 0;
  u32 _v_14 = 0;
  Term _e_0 = r0;
  Term _c_0 = r1;
  u32 _c_1 = r2;
  Term _a_0 = r3;
  WL_SPIN
    Term _v_15 = 0;
    u32 _v_16 = 0;
    u32 _v_17 = 0;
    u32 _v_18 = 0;
    u32 _v_19 = 0;
    Term _v_20 = 0;
    u32 _v_21 = 0;
    u32 _v_22 = 0;
    u32 _v_23 = 0;
    u32 _v_24 = 0;
    Term _o_10[5];
    if (spin_2(e, _o_10, _a_0, _c_0) == 0) {
      return 0;
    }
    _v_20 = _o_10[0];
    _v_21 = _o_10[1];
    _v_22 = _o_10[2];
    _v_23 = _o_10[3];
    _v_24 = _o_10[4];
    _v_15 = _v_20;
    _v_16 = _v_21;
    _v_17 = _v_22;
    _v_18 = _v_23;
    _v_19 = _v_24;
    Term _v_69 = 0;
    u32 _v_70 = 0;
    u32 _v_71 = 0;
    Term _v_72 = 0;
    u32 _v_73 = 0;
    Term _o_14[5];
    if (spin_13(e, _o_14, nat_chk(e, (_e_0 < _c_0 ? 0 : _e_0 - _c_0) + 1), (_c_0 < _e_0), _c_0, _e_0, _c_1, _v_15, _v_16, _v_17, _v_18, _v_19) == 0) {
      return 0;
    }
    _v_69 = _o_14[0];
    _v_70 = _o_14[1];
    _v_71 = _o_14[2];
    _v_72 = _o_14[3];
    _v_73 = _o_14[4];
    _v_10 = _v_69;
    _v_11 = _v_70;
    _v_12 = _v_71;
    _v_13 = _v_72;
    _v_14 = _v_73;
  break;
  }
  o[0] = _v_10;
  o[1] = _v_11;
  o[2] = _v_12;
  o[3] = _v_13;
  o[4] = _v_14;
  return 1;
}

INLINE Term spin_16(Env e, THR Term* o, u32 r0, Term r1, u32 r2, Term r3, u32 r4, u32 r5, Term r6, u32 r7) {
  u32 wpoll = 0;
  u32 _v_110 = 0;
  Term _v_111 = 0;
  Term _v_112 = 0;
  u32 _v_113 = 0;
  Term _v_114 = 0;
  u32 _v_115 = 0;
  u32 _y_0 = r0;
  Term _cq_0 = r1;
  u32 _cq_1 = r2;
  Term _r_9 = r3;
  u32 _r_10 = r4;
  u32 _r_11 = r5;
  Term _r_12 = r6;
  u32 _r_13 = r7;
  WL_SPIN
    if (_r_10 == 0) {
      _v_110 = 0;
      _v_111 = _r_9;
      _v_112 = 0;
      _v_113 = 0;
      _v_114 = 0;
      _v_115 = 0;
    } else {
      u32 _v_116 = 0;
      Term _v_117 = 0;
      Term _v_118 = 0;
      u32 _v_119 = 0;
      Term _v_120 = 0;
      u32 _v_121 = 0;
      Term _o_16[6];
      if (spin_0(e, _o_16, U32_BIN(_y_0, ==, _r_11), _r_12, _r_13, _cq_0, _cq_1, _r_9) == 0) {
        return 0;
      }
      _v_116 = _o_16[0];
      _v_117 = _o_16[1];
      _v_118 = _o_16[2];
      _v_119 = _o_16[3];
      _v_120 = _o_16[4];
      _v_121 = _o_16[5];
      _v_110 = _v_116;
      _v_111 = _v_117;
      _v_112 = _v_118;
      _v_113 = _v_119;
      _v_114 = _v_120;
      _v_115 = _v_121;
    }
  break;
  }
  o[0] = _v_110;
  o[1] = _v_111;
  o[2] = _v_112;
  o[3] = _v_113;
  o[4] = _v_114;
  o[5] = _v_115;
  return 1;
}

INLINE Term spin_17(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3, u32 r4, u32 r5, Term r6, u32 r7) {
  u32 wpoll = 0;
  u32 _v_22 = 0;
  Term _v_23 = 0;
  Term _v_24 = 0;
  u32 _v_25 = 0;
  Term _v_26 = 0;
  u32 _v_27 = 0;
  Term _ee_1 = r0;
  Term _ce_0 = r1;
  u32 _ce_1 = r2;
  Term _r_0 = r3;
  u32 _r_1 = r4;
  u32 _r_2 = r5;
  Term _r_3 = r6;
  u32 _r_4 = r7;
  WL_SPIN
    if (_r_1 == 0) {
      _v_22 = 0;
      _v_23 = _r_0;
      _v_24 = 1;
      _v_25 = 0;
      _v_26 = 0;
      _v_27 = 0;
    } else {
      Term _v_28 = 0;
      u32 _v_29 = 0;
      u32 _v_30 = 0;
      Term _v_31 = 0;
      u32 _v_32 = 0;
      Term _v_33 = 0;
      u32 _v_34 = 0;
      u32 _v_35 = 0;
      Term _v_36 = 0;
      u32 _v_37 = 0;
      Term _o_1[5];
      if (spin_1(e, _o_1, _ee_1, _ce_0, _ce_1, _r_0) == 0) {
        return 0;
      }
      _v_33 = _o_1[0];
      _v_34 = _o_1[1];
      _v_35 = _o_1[2];
      _v_36 = _o_1[3];
      _v_37 = _o_1[4];
      _v_28 = _v_33;
      _v_29 = _v_34;
      _v_30 = _v_35;
      _v_31 = _v_36;
      _v_32 = _v_37;
      u32 _v_38 = 0;
      Term _v_39 = 0;
      Term _v_40 = 0;
      u32 _v_41 = 0;
      Term _v_42 = 0;
      u32 _v_43 = 0;
      Term _o_2[6];
      if (spin_16(e, _o_2, _r_2, _r_3, _r_4, _v_28, _v_29, _v_30, _v_31, _v_32) == 0) {
        return 0;
      }
      _v_38 = _o_2[0];
      _v_39 = _o_2[1];
      _v_40 = _o_2[2];
      _v_41 = _o_2[3];
      _v_42 = _o_2[4];
      _v_43 = _o_2[5];
      _v_22 = _v_38;
      _v_23 = _v_39;
      _v_24 = _v_40;
      _v_25 = _v_41;
      _v_26 = _v_42;
      _v_27 = _v_43;
    }
  break;
  }
  o[0] = _v_22;
  o[1] = _v_23;
  o[2] = _v_24;
  o[3] = _v_25;
  o[4] = _v_26;
  o[5] = _v_27;
  return 1;
}

INLINE Term spin_18(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3, Term r4, Term r5, u32 r6, Term r7, u32 r8) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  u32 _v_3 = 0;
  Term _fuel_0 = r0;
  Term _ee_1 = r1;
  Term _eq_0 = r2;
  u32 _x_0 = r3;
  Term _x_1 = r4;
  Term _x_2 = r5;
  u32 _x_3 = r6;
  Term _x_4 = r7;
  u32 _x_5 = r8;
  WL_SPIN
    if (_fuel_0 == 0) {
      if (_x_0 == 0) {
        _v_2 = _x_1;
        _v_3 = _x_2;
      } else {
        _v_2 = _x_1;
        _v_3 = 0;
      }
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_0 == 0) {
        _v_2 = _x_1;
        _v_3 = _x_2;
      } else {
        u32 _v_4 = 0;
        Term _v_5 = 0;
        Term _v_6 = 0;
        u32 _v_7 = 0;
        Term _v_8 = 0;
        u32 _v_9 = 0;
        Term _v_10 = 0;
        u32 _v_11 = 0;
        u32 _v_12 = 0;
        Term _v_13 = 0;
        u32 _v_14 = 0;
        Term _v_15 = 0;
        u32 _v_16 = 0;
        u32 _v_17 = 0;
        Term _v_18 = 0;
        u32 _v_19 = 0;
        Term _o_0[5];
        if (spin_1(e, _o_0, _eq_0, _x_4, _x_5, _x_1) == 0) {
          return 0;
        }
        _v_15 = _o_0[0];
        _v_16 = _o_0[1];
        _v_17 = _o_0[2];
        _v_18 = _o_0[3];
        _v_19 = _o_0[4];
        _v_10 = _v_15;
        _v_11 = _v_16;
        _v_12 = _v_17;
        _v_13 = _v_18;
        _v_14 = _v_19;
        u32 _v_20 = 0;
        Term _v_21 = 0;
        Term _v_22 = 0;
        u32 _v_23 = 0;
        Term _v_24 = 0;
        u32 _v_25 = 0;
        Term _o_1[6];
        if (spin_17(e, _o_1, _ee_1, _x_2, _x_3, _v_10, _v_11, _v_12, _v_13, _v_14) == 0) {
          return 0;
        }
        _v_20 = _o_1[0];
        _v_21 = _o_1[1];
        _v_22 = _o_1[2];
        _v_23 = _o_1[3];
        _v_24 = _o_1[4];
        _v_25 = _o_1[5];
        _v_4 = _v_20;
        _v_5 = _v_21;
        _v_6 = _v_22;
        _v_7 = _v_23;
        _v_8 = _v_24;
        _v_9 = _v_25;
        r0 = _f_0;
        r1 = _ee_1;
        r2 = _eq_0;
        r3 = _v_4;
        r4 = _v_5;
        r5 = _v_6;
        r6 = _v_7;
        r7 = _v_8;
        r8 = _v_9;
        _fuel_0 = r0;
        _ee_1 = r1;
        _eq_0 = r2;
        _x_0 = r3;
        _x_1 = r4;
        _x_2 = r5;
        _x_3 = r6;
        _x_4 = r7;
        _x_5 = r8;
        WL_AGAIN(spin_18);
      }
    }
  break;
  }
  o[0] = _v_2;
  o[1] = _v_3;
  return 1;
}

INLINE Term spin_19(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  u32 _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _c_3 = r0;
  u32 _c_4 = r1;
  Term _i_1 = r2;
  Term _a_2 = r3;
  Term _pat_0 = r4;
  Term _r_0 = r5;
  u32 _r_1 = r6;
  WL_SPIN
    _v_8 = 1;
    _v_9 = _a_2;
    _v_10 = _pat_0;
    _v_11 = _r_0;
    _v_12 = _c_3;
    _v_13 = _c_4;
    _v_14 = _i_1;
    _v_15 = _r_1;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  o[4] = _v_12;
  o[5] = _v_13;
  o[6] = _v_14;
  o[7] = _v_15;
  return 1;
}

INLINE Term spin_20(Env e, THR Term* o, Term r0, Term r1, u32 r2) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _pat_1 = r0;
  Term _r_0 = r1;
  u32 _r_1 = r2;
  WL_SPIN
    _v_4 = 1;
    _v_5 = _pat_1;
    _v_6 = _r_0;
    _v_7 = _r_1;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_21(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  Term _id_1 = r0;
  Term _x_1 = r1;
  Term _r_0 = r2;
  Term _r_1 = r3;
  WL_SPIN
    Term _at_2 = blk_loc(e.mem, _r_0);
    Term _at_3 = blk_at(_r_0, ((u64)(u32)(_id_1)), 0);
    Term _c_1 = blk_read(e.mem, 1, _at_2, _at_3 + 0);
    blk_write(e.mem, 1, _at_2, _at_3 + 0, nat_chk(e, _r_1 + _x_1));
    _v_1 = _r_0;
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_22(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3, u32 r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_10 = 0;
  u32 _v_11 = 0;
  u32 _v_12 = 0;
  Term _v_13 = 0;
  u32 _v_14 = 0;
  Term _i_1 = r0;
  Term _e_0 = r1;
  Term _x_0 = r2;
  u32 _x_1 = r3;
  u32 _x_2 = r4;
  Term _x_3 = r5;
  u32 _x_4 = r6;
  WL_SPIN
    if (_i_1 == 0) {
      _v_10 = _x_0;
      _v_11 = _x_1;
      _v_12 = _x_2;
      _v_13 = _x_3;
      _v_14 = _x_4;
    } else {
      Term _j_0 = (_i_1 - 1);
      if (_x_1 == 0) {
        _v_10 = _x_0;
        _v_11 = 0;
        _v_12 = 0;
        _v_13 = 0;
        _v_14 = 0;
      } else {
        Term _v_15 = 0;
        u32 _v_16 = 0;
        u32 _v_17 = 0;
        Term _v_18 = 0;
        u32 _v_19 = 0;
        Term _v_20 = 0;
        u32 _v_21 = 0;
        u32 _v_22 = 0;
        Term _v_23 = 0;
        u32 _v_24 = 0;
        Term _o_0[5];
        if (spin_1(e, _o_0, _e_0, _x_3, _x_4, _x_0) == 0) {
          return 0;
        }
        _v_20 = _o_0[0];
        _v_21 = _o_0[1];
        _v_22 = _o_0[2];
        _v_23 = _o_0[3];
        _v_24 = _o_0[4];
        _v_15 = _v_20;
        _v_16 = _v_21;
        _v_17 = _v_22;
        _v_18 = _v_23;
        _v_19 = _v_24;
        r0 = _j_0;
        r1 = _e_0;
        r2 = _v_15;
        r3 = _v_16;
        r4 = _v_17;
        r5 = _v_18;
        r6 = _v_19;
        _i_1 = r0;
        _e_0 = r1;
        _x_0 = r2;
        _x_1 = r3;
        _x_2 = r4;
        _x_3 = r5;
        _x_4 = r6;
        WL_AGAIN(spin_22);
      }
    }
  break;
  }
  o[0] = _v_10;
  o[1] = _v_11;
  o[2] = _v_12;
  o[3] = _v_13;
  o[4] = _v_14;
  return 1;
}

INLINE Term spin_23(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4, u32 r5, Term r6, u32 r7) {
  u32 wpoll = 0;
  Term _v_27 = 0;
  u32 _v_28 = 0;
  Term _ee_1 = r0;
  Term _qo_1 = r1;
  Term _ql_1 = r2;
  Term _r_0 = r3;
  u32 _r_1 = r4;
  u32 _r_2 = r5;
  Term _r_3 = r6;
  u32 _r_4 = r7;
  WL_SPIN
    if (_r_1 == 0) {
      _v_27 = _r_0;
      _v_28 = 0;
    } else {
      Term _v_29 = 0;
      u32 _v_30 = 0;
      Term _o_2[2];
      if (spin_18(e, _o_2, nat_chk(e, _ql_1 + 1), _ee_1, nat_chk(e, _qo_1 + _ql_1), 1, _r_0, _r_3, _r_4, _qo_1, 0) == 0) {
        return 0;
      }
      _v_29 = _o_2[0];
      _v_30 = _o_2[1];
      _v_27 = _v_29;
      _v_28 = _v_30;
    }
  break;
  }
  o[0] = _v_27;
  o[1] = _v_28;
  return 1;
}

INLINE Term spin_24(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, Term r5, Term r6) {
  u32 wpoll = 0;
  u32 _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _c_2 = r0;
  u32 _c_3 = r1;
  Term _i_1 = r2;
  Term _k_1 = r3;
  Term _a_0 = r4;
  Term _t_2 = r5;
  Term _t_3 = r6;
  WL_SPIN
    Term _a_1 = 1ull;
    Term _at_1 = blk_loc(e.mem, _t_3);
    Term _at_2 = blk_at(_t_3, ((u64)(u32)((_k_1 < _a_1 ? 0 : _k_1 - _a_1))), 0);
    u32 _c_4 = blk_read(e.mem, 0, _at_1, _at_2 + 0);
    u32 _v_16 = 0;
    Term _v_17 = 0;
    Term _v_18 = 0;
    Term _v_19 = 0;
    Term _v_20 = 0;
    Term _v_21 = 0;
    Term _v_22 = 0;
    Term _v_23 = 0;
    Term _o_0[8];
    if (spin_19(e, _o_0, _c_2, _c_3, _i_1, _a_0, _t_2, _t_3, _c_4) == 0) {
      return 0;
    }
    _v_16 = _o_0[0];
    _v_17 = _o_0[1];
    _v_18 = _o_0[2];
    _v_19 = _o_0[3];
    _v_20 = _o_0[4];
    _v_21 = _o_0[5];
    _v_22 = _o_0[6];
    _v_23 = _o_0[7];
    _v_8 = _v_16;
    _v_9 = _v_17;
    _v_10 = _v_18;
    _v_11 = _v_19;
    _v_12 = _v_20;
    _v_13 = _v_21;
    _v_14 = _v_22;
    _v_15 = _v_23;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  o[4] = _v_12;
  o[5] = _v_13;
  o[6] = _v_14;
  o[7] = _v_15;
  return 1;
}

INLINE Term spin_25(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _k_1 = r0;
  Term _pat_1 = r1;
  Term _f_1 = r2;
  WL_SPIN
    if (_k_1 == 0) {
      _v_4 = 0;
      _v_5 = _pat_1;
      _v_6 = _f_1;
      _v_7 = 0;
    } else {
      Term _j_0 = (_k_1 - 1);
      Term _at_0 = blk_loc(e.mem, _f_1);
      Term _at_1 = blk_at(_f_1, ((u64)(u32)(_j_0)), 0);
      u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      u32 _v_8 = 0;
      Term _v_9 = 0;
      Term _v_10 = 0;
      Term _v_11 = 0;
      Term _o_0[4];
      if (spin_20(e, _o_0, _pat_1, _f_1, _c_0) == 0) {
        return 0;
      }
      _v_8 = _o_0[0];
      _v_9 = _o_0[1];
      _v_10 = _o_0[2];
      _v_11 = _o_0[3];
      _v_4 = _v_8;
      _v_5 = _v_9;
      _v_6 = _v_10;
      _v_7 = _v_11;
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_26(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  Term _tf_1 = r0;
  Term _dl_1 = r1;
  Term _tot_1 = r2;
  Term _n_1 = r3;
  WL_SPIN
    Term _v_2 = 0;
    Term _v_3 = 0;
    Term _o_0[1];
    if (spin_4(e, _o_0, nat_mul(e, nat_mul(e, _dl_1, _n_1), 1024ull), _tot_1) == 0) {
      return 0;
    }
    _v_3 = _o_0[0];
    _v_2 = _v_3;
    Term _v_4 = 0;
    Term _o_1[1];
    if (spin_4(e, _o_1, nat_mul(e, nat_mul(e, 44ull, _tf_1), 1048576ull), nat_chk(e, nat_mul(e, 1024ull, nat_chk(e, nat_mul(e, 20ull, _tf_1) + 6ull)) + nat_mul(e, 18ull, _v_2))) == 0) {
      return 0;
    }
    _v_4 = _o_1[0];
    _v_1 = _v_4;
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_27(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  Term _s_1 = r0;
  Term _id_1 = r1;
  Term _x_1 = r2;
  WL_SPIN
    Term _at_0 = blk_loc(e.mem, _s_1);
    Term _at_1 = blk_at(_s_1, ((u64)(u32)(_id_1)), 0);
    Term _c_0 = blk_read(e.mem, 1, _at_0, _at_1 + 0);
    Term _v_2 = 0;
    Term _o_0[1];
    if (spin_21(e, _o_0, _id_1, _x_1, _s_1, _c_0) == 0) {
      return 0;
    }
    _v_2 = _o_0[0];
    _v_1 = _v_2;
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_28(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  u32 _v_5 = 0;
  Term _a_1 = r0;
  Term _eo_0 = r1;
  Term _el_0 = r2;
  Term _qo_0 = r3;
  Term _ql_0 = r4;
  Term _i_1 = r5;
  WL_SPIN
    Term _ee_0 = nat_chk(e, _eo_0 + _el_0);
    Term _v_6 = 0;
    u32 _v_7 = 0;
    u32 _v_8 = 0;
    Term _v_9 = 0;
    u32 _v_10 = 0;
    Term _v_11 = 0;
    u32 _v_12 = 0;
    u32 _v_13 = 0;
    Term _v_14 = 0;
    u32 _v_15 = 0;
    Term _o_0[5];
    if (spin_22(e, _o_0, _i_1, _ee_0, _a_1, 1, 0ull, _eo_0, 0) == 0) {
      return 0;
    }
    _v_11 = _o_0[0];
    _v_12 = _o_0[1];
    _v_13 = _o_0[2];
    _v_14 = _o_0[3];
    _v_15 = _o_0[4];
    _v_6 = _v_11;
    _v_7 = _v_12;
    _v_8 = _v_13;
    _v_9 = _v_14;
    _v_10 = _v_15;
    Term _v_16 = 0;
    u32 _v_17 = 0;
    Term _o_1[2];
    if (spin_23(e, _o_1, _ee_0, _qo_0, _ql_0, _v_6, _v_7, _v_8, _v_9, _v_10) == 0) {
      return 0;
    }
    _v_16 = _o_1[0];
    _v_17 = _o_1[1];
    _v_4 = _v_16;
    _v_5 = _v_17;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_29(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3, Term r4, Term r5, Term r6, Term r7, u32 r8) {
  u32 wpoll = 0;
  u32 _v_26 = 0;
  Term _v_27 = 0;
  Term _v_28 = 0;
  Term _v_29 = 0;
  Term _v_30 = 0;
  Term _v_31 = 0;
  Term _v_32 = 0;
  Term _v_33 = 0;
  Term _at_1 = r0;
  Term _c_2 = r1;
  u32 _c_3 = r2;
  Term _i_2 = r3;
  Term _k_1 = r4;
  Term _t_2 = r5;
  Term _t_3 = r6;
  Term _r_0 = r7;
  u32 _r_1 = r8;
  WL_SPIN
    if (_r_1 == 1) {
      _v_26 = 0;
      _v_27 = _r_0;
      _v_28 = _t_2;
      _v_29 = _t_3;
      _v_30 = 3;
      _v_31 = _at_1;
      _v_32 = 0;
      _v_33 = 0;
    } else {
      u32 _v_34 = 0;
      Term _v_35 = 0;
      Term _v_36 = 0;
      Term _v_37 = 0;
      Term _v_38 = 0;
      Term _v_39 = 0;
      Term _v_40 = 0;
      Term _v_41 = 0;
      Term _o_3[8];
      if (spin_24(e, _o_3, _c_2, _c_3, _i_2, _k_1, _r_0, _t_2, _t_3) == 0) {
        return 0;
      }
      _v_34 = _o_3[0];
      _v_35 = _o_3[1];
      _v_36 = _o_3[2];
      _v_37 = _o_3[3];
      _v_38 = _o_3[4];
      _v_39 = _o_3[5];
      _v_40 = _o_3[6];
      _v_41 = _o_3[7];
      _v_26 = _v_34;
      _v_27 = _v_35;
      _v_28 = _v_36;
      _v_29 = _v_37;
      _v_30 = _v_38;
      _v_31 = _v_39;
      _v_32 = _v_40;
      _v_33 = _v_41;
    }
  break;
  }
  o[0] = _v_26;
  o[1] = _v_27;
  o[2] = _v_28;
  o[3] = _v_29;
  o[4] = _v_30;
  o[5] = _v_31;
  o[6] = _v_32;
  o[7] = _v_33;
  return 1;
}

INLINE Term spin_30(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  u32 _eq_0 = r0;
  Term _k_1 = r1;
  Term _pat_0 = r2;
  Term _f_1 = r3;
  WL_SPIN
    if (_eq_0 == 1) {
      _v_4 = 0;
      _v_5 = _pat_0;
      _v_6 = _f_1;
      _v_7 = nat_chk(e, _k_1 + 1);
    } else {
      u32 _v_8 = 0;
      Term _v_9 = 0;
      Term _v_10 = 0;
      Term _v_11 = 0;
      Term _o_0[4];
      if (spin_25(e, _o_0, _k_1, _pat_0, _f_1) == 0) {
        return 0;
      }
      _v_8 = _o_0[0];
      _v_9 = _o_0[1];
      _v_10 = _o_0[2];
      _v_11 = _o_0[3];
      _v_4 = _v_8;
      _v_5 = _v_9;
      _v_6 = _v_10;
      _v_7 = _v_11;
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_31(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_3 = 0;
  Term _tf_1 = r0;
  Term _dl_0 = r1;
  Term _tot_1 = r2;
  Term _n_1 = r3;
  WL_SPIN
    if (_tf_1 == 0) {
      _v_3 = 0;
    } else {
      Term _p_0 = (_tf_1 - 1);
      Term _v_4 = 0;
      Term _o_0[1];
      if (spin_26(e, _o_0, nat_chk(e, _p_0 + 1), _dl_0, _tot_1, _n_1) == 0) {
        return 0;
      }
      _v_4 = _o_0[0];
      _v_3 = _v_4;
    }
  break;
  }
  o[0] = _v_3;
  return 1;
}

INLINE Term spin_32(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  u32 _go_0 = r0;
  Term _id_1 = r1;
  Term _x_0 = r2;
  Term _s_1 = r3;
  WL_SPIN
    if (_go_0 == 0) {
      _v_6 = _s_1;
    } else {
      Term _v_7 = 0;
      Term _o_2[1];
      if (spin_27(e, _o_2, _s_1, _id_1, _x_0) == 0) {
        return 0;
      }
      _v_7 = _o_2[0];
      _v_6 = _v_7;
    }
  break;
  }
  o[0] = _v_6;
  return 1;
}

INLINE Term spin_33(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_3 = 0;
  Term _j_5 = r0;
  Term _j_6 = r1;
  Term _j_7 = r2;
  Term _j_8 = r3;
  Term _j_9 = r4;
  WL_SPIN
    _v_3 = _j_9;
  break;
  }
  o[0] = _v_3;
  return 1;
}

INLINE Term spin_35(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_7 = 0;
  u32 _c_2 = r0;
  WL_SPIN
    if (_c_2 == 0) {
      _v_7 = 0;
    } else if (_c_2 == 1) {
      _v_7 = 1;
    } else {
      _v_7 = 0;
    }
  break;
  }
  o[0] = _v_7;
  return 1;
}

INLINE Term spin_34(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  u32 _v_5 = 0;
  Term _a_1 = r0;
  Term _b_0 = r1;
  WL_SPIN
    u32 _v_6 = 0;
    Term _o_1[1];
    if (spin_35(e, _o_1, ((_a_1 > _b_0) + (_a_1 >= _b_0))) == 0) {
      return 0;
    }
    _v_6 = _o_1[0];
    _v_5 = _v_6;
  break;
  }
  o[0] = _v_5;
  return 1;
}

INLINE Term spin_36(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7, Term r8, Term r9, Term r10, Term r11, Term r12) {
  u32 wpoll = 0;
  u32 _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  u32 _full_0 = r0;
  Term _j_10 = r1;
  Term _j_11 = r2;
  Term _j_12 = r3;
  Term _j_13 = r4;
  Term _j_14 = r5;
  Term _c_3 = r6;
  u32 _c_4 = r7;
  Term _i_1 = r8;
  Term _k_0 = r9;
  Term _t_0 = r10;
  Term _t_1 = r11;
  Term _a_2 = r12;
  WL_SPIN
    if (_full_0 == 0) {
      _v_16 = 1;
      _v_17 = _a_2;
      _v_18 = _t_0;
      _v_19 = _t_1;
      _v_20 = _c_3;
      _v_21 = _c_4;
      _v_22 = _i_1;
      _v_23 = _k_0;
    } else {
      Term _at_0 = (_i_1 < _j_14 ? 0 : _i_1 - _j_14);
      Term _v_24 = 0;
      u32 _v_25 = 0;
      Term _v_26 = 0;
      u32 _v_27 = 0;
      Term _o_3[2];
      if (spin_28(e, _o_3, _a_2, _j_10, _j_11, _j_12, _j_13, _at_0) == 0) {
        return 0;
      }
      _v_26 = _o_3[0];
      _v_27 = _o_3[1];
      _v_24 = _v_26;
      _v_25 = _v_27;
      u32 _v_28 = 0;
      Term _v_29 = 0;
      Term _v_30 = 0;
      Term _v_31 = 0;
      Term _v_32 = 0;
      Term _v_33 = 0;
      Term _v_34 = 0;
      Term _v_35 = 0;
      Term _o_4[8];
      if (spin_29(e, _o_4, _at_0, _c_3, _c_4, _i_1, _k_0, _t_0, _t_1, _v_24, _v_25) == 0) {
        return 0;
      }
      _v_28 = _o_4[0];
      _v_29 = _o_4[1];
      _v_30 = _o_4[2];
      _v_31 = _o_4[3];
      _v_32 = _o_4[4];
      _v_33 = _o_4[5];
      _v_34 = _o_4[6];
      _v_35 = _o_4[7];
      _v_16 = _v_28;
      _v_17 = _v_29;
      _v_18 = _v_30;
      _v_19 = _v_31;
      _v_20 = _v_32;
      _v_21 = _v_33;
      _v_22 = _v_34;
      _v_23 = _v_35;
    }
  break;
  }
  o[0] = _v_16;
  o[1] = _v_17;
  o[2] = _v_18;
  o[3] = _v_19;
  o[4] = _v_20;
  o[5] = _v_21;
  o[6] = _v_22;
  o[7] = _v_23;
  return 1;
}

INLINE Term spin_37(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _k_1 = r0;
  u32 _y_1 = r1;
  Term _f_0 = r2;
  Term _r_0 = r3;
  u32 _r_1 = r4;
  WL_SPIN
    u32 _v_8 = 0;
    Term _v_9 = 0;
    Term _v_10 = 0;
    Term _v_11 = 0;
    Term _o_0[4];
    if (spin_30(e, _o_0, U32_BIN(_r_1, ==, _y_1), _k_1, _r_0, _f_0) == 0) {
      return 0;
    }
    _v_8 = _o_0[0];
    _v_9 = _o_0[1];
    _v_10 = _o_0[2];
    _v_11 = _o_0[3];
    _v_4 = _v_8;
    _v_5 = _v_9;
    _v_6 = _v_10;
    _v_7 = _v_11;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_38(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3) {
  u32 wpoll = 0;
  u32 _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  Term _k_0 = r0;
  u32 _y_1 = r1;
  Term _t_0 = r2;
  Term _t_1 = r3;
  WL_SPIN
    Term _at_0 = blk_loc(e.mem, _t_0);
    Term _at_1 = blk_at(_t_0, ((u64)(u32)(_k_0)), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    u32 _v_12 = 0;
    Term _v_13 = 0;
    Term _v_14 = 0;
    Term _v_15 = 0;
    Term _o_0[4];
    if (spin_37(e, _o_0, _k_0, _y_1, _t_1, _t_0, _c_0) == 0) {
      return 0;
    }
    _v_12 = _o_0[0];
    _v_13 = _o_0[1];
    _v_14 = _o_0[2];
    _v_15 = _o_0[3];
    _v_8 = _v_12;
    _v_9 = _v_13;
    _v_10 = _v_14;
    _v_11 = _v_15;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  return 1;
}

INLINE Term spin_40(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_6 = 0;
  u32 _c_0 = r0;
  WL_SPIN
    if (_c_0 == 0) {
      _v_6 = 1;
    } else if (_c_0 == 1) {
      _v_6 = 1;
    } else {
      _v_6 = 0;
    }
  break;
  }
  o[0] = _v_6;
  return 1;
}

INLINE Term spin_39(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _a_0 = r0;
  Term _b_0 = r1;
  WL_SPIN
    u32 _v_5 = 0;
    Term _o_1[1];
    if (spin_40(e, _o_1, ((_a_0 > _b_0) + (_a_0 >= _b_0))) == 0) {
      return 0;
    }
    _v_5 = _o_1[0];
    _v_4 = _v_5;
  break;
  }
  o[0] = _v_4;
  return 1;
}

INLINE Term spin_41(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_11 = 0;
  u32 _c_1 = r0;
  Term _a_1 = r1;
  Term _b_1 = r2;
  WL_SPIN
    if (_c_1 == 1) {
      _v_11 = _a_1;
    } else {
      _v_11 = _b_1;
    }
  break;
  }
  o[0] = _v_11;
  return 1;
}

INLINE Term spin_42(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _v_3 = 0;
  Term _id_1 = r0;
  Term _tf_0 = r1;
  Term _w_1 = r2;
  Term _tot_1 = r3;
  Term _n_1 = r4;
  Term _s_1 = r5;
  Term _r_2 = r6;
  u32 _r_3 = r7;
  WL_SPIN
    Term _v_4 = 0;
    Term _v_5 = 0;
    Term _v_6 = 0;
    Term _o_0[1];
    if (spin_31(e, _o_0, _tf_0, _r_3, _tot_1, _n_1) == 0) {
      return 0;
    }
    _v_6 = _o_0[0];
    _v_5 = _v_6;
    Term _v_7 = 0;
    Term _o_1[1];
    if (spin_32(e, _o_1, (_id_1 < _n_1), _id_1, nat_mul(e, _w_1, _v_5), _s_1) == 0) {
      return 0;
    }
    _v_7 = _o_1[0];
    _v_4 = _v_7;
    _v_2 = _r_2;
    _v_3 = _v_4;
  break;
  }
  o[0] = _v_2;
  o[1] = _v_3;
  return 1;
}

INLINE Term spin_43(Env e, THR Term* o, Term r0, u32 r1, u32 r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _fuel_0 = r0;
  u32 _y_0 = r1;
  u32 _x_0 = r2;
  Term _x_1 = r3;
  Term _x_2 = r4;
  Term _x_3 = r5;
  WL_SPIN
    if (_fuel_0 == 0) {
      if (_x_0 == 0) {
        _v_14 = _x_1;
        _v_15 = _x_2;
        _v_16 = _x_3;
      } else {
        _v_14 = _x_1;
        _v_15 = _x_2;
        _v_16 = 0;
      }
    } else {
      Term _g_0 = (_fuel_0 - 1);
      if (_x_0 == 0) {
        _v_14 = _x_1;
        _v_15 = _x_2;
        _v_16 = _x_3;
      } else {
        u32 _v_17 = 0;
        Term _v_18 = 0;
        Term _v_19 = 0;
        Term _v_20 = 0;
        u32 _v_21 = 0;
        Term _v_22 = 0;
        Term _v_23 = 0;
        Term _v_24 = 0;
        Term _o_1[4];
        if (spin_38(e, _o_1, _x_3, _y_0, _x_1, _x_2) == 0) {
          return 0;
        }
        _v_21 = _o_1[0];
        _v_22 = _o_1[1];
        _v_23 = _o_1[2];
        _v_24 = _o_1[3];
        _v_17 = _v_21;
        _v_18 = _v_22;
        _v_19 = _v_23;
        _v_20 = _v_24;
        r0 = _g_0;
        r1 = _y_0;
        r2 = _v_17;
        r3 = _v_18;
        r4 = _v_19;
        r5 = _v_20;
        _fuel_0 = r0;
        _y_0 = r1;
        _x_0 = r2;
        _x_1 = r3;
        _x_2 = r4;
        _x_3 = r5;
        WL_AGAIN(spin_43);
      }
    }
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  o[2] = _v_16;
  return 1;
}

INLINE Term spin_44(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, u32 r6, Term r7, Term r8, Term r9, Term r10, Term r11) {
  u32 wpoll = 0;
  u32 _v_33 = 0;
  Term _v_34 = 0;
  Term _v_35 = 0;
  Term _v_36 = 0;
  Term _v_37 = 0;
  Term _v_38 = 0;
  Term _v_39 = 0;
  Term _v_40 = 0;
  Term _j_5 = r0;
  Term _j_6 = r1;
  Term _j_7 = r2;
  Term _j_8 = r3;
  Term _j_9 = r4;
  Term _c_0 = r5;
  u32 _c_1 = r6;
  Term _i_1 = r7;
  Term _a_0 = r8;
  Term _r_5 = r9;
  Term _r_6 = r10;
  Term _r_7 = r11;
  WL_SPIN
    u32 _v_41 = 0;
    Term _v_42 = 0;
    Term _v_43 = 0;
    Term _o_3[1];
    if (spin_33(e, _o_3, _j_5, _j_6, _j_7, _j_8, _j_9) == 0) {
      return 0;
    }
    _v_43 = _o_3[0];
    _v_42 = _v_43;
    u32 _v_44 = 0;
    Term _o_4[1];
    if (spin_34(e, _o_4, _r_7, _v_42) == 0) {
      return 0;
    }
    _v_44 = _o_4[0];
    _v_41 = _v_44;
    u32 _v_45 = 0;
    Term _v_46 = 0;
    Term _v_47 = 0;
    Term _v_48 = 0;
    Term _v_49 = 0;
    Term _v_50 = 0;
    Term _v_51 = 0;
    Term _v_52 = 0;
    Term _o_5[8];
    if (spin_36(e, _o_5, _v_41, _j_5, _j_6, _j_7, _j_8, _j_9, _c_0, _c_1, _i_1, _r_7, _r_5, _r_6, _a_0) == 0) {
      return 0;
    }
    _v_45 = _o_5[0];
    _v_46 = _o_5[1];
    _v_47 = _o_5[2];
    _v_48 = _o_5[3];
    _v_49 = _o_5[4];
    _v_50 = _o_5[5];
    _v_51 = _o_5[6];
    _v_52 = _o_5[7];
    _v_33 = _v_45;
    _v_34 = _v_46;
    _v_35 = _v_47;
    _v_36 = _v_48;
    _v_37 = _v_49;
    _v_38 = _v_50;
    _v_39 = _v_51;
    _v_40 = _v_52;
  break;
  }
  o[0] = _v_33;
  o[1] = _v_34;
  o[2] = _v_35;
  o[3] = _v_36;
  o[4] = _v_37;
  o[5] = _v_38;
  o[6] = _v_39;
  o[7] = _v_40;
  return 1;
}

INLINE Term spin_45(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _j_1 = r0;
  Term _r_2 = r1;
  Term _r_3 = r2;
  Term _r_4 = r3;
  WL_SPIN
    Term _at_0 = blk_loc(e.mem, _r_3);
    Term _at_1 = blk_at(_r_3, ((u64)(u32)(_j_1)), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    blk_write(e.mem, 0, _at_0, _at_1 + 0, ((u64)(u32)(_r_4)));
    _v_17 = _r_2;
    _v_18 = _r_3;
    _v_19 = _r_4;
  break;
  }
  o[0] = _v_17;
  o[1] = _v_18;
  o[2] = _v_19;
  return 1;
}

INLINE Term spin_46(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _v_3 = 0;
  Term _q_1 = r0;
  Term _id_0 = r1;
  Term _w_1 = r2;
  Term _tot_1 = r3;
  Term _n_1 = r4;
  Term _s_1 = r5;
  Term _r_2 = r6;
  u32 _r_3 = r7;
  WL_SPIN
    Term _at_2 = blk_loc(e.mem, _r_2);
    Term _at_3 = blk_at(_r_2, ((u64)(u32)(nat_chk(e, _q_1 + 2ull))), 0);
    u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
    Term _v_4 = 0;
    Term _v_5 = 0;
    Term _o_0[2];
    if (spin_42(e, _o_0, _id_0, _r_3, _w_1, _tot_1, _n_1, _s_1, _r_2, _c_1) == 0) {
      return 0;
    }
    _v_4 = _o_0[0];
    _v_5 = _o_0[1];
    _v_2 = _v_4;
    _v_3 = _v_5;
  break;
  }
  o[0] = _v_2;
  o[1] = _v_3;
  return 1;
}

INLINE Term spin_47(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9, u32 r10, u32 r11, Term r12, u32 r13) {
  u32 wpoll = 0;
  u32 _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  Term _v_24 = 0;
  Term _v_25 = 0;
  Term _j_5 = r0;
  Term _j_6 = r1;
  Term _j_7 = r2;
  Term _j_8 = r3;
  Term _j_9 = r4;
  Term _i_1 = r5;
  Term _k_1 = r6;
  Term _t_2 = r7;
  Term _t_3 = r8;
  Term _r_0 = r9;
  u32 _r_1 = r10;
  u32 _r_2 = r11;
  Term _r_3 = r12;
  u32 _r_4 = r13;
  WL_SPIN
    if (_r_1 == 0) {
      _v_18 = 0;
      _v_19 = _r_0;
      _v_20 = _t_2;
      _v_21 = _t_3;
      _v_22 = 2;
      _v_23 = 0;
      _v_24 = 0;
      _v_25 = 0;
    } else {
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _v_28 = 0;
      u32 _v_29 = 0;
      Term _v_30 = 0;
      Term _v_31 = 0;
      Term _v_32 = 0;
      u32 _v_33 = 0;
      Term _v_34 = 0;
      Term _v_35 = 0;
      Term _v_36 = 0;
      Term _o_1[4];
      if (spin_38(e, _o_1, _k_1, _r_2, _t_2, _t_3) == 0) {
        return 0;
      }
      _v_33 = _o_1[0];
      _v_34 = _o_1[1];
      _v_35 = _o_1[2];
      _v_36 = _o_1[3];
      _v_29 = _v_33;
      _v_30 = _v_34;
      _v_31 = _v_35;
      _v_32 = _v_36;
      Term _v_37 = 0;
      Term _v_38 = 0;
      Term _v_39 = 0;
      Term _o_2[3];
      if (spin_43(e, _o_2, nat_chk(e, _k_1 + 1), _r_2, _v_29, _v_30, _v_31, _v_32) == 0) {
        return 0;
      }
      _v_37 = _o_2[0];
      _v_38 = _o_2[1];
      _v_39 = _o_2[2];
      _v_26 = _v_37;
      _v_27 = _v_38;
      _v_28 = _v_39;
      u32 _v_40 = 0;
      Term _v_41 = 0;
      Term _v_42 = 0;
      Term _v_43 = 0;
      Term _v_44 = 0;
      Term _v_45 = 0;
      Term _v_46 = 0;
      Term _v_47 = 0;
      Term _o_3[8];
      if (spin_44(e, _o_3, _j_5, _j_6, _j_7, _j_8, _j_9, _r_3, _r_4, nat_chk(e, _i_1 + 1), _r_0, _v_26, _v_27, _v_28) == 0) {
        return 0;
      }
      _v_40 = _o_3[0];
      _v_41 = _o_3[1];
      _v_42 = _o_3[2];
      _v_43 = _o_3[3];
      _v_44 = _o_3[4];
      _v_45 = _o_3[5];
      _v_46 = _o_3[6];
      _v_47 = _o_3[7];
      _v_18 = _v_40;
      _v_19 = _v_41;
      _v_20 = _v_42;
      _v_21 = _v_43;
      _v_22 = _v_44;
      _v_23 = _v_45;
      _v_24 = _v_46;
      _v_25 = _v_47;
    }
  break;
  }
  o[0] = _v_18;
  o[1] = _v_19;
  o[2] = _v_20;
  o[3] = _v_21;
  o[4] = _v_22;
  o[5] = _v_23;
  o[6] = _v_24;
  o[7] = _v_25;
  return 1;
}

INLINE Term spin_48(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  Term _v_3 = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _j_1 = r0;
  Term _k_1 = r1;
  Term _f_0 = r2;
  Term _r_0 = r3;
  u32 _r_1 = r4;
  WL_SPIN
    Term _v_6 = 0;
    Term _v_7 = 0;
    Term _v_8 = 0;
    u32 _v_9 = 0;
    Term _v_10 = 0;
    Term _v_11 = 0;
    Term _v_12 = 0;
    u32 _v_13 = 0;
    Term _v_14 = 0;
    Term _v_15 = 0;
    Term _v_16 = 0;
    Term _o_0[4];
    if (spin_38(e, _o_0, _k_1, _r_1, _r_0, _f_0) == 0) {
      return 0;
    }
    _v_13 = _o_0[0];
    _v_14 = _o_0[1];
    _v_15 = _o_0[2];
    _v_16 = _o_0[3];
    _v_9 = _v_13;
    _v_10 = _v_14;
    _v_11 = _v_15;
    _v_12 = _v_16;
    Term _v_17 = 0;
    Term _v_18 = 0;
    Term _v_19 = 0;
    Term _o_1[3];
    if (spin_43(e, _o_1, nat_chk(e, _k_1 + 1), _r_1, _v_9, _v_10, _v_11, _v_12) == 0) {
      return 0;
    }
    _v_17 = _o_1[0];
    _v_18 = _o_1[1];
    _v_19 = _o_1[2];
    _v_6 = _v_17;
    _v_7 = _v_18;
    _v_8 = _v_19;
    Term _v_20 = 0;
    Term _v_21 = 0;
    Term _v_22 = 0;
    Term _o_2[3];
    if (spin_45(e, _o_2, _j_1, _v_6, _v_7, _v_8) == 0) {
      return 0;
    }
    _v_20 = _o_2[0];
    _v_21 = _o_2[1];
    _v_22 = _o_2[2];
    _v_3 = _v_20;
    _v_4 = _v_21;
    _v_5 = _v_22;
  break;
  }
  o[0] = _v_3;
  o[1] = _v_4;
  o[2] = _v_5;
  return 1;
}

INLINE Term spin_49(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _v_3 = 0;
  Term _q_1 = r0;
  Term _w_1 = r1;
  Term _tot_1 = r2;
  Term _n_1 = r3;
  Term _s_0 = r4;
  Term _r_0 = r5;
  u32 _r_1 = r6;
  WL_SPIN
    Term _at_2 = blk_loc(e.mem, _r_0);
    Term _at_3 = blk_at(_r_0, ((u64)(u32)(nat_chk(e, _q_1 + 1))), 0);
    u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
    Term _v_4 = 0;
    Term _v_5 = 0;
    Term _o_0[2];
    if (spin_46(e, _o_0, _q_1, _r_1, _w_1, _tot_1, _n_1, _s_0, _r_0, _c_1) == 0) {
      return 0;
    }
    _v_4 = _o_0[0];
    _v_5 = _o_0[1];
    _v_2 = _v_4;
    _v_3 = _v_5;
  break;
  }
  o[0] = _v_2;
  o[1] = _v_3;
  return 1;
}

INLINE Term spin_51(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  u32 _v_4 = 0;
  Term _a_0 = r0;
  Term _a_1 = r1;
  Term _b_0 = r2;
  Term _b_1 = r3;
  WL_SPIN
    u32 _v_5 = 0;
    u32 _v_6 = 0;
    u32 _v_7 = 0;
    Term _o_0[1];
    if (spin_34(e, _o_0, _a_0, _b_0) == 0) {
      return 0;
    }
    _v_7 = _o_0[0];
    _v_6 = _v_7;
    u32 _v_8 = 0;
    Term _o_1[1];
    if (spin_11(e, _o_1, _v_6, (_a_1 < _b_1)) == 0) {
      return 0;
    }
    _v_8 = _o_1[0];
    _v_5 = _v_8;
    _v_4 = (((_b_0 < _a_0)) | (_v_5));
  break;
  }
  o[0] = _v_4;
  return 1;
}

INLINE Term spin_50(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  u32 _v_2 = 0;
  Term _h_2 = r0;
  Term _h_3 = r1;
  Term _hs_0 = r2;
  WL_SPIN
    if (term_aux(_hs_0) == CID_NIL) {
      _v_2 = 0;
    } else {
      u64 _sp_0 = term_peek(e.mem, _hs_0);
      Term _f_0 = e.mem[_sp_0 + 0];
      Term _f_1 = e.mem[_sp_0 + 1];
      u64 _sp_1 = term_peek(e.mem, _f_0);
      Term _f_2 = e.mem[_sp_1 + 0];
      Term _f_3 = e.mem[_sp_1 + 1];
      u32 _v_3 = 0;
      Term _o_2[1];
      if (spin_51(e, _o_2, _h_2, _h_3, _f_2, _f_3) == 0) {
        return 0;
      }
      _v_3 = _o_2[0];
      _v_2 = _v_3;
    }
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_53(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _xs_0 = r0;
  Term _acc_1 = r1;
  WL_SPIN
    if (term_aux(_xs_0) == CID_NIL) {
      _v_12 = _acc_1;
    } else {
      u64 _sp_2 = term_loc(_xs_0);
      Term _f_4 = e.mem[_sp_2 + 0];
      Term _f_5 = e.mem[_sp_2 + 1];
      u64 _nd_2 = _sp_2;
      e.mem[_nd_2 + 0] = _f_4;
      e.mem[_nd_2 + 1] = _acc_1;
      r0 = _f_5;
      r1 = term_ctr(CID_CON, _nd_2);
      _xs_0 = r0;
      _acc_1 = r1;
      WL_AGAIN(spin_53);
    }
  break;
  }
  o[0] = _v_12;
  return 1;
}

INLINE Term spin_52(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3, Term r4) {
  u32 wpoll = 0;
  Term _v_10 = 0;
  Term _hs_1 = r0;
  Term _h_4 = r1;
  Term _h_5 = r2;
  u32 _go_1 = r3;
  Term _acc_0 = r4;
  WL_SPIN
    if (term_aux(_hs_1) == CID_NIL) {
      u64 _nd_0 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_0 + 0] = _h_4;
      e.mem[_nd_0 + 1] = _h_5;
      u64 _nd_1 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_1 + 0] = term_ctr(CID_CORE_HIT, _nd_0);
      e.mem[_nd_1 + 1] = term_pak(CID_NIL, 0);
      Term _v_11 = 0;
      Term _o_4[1];
      if (spin_53(e, _o_4, _acc_0, term_ctr(CID_CON, _nd_1)) == 0) {
        return 0;
      }
      _v_11 = _o_4[0];
      _v_10 = _v_11;
    } else {
      u64 _sp_3 = term_loc(_hs_1);
      Term _f_6 = e.mem[_sp_3 + 0];
      Term _f_7 = e.mem[_sp_3 + 1];
      u64 _sp_4 = term_loc(_f_6);
      Term _f_8 = e.mem[_sp_4 + 0];
      Term _f_9 = e.mem[_sp_4 + 1];
      heap_free(e, cls_fit(2), _sp_4);
      if (_go_1 == 0) {
        u64 _nd_3 = _sp_3;
        e.mem[_nd_3 + 0] = _f_8;
        e.mem[_nd_3 + 1] = _f_9;
        u64 _nd_4 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_4 + 0] = term_ctr(CID_CORE_HIT, _nd_3);
        e.mem[_nd_4 + 1] = _f_7;
        u64 _nd_5 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_5 + 0] = _h_4;
        e.mem[_nd_5 + 1] = _h_5;
        u64 _nd_6 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_6 + 0] = term_ctr(CID_CORE_HIT, _nd_5);
        e.mem[_nd_6 + 1] = term_ctr(CID_CON, _nd_4);
        Term _v_13 = 0;
        Term _o_5[1];
        if (spin_53(e, _o_5, _acc_0, term_ctr(CID_CON, _nd_6)) == 0) {
          return 0;
        }
        _v_13 = _o_5[0];
        _v_10 = _v_13;
      } else {
        u32 _v_14 = 0;
        u32 _v_15 = 0;
        Term _o_6[1];
        if (spin_50(e, _o_6, _h_4, _h_5, _f_7) == 0) {
          return 0;
        }
        _v_15 = _o_6[0];
        _v_14 = _v_15;
        u64 _nd_7 = _sp_3;
        e.mem[_nd_7 + 0] = _f_8;
        e.mem[_nd_7 + 1] = _f_9;
        u64 _nd_8 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_8 + 0] = term_ctr(CID_CORE_HIT, _nd_7);
        e.mem[_nd_8 + 1] = _acc_0;
        r0 = _f_7;
        r1 = _h_4;
        r2 = _h_5;
        r3 = _v_14;
        r4 = term_ctr(CID_CON, _nd_8);
        _hs_1 = r0;
        _h_4 = r1;
        _h_5 = r2;
        _go_1 = r3;
        _acc_0 = r4;
        WL_AGAIN(spin_52);
      }
    }
  break;
  }
  o[0] = _v_10;
  return 1;
}

INLINE Term spin_54(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9, u32 r10, Term r11, Term r12) {
  u32 wpoll = 0;
  u32 _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  Term _j_5 = r0;
  Term _j_6 = r1;
  Term _j_7 = r2;
  Term _j_8 = r3;
  Term _j_9 = r4;
  Term _e_1 = r5;
  Term _a_0 = r6;
  Term _t_0 = r7;
  Term _t_1 = r8;
  Term _c_0 = r9;
  u32 _c_1 = r10;
  Term _i_0 = r11;
  Term _k_0 = r12;
  WL_SPIN
    Term _v_24 = 0;
    u32 _v_25 = 0;
    u32 _v_26 = 0;
    Term _v_27 = 0;
    u32 _v_28 = 0;
    Term _v_29 = 0;
    u32 _v_30 = 0;
    u32 _v_31 = 0;
    Term _v_32 = 0;
    u32 _v_33 = 0;
    Term _o_0[5];
    if (spin_1(e, _o_0, _e_1, _c_0, _c_1, _a_0) == 0) {
      return 0;
    }
    _v_29 = _o_0[0];
    _v_30 = _o_0[1];
    _v_31 = _o_0[2];
    _v_32 = _o_0[3];
    _v_33 = _o_0[4];
    _v_24 = _v_29;
    _v_25 = _v_30;
    _v_26 = _v_31;
    _v_27 = _v_32;
    _v_28 = _v_33;
    u32 _v_34 = 0;
    Term _v_35 = 0;
    Term _v_36 = 0;
    Term _v_37 = 0;
    Term _v_38 = 0;
    Term _v_39 = 0;
    Term _v_40 = 0;
    Term _v_41 = 0;
    Term _o_1[8];
    if (spin_47(e, _o_1, _j_5, _j_6, _j_7, _j_8, _j_9, _i_0, _k_0, _t_0, _t_1, _v_24, _v_25, _v_26, _v_27, _v_28) == 0) {
      return 0;
    }
    _v_34 = _o_1[0];
    _v_35 = _o_1[1];
    _v_36 = _o_1[2];
    _v_37 = _o_1[3];
    _v_38 = _o_1[4];
    _v_39 = _o_1[5];
    _v_40 = _o_1[6];
    _v_41 = _o_1[7];
    _v_16 = _v_34;
    _v_17 = _v_35;
    _v_18 = _v_36;
    _v_19 = _v_37;
    _v_20 = _v_38;
    _v_21 = _v_39;
    _v_22 = _v_40;
    _v_23 = _v_41;
  break;
  }
  o[0] = _v_16;
  o[1] = _v_17;
  o[2] = _v_18;
  o[3] = _v_19;
  o[4] = _v_20;
  o[5] = _v_21;
  o[6] = _v_22;
  o[7] = _v_23;
  return 1;
}

INLINE Term spin_55(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _v_8 = 0;
  Term _j_1 = r0;
  Term _k_0 = r1;
  Term _t_0 = r2;
  Term _t_1 = r3;
  WL_SPIN
    Term _at_0 = blk_loc(e.mem, _t_0);
    Term _at_1 = blk_at(_t_0, ((u64)(u32)(_j_1)), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    Term _v_9 = 0;
    Term _v_10 = 0;
    Term _v_11 = 0;
    Term _o_0[3];
    if (spin_48(e, _o_0, _j_1, _k_0, _t_1, _t_0, _c_0) == 0) {
      return 0;
    }
    _v_9 = _o_0[0];
    _v_10 = _o_0[1];
    _v_11 = _o_0[2];
    _v_6 = _v_9;
    _v_7 = _v_10;
    _v_8 = _v_11;
  break;
  }
  o[0] = _v_6;
  o[1] = _v_7;
  o[2] = _v_8;
  return 1;
}

INLINE Term spin_59(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_40 = 0;
  u32 _v_41 = 0;
  u32 _v_42 = 0;
  Term _p_3 = r0;
  Term _len_4 = r1;
  Term _r_0 = r2;
  u32 _r_1 = r3;
  WL_SPIN
    _v_40 = _r_0;
    _v_41 = _r_1;
    _v_42 = (_p_3 < _len_4);
  break;
  }
  o[0] = _v_40;
  o[1] = _v_41;
  o[2] = _v_42;
  return 1;
}

INLINE Term spin_58(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_30 = 0;
  u32 _v_31 = 0;
  u32 _v_32 = 0;
  Term _a_2 = r0;
  Term _len_3 = r1;
  Term _p_2 = r2;
  WL_SPIN
    Term _v_33 = 0;
    u32 _v_34 = 0;
    Term _v_35 = 0;
    u32 _v_36 = 0;
    Term _o_1[2];
    if (spin_3(e, _o_1, _a_2, _p_2) == 0) {
      return 0;
    }
    _v_35 = _o_1[0];
    _v_36 = _o_1[1];
    _v_33 = _v_35;
    _v_34 = _v_36;
    Term _v_37 = 0;
    u32 _v_38 = 0;
    u32 _v_39 = 0;
    Term _o_2[3];
    if (spin_59(e, _o_2, _p_2, _len_3, _v_33, _v_34) == 0) {
      return 0;
    }
    _v_37 = _o_2[0];
    _v_38 = _o_2[1];
    _v_39 = _o_2[2];
    _v_30 = _v_37;
    _v_31 = _v_38;
    _v_32 = _v_39;
  break;
  }
  o[0] = _v_30;
  o[1] = _v_31;
  o[2] = _v_32;
  return 1;
}

INLINE Term spin_60(Env e, THR Term* o, u32 r0, Term r1, u32 r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_45 = 0;
  u32 _v_46 = 0;
  u32 _y_1 = r0;
  Term _x_4 = r1;
  u32 _x_5 = r2;
  u32 _x_6 = r3;
  WL_SPIN
    u32 _v_47 = 0;
    u32 _v_48 = 0;
    Term _o_4[1];
    if (spin_11(e, _o_4, _x_6, U32_BIN(_x_5, ==, _y_1)) == 0) {
      return 0;
    }
    _v_48 = _o_4[0];
    _v_47 = _v_48;
    _v_45 = _x_4;
    _v_46 = _v_47;
  break;
  }
  o[0] = _v_45;
  o[1] = _v_46;
  return 1;
}

INLINE Term spin_57(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_22 = 0;
  u32 _v_23 = 0;
  Term _a_1 = r0;
  Term _len_2 = r1;
  Term _p_1 = r2;
  u32 _y_0 = r3;
  WL_SPIN
    Term _v_24 = 0;
    u32 _v_25 = 0;
    u32 _v_26 = 0;
    Term _v_27 = 0;
    u32 _v_28 = 0;
    u32 _v_29 = 0;
    Term _o_3[3];
    if (spin_58(e, _o_3, _a_1, _len_2, _p_1) == 0) {
      return 0;
    }
    _v_27 = _o_3[0];
    _v_28 = _o_3[1];
    _v_29 = _o_3[2];
    _v_24 = _v_27;
    _v_25 = _v_28;
    _v_26 = _v_29;
    Term _v_43 = 0;
    u32 _v_44 = 0;
    Term _o_5[2];
    if (spin_60(e, _o_5, _y_0, _v_24, _v_25, _v_26) == 0) {
      return 0;
    }
    _v_43 = _o_5[0];
    _v_44 = _o_5[1];
    _v_22 = _v_43;
    _v_23 = _v_44;
  break;
  }
  o[0] = _v_22;
  o[1] = _v_23;
  return 1;
}

INLINE Term spin_65(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_89 = 0;
  u32 _v_90 = 0;
  u32 _v_91 = 0;
  Term _p_8 = r0;
  Term _len_9 = r1;
  Term _r_4 = r2;
  u32 _r_5 = r3;
  WL_SPIN
    u32 _v_92 = 0;
    u32 _v_93 = 0;
    u32 _v_94 = 0;
    Term _o_8[1];
    if (spin_11(e, _o_8, U32_BIN(_r_5, >=, 48ull), U32_BIN(_r_5, <=, 57ull)) == 0) {
      return 0;
    }
    _v_94 = _o_8[0];
    _v_93 = _v_94;
    u32 _v_95 = 0;
    Term _o_9[1];
    if (spin_11(e, _o_9, (_p_8 < _len_9), _v_93) == 0) {
      return 0;
    }
    _v_95 = _o_9[0];
    _v_92 = _v_95;
    _v_89 = _r_4;
    _v_90 = _r_5;
    _v_91 = _v_92;
  break;
  }
  o[0] = _v_89;
  o[1] = _v_90;
  o[2] = _v_91;
  return 1;
}

INLINE Term spin_64(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_79 = 0;
  u32 _v_80 = 0;
  u32 _v_81 = 0;
  Term _a_5 = r0;
  Term _len_8 = r1;
  Term _p_7 = r2;
  WL_SPIN
    Term _v_82 = 0;
    u32 _v_83 = 0;
    Term _v_84 = 0;
    u32 _v_85 = 0;
    Term _o_7[2];
    if (spin_3(e, _o_7, _a_5, _p_7) == 0) {
      return 0;
    }
    _v_84 = _o_7[0];
    _v_85 = _o_7[1];
    _v_82 = _v_84;
    _v_83 = _v_85;
    Term _v_86 = 0;
    u32 _v_87 = 0;
    u32 _v_88 = 0;
    Term _o_10[3];
    if (spin_65(e, _o_10, _p_7, _len_8, _v_82, _v_83) == 0) {
      return 0;
    }
    _v_86 = _o_10[0];
    _v_87 = _o_10[1];
    _v_88 = _o_10[2];
    _v_79 = _v_86;
    _v_80 = _v_87;
    _v_81 = _v_88;
  break;
  }
  o[0] = _v_79;
  o[1] = _v_80;
  o[2] = _v_81;
  return 1;
}

INLINE Term spin_67(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_107 = 0;
  Term _a_6 = r0;
  Term _b_0 = r1;
  WL_SPIN
    Term _v_108 = 0;
    Term _o_12[1];
    if (spin_41(e, _o_12, (_a_6 < _b_0), _a_6, _b_0) == 0) {
      return 0;
    }
    _v_108 = _o_12[0];
    _v_107 = _v_108;
  break;
  }
  o[0] = _v_107;
  return 1;
}

INLINE Term spin_66(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4, Term r5, u32 r6, u32 r7) {
  u32 wpoll = 0;
  Term _v_101 = 0;
  Term _v_102 = 0;
  Term _v_103 = 0;
  u32 _v_104 = 0;
  Term _fuel_0 = r0;
  Term _len_10 = r1;
  Term _p_9 = r2;
  Term _v_100 = r3;
  u32 _any_0 = r4;
  Term _x_7 = r5;
  u32 _x_8 = r6;
  u32 _x_9 = r7;
  WL_SPIN
    if (_fuel_0 == 0) {
      _v_101 = _x_7;
      _v_102 = _v_100;
      _v_103 = _p_9;
      _v_104 = _any_0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_9 == 0) {
        _v_101 = _x_7;
        _v_102 = _v_100;
        _v_103 = _p_9;
        _v_104 = _any_0;
      } else {
        Term _v_105 = 0;
        Term _v_106 = 0;
        Term _o_13[1];
        if (spin_67(e, _o_13, nat_mul(e, 1048576ull, 1048576ull), nat_chk(e, nat_mul(e, _v_100, 10ull) + U32_BIN(_x_8, -, 48ull))) == 0) {
          return 0;
        }
        _v_106 = _o_13[0];
        _v_105 = _v_106;
        Term _v_109 = 0;
        u32 _v_110 = 0;
        u32 _v_111 = 0;
        Term _v_112 = 0;
        u32 _v_113 = 0;
        u32 _v_114 = 0;
        Term _o_14[3];
        if (spin_64(e, _o_14, _x_7, _len_10, nat_chk(e, _p_9 + 1)) == 0) {
          return 0;
        }
        _v_112 = _o_14[0];
        _v_113 = _o_14[1];
        _v_114 = _o_14[2];
        _v_109 = _v_112;
        _v_110 = _v_113;
        _v_111 = _v_114;
        r0 = _f_0;
        r1 = _len_10;
        r2 = nat_chk(e, _p_9 + 1);
        r3 = _v_105;
        r4 = 1;
        r5 = _v_109;
        r6 = _v_110;
        r7 = _v_111;
        _fuel_0 = r0;
        _len_10 = r1;
        _p_9 = r2;
        _v_100 = r3;
        _any_0 = r4;
        _x_7 = r5;
        _x_8 = r6;
        _x_9 = r7;
        WL_AGAIN(spin_66);
      }
    }
  break;
  }
  o[0] = _v_101;
  o[1] = _v_102;
  o[2] = _v_103;
  o[3] = _v_104;
  return 1;
}

INLINE Term spin_63(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_69 = 0;
  Term _v_70 = 0;
  Term _v_71 = 0;
  u32 _v_72 = 0;
  Term _a_4 = r0;
  Term _len_7 = r1;
  Term _p_6 = r2;
  WL_SPIN
    Term _v_73 = 0;
    u32 _v_74 = 0;
    u32 _v_75 = 0;
    Term _v_76 = 0;
    u32 _v_77 = 0;
    u32 _v_78 = 0;
    Term _o_11[3];
    if (spin_64(e, _o_11, _a_4, _len_7, _p_6) == 0) {
      return 0;
    }
    _v_76 = _o_11[0];
    _v_77 = _o_11[1];
    _v_78 = _o_11[2];
    _v_73 = _v_76;
    _v_74 = _v_77;
    _v_75 = _v_78;
    Term _v_96 = 0;
    Term _v_97 = 0;
    Term _v_98 = 0;
    u32 _v_99 = 0;
    Term _o_15[4];
    if (spin_66(e, _o_15, nat_chk(e, (_len_7 < _p_6 ? 0 : _len_7 - _p_6) + 1), _len_7, _p_6, 0, 0, _v_73, _v_74, _v_75) == 0) {
      return 0;
    }
    _v_96 = _o_15[0];
    _v_97 = _o_15[1];
    _v_98 = _o_15[2];
    _v_99 = _o_15[3];
    _v_69 = _v_96;
    _v_70 = _v_97;
    _v_71 = _v_98;
    _v_72 = _v_99;
  break;
  }
  o[0] = _v_69;
  o[1] = _v_70;
  o[2] = _v_71;
  o[3] = _v_72;
  return 1;
}

INLINE Term spin_62(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_61 = 0;
  Term _v_62 = 0;
  Term _v_63 = 0;
  u32 _v_64 = 0;
  u32 _ok_0 = r0;
  Term _len_6 = r1;
  Term _p_5 = r2;
  Term _a_3 = r3;
  WL_SPIN
    if (_ok_0 == 1) {
      Term _v_65 = 0;
      Term _v_66 = 0;
      Term _v_67 = 0;
      u32 _v_68 = 0;
      Term _o_16[4];
      if (spin_63(e, _o_16, _a_3, _len_6, nat_chk(e, _p_5 + 1)) == 0) {
        return 0;
      }
      _v_65 = _o_16[0];
      _v_66 = _o_16[1];
      _v_67 = _o_16[2];
      _v_68 = _o_16[3];
      _v_61 = _v_65;
      _v_62 = _v_66;
      _v_63 = _v_67;
      _v_64 = _v_68;
    } else {
      _v_61 = _a_3;
      _v_62 = 0;
      _v_63 = _p_5;
      _v_64 = 0;
    }
  break;
  }
  o[0] = _v_61;
  o[1] = _v_62;
  o[2] = _v_63;
  o[3] = _v_64;
  return 1;
}

INLINE Term spin_61(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_53 = 0;
  Term _v_54 = 0;
  Term _v_55 = 0;
  u32 _v_56 = 0;
  Term _len_5 = r0;
  Term _p_4 = r1;
  Term _r_2 = r2;
  u32 _r_3 = r3;
  WL_SPIN
    Term _v_57 = 0;
    Term _v_58 = 0;
    Term _v_59 = 0;
    u32 _v_60 = 0;
    Term _o_17[4];
    if (spin_62(e, _o_17, _r_3, _len_5, _p_4, _r_2) == 0) {
      return 0;
    }
    _v_57 = _o_17[0];
    _v_58 = _o_17[1];
    _v_59 = _o_17[2];
    _v_60 = _o_17[3];
    _v_53 = _v_57;
    _v_54 = _v_58;
    _v_55 = _v_59;
    _v_56 = _v_60;
  break;
  }
  o[0] = _v_53;
  o[1] = _v_54;
  o[2] = _v_55;
  o[3] = _v_56;
  return 1;
}

INLINE Term spin_56(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  u32 _v_17 = 0;
  Term _a_0 = r0;
  Term _len_1 = r1;
  Term _p_0 = r2;
  WL_SPIN
    Term _v_18 = 0;
    u32 _v_19 = 0;
    Term _v_20 = 0;
    u32 _v_21 = 0;
    Term _o_6[2];
    if (spin_57(e, _o_6, _a_0, _len_1, _p_0, 32ull) == 0) {
      return 0;
    }
    _v_20 = _o_6[0];
    _v_21 = _o_6[1];
    _v_18 = _v_20;
    _v_19 = _v_21;
    Term _v_49 = 0;
    Term _v_50 = 0;
    Term _v_51 = 0;
    u32 _v_52 = 0;
    Term _o_18[4];
    if (spin_61(e, _o_18, _len_1, _p_0, _v_18, _v_19) == 0) {
      return 0;
    }
    _v_49 = _o_18[0];
    _v_50 = _o_18[1];
    _v_51 = _o_18[2];
    _v_52 = _o_18[3];
    _v_14 = _v_49;
    _v_15 = _v_50;
    _v_16 = _v_51;
    _v_17 = _v_52;
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  o[2] = _v_16;
  o[3] = _v_17;
  return 1;
}

INLINE Term spin_69(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  Term _v_132 = 0;
  Term _v_133 = 0;
  Term _v_134 = 0;
  u32 _v_135 = 0;
  Term _v_131 = r0;
  Term _p_10 = r1;
  u32 _ok_1 = r2;
  Term _r_6 = r3;
  u32 _r_7 = r4;
  WL_SPIN
    u32 _v_136 = 0;
    u32 _v_137 = 0;
    Term _o_21[1];
    if (spin_11(e, _o_21, _ok_1, _r_7) == 0) {
      return 0;
    }
    _v_137 = _o_21[0];
    _v_136 = _v_137;
    _v_132 = _r_6;
    _v_133 = _v_131;
    _v_134 = nat_chk(e, _p_10 + 1);
    _v_135 = _v_136;
  break;
  }
  o[0] = _v_132;
  o[1] = _v_133;
  o[2] = _v_134;
  o[3] = _v_135;
  return 1;
}

INLINE Term spin_68(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, u32 r5) {
  u32 wpoll = 0;
  Term _v_119 = 0;
  Term _v_120 = 0;
  Term _v_121 = 0;
  u32 _v_122 = 0;
  Term _len_11 = r0;
  u32 _y_2 = r1;
  Term _x_10 = r2;
  Term _x_11 = r3;
  Term _x_12 = r4;
  u32 _x_13 = r5;
  WL_SPIN
    Term _v_123 = 0;
    u32 _v_124 = 0;
    Term _v_125 = 0;
    u32 _v_126 = 0;
    Term _o_20[2];
    if (spin_57(e, _o_20, _x_10, _len_11, _x_12, _y_2) == 0) {
      return 0;
    }
    _v_125 = _o_20[0];
    _v_126 = _o_20[1];
    _v_123 = _v_125;
    _v_124 = _v_126;
    Term _v_127 = 0;
    Term _v_128 = 0;
    Term _v_129 = 0;
    u32 _v_130 = 0;
    Term _o_22[4];
    if (spin_69(e, _o_22, _x_11, _x_12, _x_13, _v_123, _v_124) == 0) {
      return 0;
    }
    _v_127 = _o_22[0];
    _v_128 = _o_22[1];
    _v_129 = _o_22[2];
    _v_130 = _o_22[3];
    _v_119 = _v_127;
    _v_120 = _v_128;
    _v_121 = _v_129;
    _v_122 = _v_130;
  break;
  }
  o[0] = _v_119;
  o[1] = _v_120;
  o[2] = _v_121;
  o[3] = _v_122;
  return 1;
}

INLINE Term spin_70(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3, Term r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_144 = 0;
  Term _v_145 = 0;
  Term _v_146 = 0;
  Term _v_147 = 0;
  Term _v_148 = 0;
  u32 _v_149 = 0;
  Term _id_1 = r0;
  Term _tf_0 = r1;
  u32 _ok1_1 = r2;
  Term _x_14 = r3;
  Term _x_15 = r4;
  Term _x_16 = r5;
  u32 _x_17 = r6;
  WL_SPIN
    u32 _v_150 = 0;
    u32 _v_151 = 0;
    Term _o_24[1];
    if (spin_11(e, _o_24, _ok1_1, _x_17) == 0) {
      return 0;
    }
    _v_151 = _o_24[0];
    _v_150 = _v_151;
    _v_144 = _x_14;
    _v_145 = _id_1;
    _v_146 = _tf_0;
    _v_147 = _x_15;
    _v_148 = _x_16;
    _v_149 = _v_150;
  break;
  }
  o[0] = _v_144;
  o[1] = _v_145;
  o[2] = _v_146;
  o[3] = _v_147;
  o[4] = _v_148;
  o[5] = _v_149;
  return 1;
}

INLINE Term spin_71(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _q_1 = r0;
  Term _w_1 = r1;
  Term _tot_1 = r2;
  Term _n_1 = r3;
  Term _x_2 = r4;
  Term _x_3 = r5;
  WL_SPIN
    Term _at_0 = blk_loc(e.mem, _x_2);
    Term _at_1 = blk_at(_x_2, ((u64)(u32)(_q_1)), 0);
    u32 _c_1 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    Term _v_6 = 0;
    Term _v_7 = 0;
    Term _o_0[2];
    if (spin_49(e, _o_0, _q_1, _w_1, _tot_1, _n_1, _x_3, _x_2, _c_1) == 0) {
      return 0;
    }
    _v_6 = _o_0[0];
    _v_7 = _o_0[1];
    _v_4 = _v_6;
    _v_5 = _v_7;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_72(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  u32 _go_0 = r0;
  Term _h_2 = r1;
  Term _h_3 = r2;
  Term _x_0 = r3;
  Term _x_1 = r4;
  Term _t_0 = r5;
  WL_SPIN
    if (_go_0 == 1) {
      u32 _v_7 = 0;
      u32 _v_8 = 0;
      Term _o_3[1];
      if (spin_50(e, _o_3, _h_2, _h_3, _t_0) == 0) {
        return 0;
      }
      _v_8 = _o_3[0];
      _v_7 = _v_8;
      Term _v_9 = 0;
      Term _o_4[1];
      if (spin_52(e, _o_4, _t_0, _h_2, _h_3, _v_7, term_pak(CID_NIL, 0)) == 0) {
        return 0;
      }
      _v_9 = _o_4[0];
      _v_6 = _v_9;
    } else {
      u64 _nd_0 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_0 + 0] = _x_0;
      e.mem[_nd_0 + 1] = _x_1;
      u64 _nd_1 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_1 + 0] = term_ctr(CID_CORE_HIT, _nd_0);
      e.mem[_nd_1 + 1] = _t_0;
      _v_6 = term_ctr(CID_CON, _nd_1);
    }
  break;
  }
  o[0] = _v_6;
  return 1;
}

INLINE Term spin_73(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _fuel_0 = r0;
  Term _j_0 = r1;
  Term _x_3 = r2;
  Term _x_4 = r3;
  Term _x_5 = r4;
  WL_SPIN
    if (_fuel_0 == 0) {
      _v_4 = _x_3;
      _v_5 = _x_4;
    } else {
      Term _g_0 = (_fuel_0 - 1);
      Term _v_6 = 0;
      Term _v_7 = 0;
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _v_10 = 0;
      Term _v_11 = 0;
      Term _o_0[3];
      if (spin_55(e, _o_0, _j_0, _x_5, _x_3, _x_4) == 0) {
        return 0;
      }
      _v_9 = _o_0[0];
      _v_10 = _o_0[1];
      _v_11 = _o_0[2];
      _v_6 = _v_9;
      _v_7 = _v_10;
      _v_8 = _v_11;
      r0 = _g_0;
      r1 = nat_chk(e, _j_0 + 1);
      r2 = _v_6;
      r3 = _v_7;
      r4 = _v_8;
      _fuel_0 = r0;
      _j_0 = r1;
      _x_3 = r2;
      _x_4 = r3;
      _x_5 = r4;
      WL_AGAIN(spin_73);
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_74(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7, Term r8, Term r9, Term r10, Term r11, Term r12, Term r13, Term r14) {
  u32 wpoll = 0;
  Term _v_15 = 0;
  u32 _v_16 = 0;
  Term _v_17 = 0;
  Term _fuel_1 = r0;
  Term _j_1 = r1;
  Term _j_2 = r2;
  Term _j_3 = r3;
  Term _j_4 = r4;
  Term _j_5 = r5;
  Term _e_1 = r6;
  u32 _x_6 = r7;
  Term _x_7 = r8;
  Term _x_8 = r9;
  Term _x_9 = r10;
  Term _x_10 = r11;
  Term _x_11 = r12;
  Term _x_12 = r13;
  Term _x_13 = r14;
  WL_SPIN
    if (_fuel_1 == 0) {
      if (_x_6 == 0) {
        term_sink(e, _x_8);
        term_sink(e, _x_9);
        _v_15 = _x_7;
        _v_16 = _x_10;
        _v_17 = _x_11;
      } else {
        term_sink(e, _x_8);
        term_sink(e, _x_9);
        _v_15 = _x_7;
        _v_16 = 2;
        _v_17 = 0;
      }
    } else {
      Term _g_1 = (_fuel_1 - 1);
      if (_x_6 == 0) {
        term_sink(e, _x_8);
        term_sink(e, _x_9);
        _v_15 = _x_7;
        _v_16 = _x_10;
        _v_17 = _x_11;
      } else {
        u32 _v_18 = 0;
        Term _v_19 = 0;
        Term _v_20 = 0;
        Term _v_21 = 0;
        Term _v_22 = 0;
        Term _v_23 = 0;
        Term _v_24 = 0;
        Term _v_25 = 0;
        u32 _v_26 = 0;
        Term _v_27 = 0;
        Term _v_28 = 0;
        Term _v_29 = 0;
        Term _v_30 = 0;
        Term _v_31 = 0;
        Term _v_32 = 0;
        Term _v_33 = 0;
        Term _o_2[8];
        if (spin_54(e, _o_2, _j_1, _j_2, _j_3, _j_4, _j_5, _e_1, _x_7, _x_8, _x_9, _x_10, _x_11, _x_12, _x_13) == 0) {
          return 0;
        }
        _v_26 = _o_2[0];
        _v_27 = _o_2[1];
        _v_28 = _o_2[2];
        _v_29 = _o_2[3];
        _v_30 = _o_2[4];
        _v_31 = _o_2[5];
        _v_32 = _o_2[6];
        _v_33 = _o_2[7];
        _v_18 = _v_26;
        _v_19 = _v_27;
        _v_20 = _v_28;
        _v_21 = _v_29;
        _v_22 = _v_30;
        _v_23 = _v_31;
        _v_24 = _v_32;
        _v_25 = _v_33;
        r0 = _g_1;
        r1 = _j_1;
        r2 = _j_2;
        r3 = _j_3;
        r4 = _j_4;
        r5 = _j_5;
        r6 = _e_1;
        r7 = _v_18;
        r8 = _v_19;
        r9 = _v_20;
        r10 = _v_21;
        r11 = _v_22;
        r12 = _v_23;
        r13 = _v_24;
        r14 = _v_25;
        _fuel_1 = r0;
        _j_1 = r1;
        _j_2 = r2;
        _j_3 = r3;
        _j_4 = r4;
        _j_5 = r5;
        _e_1 = r6;
        _x_6 = r7;
        _x_7 = r8;
        _x_8 = r9;
        _x_9 = r10;
        _x_10 = r11;
        _x_11 = r12;
        _x_12 = r13;
        _x_13 = r14;
        WL_AGAIN(spin_74);
      }
    }
  break;
  }
  o[0] = _v_15;
  o[1] = _v_16;
  o[2] = _v_17;
  return 1;
}

INLINE Term spin_75(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, u32 r5, u32 r6, u32 r7, u32 r8) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _fuel_0 = r0;
  Term _o_2 = r1;
  Term _s_1 = r2;
  Term _i_0 = r3;
  Term _x_0 = r4;
  u32 _x_1 = r5;
  u32 _x_2 = r6;
  u32 _x_3 = r7;
  u32 _x_4 = r8;
  WL_SPIN
    if (_fuel_0 == 0) {
      _v_12 = _x_0;
      _v_13 = 1200ull;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_3 == 0) {
        _v_12 = _x_0;
        _v_13 = _i_0;
      } else {
        Term _a_1 = 1ull;
        Term _v_14 = 0;
        u32 _v_15 = 0;
        u32 _v_16 = 0;
        u32 _v_17 = 0;
        u32 _v_18 = 0;
        Term _a_2 = 1ull;
        Term _v_19 = 0;
        u32 _v_20 = 0;
        u32 _v_21 = 0;
        u32 _v_22 = 0;
        u32 _v_23 = 0;
        Term _o_3[5];
        if (spin_2(e, _o_3, _x_0, nat_chk(e, _o_2 + nat_chk(e, _s_1 + (_i_0 < _a_2 ? 0 : _i_0 - _a_2)))) == 0) {
          return 0;
        }
        _v_19 = _o_3[0];
        _v_20 = _o_3[1];
        _v_21 = _o_3[2];
        _v_22 = _o_3[3];
        _v_23 = _o_3[4];
        _v_14 = _v_19;
        _v_15 = _v_20;
        _v_16 = _v_21;
        _v_17 = _v_22;
        _v_18 = _v_23;
        r0 = _f_0;
        r1 = _o_2;
        r2 = _s_1;
        r3 = (_i_0 < _a_1 ? 0 : _i_0 - _a_1);
        r4 = _v_14;
        r5 = _v_15;
        r6 = _v_16;
        r7 = _v_17;
        r8 = _v_18;
        _fuel_0 = r0;
        _o_2 = r1;
        _s_1 = r2;
        _i_0 = r3;
        _x_0 = r4;
        _x_1 = r5;
        _x_2 = r6;
        _x_3 = r7;
        _x_4 = r8;
        WL_AGAIN(spin_75);
      }
    }
  break;
  }
  o[0] = _v_12;
  o[1] = _v_13;
  return 1;
}

INLINE Term spin_76(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3, Term r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  u32 _v_19 = 0;
  Term _len_1 = r0;
  Term _id_0 = r1;
  u32 _ok1_0 = r2;
  Term _x_4 = r3;
  Term _x_5 = r4;
  Term _x_6 = r5;
  u32 _x_7 = r6;
  WL_SPIN
    u32 _v_20 = 0;
    u32 _v_21 = 0;
    Term _o_1[1];
    if (spin_11(e, _o_1, _ok1_0, _x_7) == 0) {
      return 0;
    }
    _v_21 = _o_1[0];
    _v_20 = _v_21;
    Term _v_22 = 0;
    Term _v_23 = 0;
    Term _v_24 = 0;
    u32 _v_25 = 0;
    Term _v_26 = 0;
    Term _v_27 = 0;
    Term _v_28 = 0;
    u32 _v_29 = 0;
    Term _v_30 = 0;
    Term _v_31 = 0;
    Term _v_32 = 0;
    u32 _v_33 = 0;
    Term _o_2[4];
    if (spin_56(e, _o_2, _x_4, _len_1, _x_6) == 0) {
      return 0;
    }
    _v_30 = _o_2[0];
    _v_31 = _o_2[1];
    _v_32 = _o_2[2];
    _v_33 = _o_2[3];
    _v_26 = _v_30;
    _v_27 = _v_31;
    _v_28 = _v_32;
    _v_29 = _v_33;
    Term _v_34 = 0;
    Term _v_35 = 0;
    Term _v_36 = 0;
    u32 _v_37 = 0;
    Term _o_3[4];
    if (spin_68(e, _o_3, _len_1, 10ull, _v_26, _v_27, _v_28, _v_29) == 0) {
      return 0;
    }
    _v_34 = _o_3[0];
    _v_35 = _o_3[1];
    _v_36 = _o_3[2];
    _v_37 = _o_3[3];
    _v_22 = _v_34;
    _v_23 = _v_35;
    _v_24 = _v_36;
    _v_25 = _v_37;
    Term _v_38 = 0;
    Term _v_39 = 0;
    Term _v_40 = 0;
    Term _v_41 = 0;
    Term _v_42 = 0;
    u32 _v_43 = 0;
    Term _o_4[6];
    if (spin_70(e, _o_4, _id_0, _x_5, _v_20, _v_22, _v_23, _v_24, _v_25) == 0) {
      return 0;
    }
    _v_38 = _o_4[0];
    _v_39 = _o_4[1];
    _v_40 = _o_4[2];
    _v_41 = _o_4[3];
    _v_42 = _o_4[4];
    _v_43 = _o_4[5];
    _v_14 = _v_38;
    _v_15 = _v_39;
    _v_16 = _v_40;
    _v_17 = _v_41;
    _v_18 = _v_42;
    _v_19 = _v_43;
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  o[2] = _v_16;
  o[3] = _v_17;
  o[4] = _v_18;
  o[5] = _v_19;
  return 1;
}

INLINE Term spin_81(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_13 = 0;
  Term _fuel_0 = r0;
  u32 _more_0 = r1;
  Term _n_1 = r2;
  Term _d_0 = r3;
  Term _w_0 = r4;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      _v_13 = _d_0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_more_0 == 1) {
        r0 = _f_0;
        r1 = (nat_mul(e, 2ull, _w_0) < _n_1);
        r2 = _n_1;
        r3 = nat_chk(e, _d_0 + 1);
        r4 = nat_mul(e, 2ull, _w_0);
        _fuel_0 = r0;
        _more_0 = r1;
        _n_1 = r2;
        _d_0 = r3;
        _w_0 = r4;
        WL_AGAIN(spin_81);
      } else {
        _v_13 = _d_0;
      }
    }
  break;
  }
  o[0] = _v_13;
  return 1;
}

INLINE Term spin_80(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_11 = 0;
  Term _n_0 = r0;
  WL_SPIN
    Term _v_12 = 0;
    Term _o_1[1];
    if (spin_81(e, _o_1, 48ull, (1ull < _n_0), _n_0, 0, 1ull) == 0) {
      return 0;
    }
    _v_12 = _o_1[0];
    _v_11 = _v_12;
  break;
  }
  o[0] = _v_11;
  return 1;
}

INLINE Term spin_82(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  Term _v_15 = 0;
  Term _fuel_2 = r0;
  Term _i_0 = r1;
  Term _dst_0 = r2;
  Term _r_2 = r3;
  u32 _r_3 = r4;
  WL_SPIN
    if (_fuel_2 == 0) {
      term_sink(e, _r_2);
      _v_15 = _dst_0;
    } else {
      Term _f_1 = (_fuel_2 - 1);
      Term _at_2 = blk_loc(e.mem, _dst_0);
      Term _at_3 = blk_at(_dst_0, ((u64)(u32)(_i_0)), 0);
      u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
      blk_write(e.mem, 0, _at_2, _at_3 + 0, _r_3);
      Term _at_4 = blk_loc(e.mem, _r_2);
      Term _at_5 = blk_at(_r_2, ((u64)(u32)(nat_chk(e, _i_0 + 1))), 0);
      u32 _c_2 = blk_read(e.mem, 0, _at_4, _at_5 + 0);
      r0 = _f_1;
      r1 = nat_chk(e, _i_0 + 1);
      r2 = _dst_0;
      r3 = _r_2;
      r4 = _c_2;
      _fuel_2 = r0;
      _i_0 = r1;
      _dst_0 = r2;
      _r_2 = r3;
      _r_3 = r4;
      WL_AGAIN(spin_82);
    }
  break;
  }
  o[0] = _v_15;
  return 1;
}

INLINE Term spin_79(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  u32 _big_0 = r0;
  Term _cap_0 = r1;
  Term _ev_0 = r2;
  WL_SPIN
    if (_big_0 == 1) {
      _v_8 = _ev_0;
    } else {
      Term _v_9 = 0;
      Term _v_10 = 0;
      Term _o_2[1];
      if (spin_80(e, _o_2, nat_mul(e, 2ull, _cap_0)) == 0) {
        return 0;
      }
      _v_10 = _o_2[0];
      _v_9 = _v_10;
      Term _fv_0[1];
      _fv_0[0] = 0ull;
      Term _at_0 = blk_loc(e.mem, _ev_0);
      Term _at_1 = blk_at(_ev_0, 0ull, 0);
      u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      Term _v_14 = 0;
      Term _o_3[1];
      if (spin_82(e, _o_3, _cap_0, 0, blk_new(e, 0, _v_9, 0, 1, _fv_0), _ev_0, _c_0) == 0) {
        return 0;
      }
      _v_14 = _o_3[0];
      _v_8 = _v_14;
    }
  break;
  }
  o[0] = _v_8;
  return 1;
}

INLINE Term spin_78(Env e, THR Term* o, Term r0, Term r1, u32 r2) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _need_1 = r0;
  Term _r_0 = r1;
  u32 _r_1 = r2;
  WL_SPIN
    u32 _v_5 = 0;
    u32 _v_6 = 0;
    Term _o_0[1];
    if (spin_39(e, _o_0, _need_1, _r_1) == 0) {
      return 0;
    }
    _v_6 = _o_0[0];
    _v_5 = _v_6;
    Term _v_7 = 0;
    Term _o_4[1];
    if (spin_79(e, _o_4, _v_5, _r_1, _r_0) == 0) {
      return 0;
    }
    _v_7 = _o_4[0];
    _v_4 = _v_7;
  break;
  }
  o[0] = _v_4;
  return 1;
}

INLINE Term spin_77(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _need_0 = r0;
  Term _pp_1 = r1;
  WL_SPIN
    Term _v_3 = 0;
    Term _o_5[1];
    if (spin_78(e, _o_5, _need_0, _pp_1, (1ull << (blk_cls(_pp_1) - 0))) == 0) {
      return 0;
    }
    _v_3 = _o_5[0];
    _v_2 = _v_3;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_83(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_18 = 0;
  Term _a_1 = r0;
  Term _b_0 = r1;
  WL_SPIN
    Term _v_19 = 0;
    Term _o_7[1];
    if (spin_41(e, _o_7, (_a_1 < _b_0), _b_0, _a_1) == 0) {
      return 0;
    }
    _v_19 = _o_7[0];
    _v_18 = _v_19;
  break;
  }
  o[0] = _v_18;
  return 1;
}

INLINE Term spin_84(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _c_0 = r0;
  Term _q_2 = r1;
  Term _w_0 = r2;
  Term _tot_2 = r3;
  Term _n_2 = r4;
  Term _x_0 = r5;
  Term _x_1 = r6;
  WL_SPIN
    if (_c_0 == 0) {
      _v_4 = _x_0;
      _v_5 = _x_1;
    } else {
      Term _m_0 = (_c_0 - 1);
      Term _v_6 = 0;
      Term _v_7 = 0;
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _o_0[2];
      if (spin_71(e, _o_0, _q_2, _w_0, _tot_2, _n_2, _x_0, _x_1) == 0) {
        return 0;
      }
      _v_8 = _o_0[0];
      _v_9 = _o_0[1];
      _v_6 = _v_8;
      _v_7 = _v_9;
      r0 = _m_0;
      r1 = nat_chk(e, _q_2 + 3ull);
      r2 = _w_0;
      r3 = _tot_2;
      r4 = _n_2;
      r5 = _v_6;
      r6 = _v_7;
      _c_0 = r0;
      _q_2 = r1;
      _w_0 = r2;
      _tot_2 = r3;
      _n_2 = r4;
      _x_0 = r5;
      _x_1 = r6;
      WL_AGAIN(spin_84);
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_85(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _q_3 = r0;
  Term _x_2 = r1;
  Term _x_3 = r2;
  WL_SPIN
    _v_13 = _x_2;
    _v_14 = _x_3;
    _v_15 = _q_3;
  break;
  }
  o[0] = _v_13;
  o[1] = _v_14;
  o[2] = _v_15;
  return 1;
}

INLINE Term spin_86(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _c_1 = r0;
  Term _hs_0 = r1;
  Term _r_0 = r2;
  Term _r_1 = r3;
  WL_SPIN
    _v_4 = _r_0;
    _v_5 = _r_1;
    _v_6 = _c_1;
    _v_7 = _hs_0;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_87(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  u32 _room_1 = r0;
  Term _h_0 = r1;
  Term _h_1 = r2;
  Term _hs_1 = r3;
  WL_SPIN
    if (_room_1 == 1) {
      u32 _v_13 = 0;
      u32 _v_14 = 0;
      Term _o_2[1];
      if (spin_50(e, _o_2, _h_0, _h_1, _hs_1) == 0) {
        return 0;
      }
      _v_14 = _o_2[0];
      _v_13 = _v_14;
      Term _v_15 = 0;
      Term _o_3[1];
      if (spin_52(e, _o_3, _hs_1, _h_0, _h_1, _v_13, term_pak(CID_NIL, 0)) == 0) {
        return 0;
      }
      _v_15 = _o_3[0];
      _v_12 = _v_15;
    } else {
      if (term_aux(_hs_1) == CID_NIL) {
        _v_12 = term_pak(CID_NIL, 0);
      } else {
        u64 _sp_0 = term_loc(_hs_1);
        Term _f_0 = e.mem[_sp_0 + 0];
        Term _f_1 = e.mem[_sp_0 + 1];
        u64 _sp_1 = term_loc(_f_0);
        Term _f_2 = e.mem[_sp_1 + 0];
        Term _f_3 = e.mem[_sp_1 + 1];
        heap_free(e, cls_fit(2), _sp_1);
        u32 _v_16 = 0;
        u32 _v_17 = 0;
        Term _o_4[1];
        if (spin_51(e, _o_4, _h_0, _h_1, _f_2, _f_3) == 0) {
          return 0;
        }
        _v_17 = _o_4[0];
        _v_16 = _v_17;
        Term _v_18 = 0;
        Term _o_5[1];
        if (spin_72(e, _o_5, _v_16, _h_0, _h_1, _f_2, _f_3, _f_1) == 0) {
          return 0;
        }
        _v_18 = _o_5[0];
        _v_12 = _v_18;
        heap_free(e, cls_fit(2), _sp_0);
      }
    }
  break;
  }
  o[0] = _v_12;
  return 1;
}

INLINE Term spin_89(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, u32 r5, u32 r6, Term r7, u32 r8) {
  u32 wpoll = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _fuel_0 = r0;
  Term _e_0 = r1;
  Term _m_0 = r2;
  Term _pat_0 = r3;
  Term _x_0 = r4;
  u32 _x_1 = r5;
  u32 _x_2 = r6;
  Term _x_3 = r7;
  u32 _x_4 = r8;
  WL_SPIN
    if (_fuel_0 == 0) {
      _v_18 = _x_0;
      _v_19 = _pat_0;
      _v_20 = _m_0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_1 == 0) {
        _v_18 = _x_0;
        _v_19 = _pat_0;
        _v_20 = _m_0;
      } else {
        Term _at_0 = blk_loc(e.mem, _pat_0);
        Term _at_1 = blk_at(_pat_0, ((u64)(u32)(_m_0)), 0);
        u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
        blk_write(e.mem, 0, _at_0, _at_1 + 0, _x_2);
        Term _v_21 = 0;
        u32 _v_22 = 0;
        u32 _v_23 = 0;
        Term _v_24 = 0;
        u32 _v_25 = 0;
        Term _v_26 = 0;
        u32 _v_27 = 0;
        u32 _v_28 = 0;
        Term _v_29 = 0;
        u32 _v_30 = 0;
        Term _o_2[5];
        if (spin_1(e, _o_2, _e_0, _x_3, _x_4, _x_0) == 0) {
          return 0;
        }
        _v_26 = _o_2[0];
        _v_27 = _o_2[1];
        _v_28 = _o_2[2];
        _v_29 = _o_2[3];
        _v_30 = _o_2[4];
        _v_21 = _v_26;
        _v_22 = _v_27;
        _v_23 = _v_28;
        _v_24 = _v_29;
        _v_25 = _v_30;
        r0 = _f_0;
        r1 = _e_0;
        r2 = nat_chk(e, _m_0 + 1);
        r3 = _pat_0;
        r4 = _v_21;
        r5 = _v_22;
        r6 = _v_23;
        r7 = _v_24;
        r8 = _v_25;
        _fuel_0 = r0;
        _e_0 = r1;
        _m_0 = r2;
        _pat_0 = r3;
        _x_0 = r4;
        _x_1 = r5;
        _x_2 = r6;
        _x_3 = r7;
        _x_4 = r8;
        WL_AGAIN(spin_89);
      }
    }
  break;
  }
  o[0] = _v_18;
  o[1] = _v_19;
  o[2] = _v_20;
  return 1;
}

INLINE Term spin_90(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7) {
  u32 wpoll = 0;
  Term _v_34 = 0;
  u32 _v_35 = 0;
  Term _v_36 = 0;
  Term _eo_1 = r0;
  Term _el_1 = r1;
  Term _qo_1 = r2;
  Term _ql_1 = r3;
  Term _d_0 = r4;
  Term _x_5 = r5;
  Term _x_6 = r6;
  Term _x_7 = r7;
  WL_SPIN
    if (_x_7 == 0) {
      term_sink(e, _x_6);
      _v_34 = _x_5;
      _v_35 = 1;
      _v_36 = 0;
    } else {
      Term _mm_0 = (_x_7 - 1);
      Term _v_37 = 0;
      Term _v_38 = 0;
      Term _fv_1[1];
      _fv_1[0] = 0ull;
      Term _v_39 = 0;
      Term _v_40 = 0;
      Term _o_4[2];
      if (spin_73(e, _o_4, _mm_0, 1ull, _x_6, blk_new(e, 0, _d_0, 0, 1, _fv_1), 0) == 0) {
        return 0;
      }
      _v_39 = _o_4[0];
      _v_40 = _o_4[1];
      _v_37 = _v_39;
      _v_38 = _v_40;
      Term _e_1 = nat_chk(e, _eo_1 + _el_1);
      Term _v_41 = 0;
      u32 _v_42 = 0;
      Term _v_43 = 0;
      Term _o_5[3];
      if (spin_74(e, _o_5, nat_chk(e, nat_mul(e, 2ull, _el_1) + 1), _eo_1, _el_1, _qo_1, _ql_1, nat_chk(e, _mm_0 + 1), _e_1, 1, _x_5, _v_37, _v_38, _eo_1, 0, 0, 0) == 0) {
        return 0;
      }
      _v_41 = _o_5[0];
      _v_42 = _o_5[1];
      _v_43 = _o_5[2];
      _v_34 = _v_41;
      _v_35 = _v_42;
      _v_36 = _v_43;
    }
  break;
  }
  o[0] = _v_34;
  o[1] = _v_35;
  o[2] = _v_36;
  return 1;
}

INLINE Term spin_91(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _v_3 = 0;
  Term _o_1 = r0;
  Term _s_1 = r1;
  Term _ws_1 = r2;
  Term _a_0 = r3;
  WL_SPIN
    if (_ws_1 == 0) {
      Term _v_4 = 0;
      u32 _v_5 = 0;
      u32 _v_6 = 0;
      u32 _v_7 = 0;
      u32 _v_8 = 0;
      Term _v_9 = 0;
      u32 _v_10 = 0;
      u32 _v_11 = 0;
      u32 _v_12 = 0;
      u32 _v_13 = 0;
      Term _o_2[5];
      if (spin_2(e, _o_2, _a_0, nat_chk(e, _o_1 + nat_chk(e, _s_1 + 1200ull))) == 0) {
        return 0;
      }
      _v_9 = _o_2[0];
      _v_10 = _o_2[1];
      _v_11 = _o_2[2];
      _v_12 = _o_2[3];
      _v_13 = _o_2[4];
      _v_4 = _v_9;
      _v_5 = _v_10;
      _v_6 = _v_11;
      _v_7 = _v_12;
      _v_8 = _v_13;
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _o_3[2];
      if (spin_75(e, _o_3, 1200ull, _o_1, _s_1, 1200ull, _v_4, _v_5, _v_6, _v_7, _v_8) == 0) {
        return 0;
      }
      _v_14 = _o_3[0];
      _v_15 = _o_3[1];
      _v_2 = _v_14;
      _v_3 = _v_15;
    } else {
      Term _w_0 = (_ws_1 - 1);
      _v_2 = _a_0;
      _v_3 = nat_chk(e, _w_0 + 1);
    }
  break;
  }
  o[0] = _v_2;
  o[1] = _v_3;
  return 1;
}

INLINE Term spin_92(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  u32 _v_19 = 0;
  Term _len_1 = r0;
  Term _x_0 = r1;
  Term _x_1 = r2;
  Term _x_2 = r3;
  u32 _x_3 = r4;
  WL_SPIN
    Term _v_20 = 0;
    Term _v_21 = 0;
    Term _v_22 = 0;
    u32 _v_23 = 0;
    Term _v_24 = 0;
    Term _v_25 = 0;
    Term _v_26 = 0;
    u32 _v_27 = 0;
    Term _o_1[4];
    if (spin_56(e, _o_1, _x_0, _len_1, _x_2) == 0) {
      return 0;
    }
    _v_24 = _o_1[0];
    _v_25 = _o_1[1];
    _v_26 = _o_1[2];
    _v_27 = _o_1[3];
    _v_20 = _v_24;
    _v_21 = _v_25;
    _v_22 = _v_26;
    _v_23 = _v_27;
    Term _v_28 = 0;
    Term _v_29 = 0;
    Term _v_30 = 0;
    Term _v_31 = 0;
    Term _v_32 = 0;
    u32 _v_33 = 0;
    Term _o_2[6];
    if (spin_76(e, _o_2, _len_1, _x_1, _x_3, _v_20, _v_21, _v_22, _v_23) == 0) {
      return 0;
    }
    _v_28 = _o_2[0];
    _v_29 = _o_2[1];
    _v_30 = _o_2[2];
    _v_31 = _o_2[3];
    _v_32 = _o_2[4];
    _v_33 = _o_2[5];
    _v_14 = _v_28;
    _v_15 = _v_29;
    _v_16 = _v_30;
    _v_17 = _v_31;
    _v_18 = _v_32;
    _v_19 = _v_33;
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  o[2] = _v_16;
  o[3] = _v_17;
  o[4] = _v_18;
  o[5] = _v_19;
  return 1;
}

INLINE Term spin_93(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9) {
  u32 wpoll = 0;
  Term _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  u32 _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  u32 _ok_0 = r0;
  Term _q_1 = r1;
  Term _mtf_1 = r2;
  Term _mdl_1 = r3;
  Term _pp_1 = r4;
  Term _id_0 = r5;
  Term _tf_0 = r6;
  Term _dl_0 = r7;
  Term _p_0 = r8;
  Term _a_0 = r9;
  WL_SPIN
    if (_ok_0 == 0) {
      _v_11 = _a_0;
      _v_12 = _p_0;
      _v_13 = _pp_1;
      _v_14 = _q_1;
      _v_15 = 0;
      _v_16 = _mtf_1;
      _v_17 = _mdl_1;
    } else {
      Term _v_18 = 0;
      Term _v_19 = 0;
      Term _o_2[1];
      if (spin_77(e, _o_2, nat_chk(e, _q_1 + 3ull), _pp_1) == 0) {
        return 0;
      }
      _v_19 = _o_2[0];
      _v_18 = _v_19;
      Term _at_0 = blk_loc(e.mem, _v_18);
      Term _at_1 = blk_at(_v_18, ((u64)(u32)(_q_1)), 0);
      u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      blk_write(e.mem, 0, _at_0, _at_1 + 0, ((u64)(u32)(_id_0)));
      Term _at_2 = blk_loc(e.mem, _v_18);
      Term _at_3 = blk_at(_v_18, ((u64)(u32)(nat_chk(e, _q_1 + 1))), 0);
      u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
      blk_write(e.mem, 0, _at_2, _at_3 + 0, ((u64)(u32)(_tf_0)));
      Term _at_4 = blk_loc(e.mem, _v_18);
      Term _at_5 = blk_at(_v_18, ((u64)(u32)(nat_chk(e, _q_1 + 2ull))), 0);
      u32 _c_2 = blk_read(e.mem, 0, _at_4, _at_5 + 0);
      blk_write(e.mem, 0, _at_4, _at_5 + 0, ((u64)(u32)(_dl_0)));
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _o_3[1];
      if (spin_83(e, _o_3, _mtf_1, _tf_0) == 0) {
        return 0;
      }
      _v_21 = _o_3[0];
      _v_20 = _v_21;
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _o_4[1];
      if (spin_83(e, _o_4, _mdl_1, _dl_0) == 0) {
        return 0;
      }
      _v_23 = _o_4[0];
      _v_22 = _v_23;
      _v_11 = _a_0;
      _v_12 = _p_0;
      _v_13 = _v_18;
      _v_14 = nat_chk(e, _q_1 + 3ull);
      _v_15 = 1;
      _v_16 = _v_20;
      _v_17 = _v_22;
    }
  break;
  }
  o[0] = _v_11;
  o[1] = _v_12;
  o[2] = _v_13;
  o[3] = _v_14;
  o[4] = _v_15;
  o[5] = _v_16;
  o[6] = _v_17;
  return 1;
}

INLINE Term spin_94(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  Term _k_1 = r0;
  Term _i_1 = r1;
  Term _x_4 = r2;
  Term _x_5 = r3;
  Term _x_6 = r4;
  Term _x_7 = r5;
  WL_SPIN
    if (_x_5 == 0) {
      Term _at_0 = blk_loc(e.mem, _x_4);
      Term _at_1 = blk_at(_x_4, ((u64)(u32)(nat_chk(e, _i_1 + 1))), 0);
      Term _c_0 = blk_read(e.mem, 1, _at_0, _at_1 + 0);
      Term _v_12 = 0;
      Term _v_13 = 0;
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _o_0[4];
      if (spin_86(e, _o_0, _x_6, _x_7, _x_4, _c_0) == 0) {
        return 0;
      }
      _v_12 = _o_0[0];
      _v_13 = _o_0[1];
      _v_14 = _o_0[2];
      _v_15 = _o_0[3];
      _v_8 = _v_12;
      _v_9 = _v_13;
      _v_10 = _v_14;
      _v_11 = _v_15;
    } else {
      Term _w_0 = (_x_5 - 1);
      u32 _room_0 = (_x_6 < _k_1);
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _o_1[1];
      if (spin_41(e, _o_1, _room_0, nat_chk(e, _x_6 + 1), _x_6) == 0) {
        return 0;
      }
      _v_17 = _o_1[0];
      _v_16 = _v_17;
      Term _v_18 = 0;
      Term _v_19 = 0;
      Term _o_2[1];
      if (spin_87(e, _o_2, _room_0, nat_chk(e, _w_0 + 1), _i_1, _x_7) == 0) {
        return 0;
      }
      _v_19 = _o_2[0];
      _v_18 = _v_19;
      Term _at_2 = blk_loc(e.mem, _x_4);
      Term _at_3 = blk_at(_x_4, ((u64)(u32)(nat_chk(e, _i_1 + 1))), 0);
      Term _c_1 = blk_read(e.mem, 1, _at_2, _at_3 + 0);
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _o_3[4];
      if (spin_86(e, _o_3, _v_16, _v_18, _x_4, _c_1) == 0) {
        return 0;
      }
      _v_20 = _o_3[0];
      _v_21 = _o_3[1];
      _v_22 = _o_3[2];
      _v_23 = _o_3[3];
      _v_8 = _v_20;
      _v_9 = _v_21;
      _v_10 = _v_22;
      _v_11 = _v_23;
    }
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  return 1;
}

INLINE Term spin_95(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _xs_0 = r0;
  WL_SPIN
    Term _v_3 = 0;
    Term _o_0[1];
    if (spin_53(e, _o_0, _xs_0, term_pak(CID_NIL, 0)) == 0) {
      return 0;
    }
    _v_3 = _o_0[0];
    _v_2 = _v_3;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_96(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  u32 _v_7 = 0;
  Term _v_8 = 0;
  Term _a_1 = r0;
  Term _eo_1 = r1;
  Term _el_0 = r2;
  Term _qo_1 = r3;
  Term _ql_1 = r4;
  WL_SPIN
    Term _v_9 = 0;
    Term _v_10 = 0;
    Term _o_0[1];
    if (spin_80(e, _o_0, nat_chk(e, _ql_1 + 1)) == 0) {
      return 0;
    }
    _v_10 = _o_0[0];
    _v_9 = _v_10;
    Term _eq_0 = nat_chk(e, _qo_1 + _ql_1);
    Term _v_11 = 0;
    Term _v_12 = 0;
    Term _v_13 = 0;
    Term _fv_0[1];
    _fv_0[0] = 0ull;
    Term _v_14 = 0;
    u32 _v_15 = 0;
    u32 _v_16 = 0;
    Term _v_17 = 0;
    u32 _v_18 = 0;
    Term _v_19 = 0;
    u32 _v_20 = 0;
    u32 _v_21 = 0;
    Term _v_22 = 0;
    u32 _v_23 = 0;
    Term _o_1[5];
    if (spin_1(e, _o_1, _eq_0, _qo_1, 0, _a_1) == 0) {
      return 0;
    }
    _v_19 = _o_1[0];
    _v_20 = _o_1[1];
    _v_21 = _o_1[2];
    _v_22 = _o_1[3];
    _v_23 = _o_1[4];
    _v_14 = _v_19;
    _v_15 = _v_20;
    _v_16 = _v_21;
    _v_17 = _v_22;
    _v_18 = _v_23;
    Term _v_24 = 0;
    Term _v_25 = 0;
    Term _v_26 = 0;
    Term _o_2[3];
    if (spin_89(e, _o_2, nat_chk(e, nat_mul(e, 2ull, _ql_1) + 1), _eq_0, 0, blk_new(e, 0, _v_9, 0, 1, _fv_0), _v_14, _v_15, _v_16, _v_17, _v_18) == 0) {
      return 0;
    }
    _v_24 = _o_2[0];
    _v_25 = _o_2[1];
    _v_26 = _o_2[2];
    _v_11 = _v_24;
    _v_12 = _v_25;
    _v_13 = _v_26;
    Term _v_27 = 0;
    u32 _v_28 = 0;
    Term _v_29 = 0;
    Term _o_3[3];
    if (spin_90(e, _o_3, _eo_1, _el_0, _qo_1, _ql_1, _v_9, _v_11, _v_12, _v_13) == 0) {
      return 0;
    }
    _v_27 = _o_3[0];
    _v_28 = _o_3[1];
    _v_29 = _o_3[2];
    _v_6 = _v_27;
    _v_7 = _v_28;
    _v_8 = _v_29;
  break;
  }
  o[0] = _v_6;
  o[1] = _v_7;
  o[2] = _v_8;
  return 1;
}

INLINE Term spin_97(Env e, THR Term* o, Term r0, Term r1, u32 r2, Term r3) {
  u32 wpoll = 0;
  Term _v_34 = 0;
  Term _v_35 = 0;
  u32 _v_36 = 0;
  Term _v_37 = 0;
  Term _ev_0 = r0;
  Term _r_2 = r1;
  u32 _r_3 = r2;
  Term _r_4 = r3;
  WL_SPIN
    _v_34 = _r_2;
    _v_35 = _ev_0;
    _v_36 = _r_3;
    _v_37 = _r_4;
  break;
  }
  o[0] = _v_34;
  o[1] = _v_35;
  o[2] = _v_36;
  o[3] = _v_37;
  return 1;
}

INLINE Term spin_98(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, u32 r5, Term r6, Term r7, u32 r8, u32 r9, u32 r10, u32 r11) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _fuel_0 = r0;
  u32 _l1_0 = r1;
  Term _o_2 = r2;
  Term _s_1 = r3;
  Term _j_0 = r4;
  u32 _w1_0 = r5;
  Term _ws_0 = r6;
  Term _x_5 = r7;
  u32 _x_6 = r8;
  u32 _x_7 = r9;
  u32 _x_8 = r10;
  u32 _x_9 = r11;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _o_3[2];
      if (spin_91(e, _o_3, _o_2, _s_1, _ws_0, _x_5) == 0) {
        return 0;
      }
      _v_14 = _o_3[0];
      _v_15 = _o_3[1];
      _v_12 = _v_14;
      _v_13 = _v_15;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_l1_0 == 1) {
        if (_x_9 == 1) {
          _v_12 = _x_5;
          _v_13 = _j_0;
        } else {
          Term _a_4 = 1ull;
          Term _v_16 = 0;
          u32 _v_17 = 0;
          u32 _v_18 = 0;
          u32 _v_19 = 0;
          Term _o_4[1];
          if (spin_34(e, _o_4, _ws_0, 0) == 0) {
            return 0;
          }
          _v_19 = _o_4[0];
          _v_18 = _v_19;
          u32 _v_20 = 0;
          Term _o_5[1];
          if (spin_11(e, _o_5, _v_18, _w1_0) == 0) {
            return 0;
          }
          _v_20 = _o_5[0];
          _v_17 = _v_20;
          Term _v_21 = 0;
          Term _o_6[1];
          if (spin_41(e, _o_6, _v_17, _j_0, _ws_0) == 0) {
            return 0;
          }
          _v_21 = _o_6[0];
          _v_16 = _v_21;
          Term _v_22 = 0;
          u32 _v_23 = 0;
          u32 _v_24 = 0;
          u32 _v_25 = 0;
          u32 _v_26 = 0;
          Term _a_5 = nat_chk(e, _s_1 + _j_0);
          Term _a_6 = 3ull;
          Term _v_27 = 0;
          u32 _v_28 = 0;
          u32 _v_29 = 0;
          u32 _v_30 = 0;
          u32 _v_31 = 0;
          Term _o_7[5];
          if (spin_2(e, _o_7, _x_5, nat_chk(e, _o_2 + (_a_5 < _a_6 ? 0 : _a_5 - _a_6))) == 0) {
            return 0;
          }
          _v_27 = _o_7[0];
          _v_28 = _o_7[1];
          _v_29 = _o_7[2];
          _v_30 = _o_7[3];
          _v_31 = _o_7[4];
          _v_22 = _v_27;
          _v_23 = _v_28;
          _v_24 = _v_29;
          _v_25 = _v_30;
          _v_26 = _v_31;
          r0 = _f_0;
          r1 = 0;
          r2 = _o_2;
          r3 = _s_1;
          r4 = (_j_0 < _a_4 ? 0 : _j_0 - _a_4);
          r5 = _x_7;
          r6 = _v_16;
          r7 = _v_22;
          r8 = _v_23;
          r9 = _v_24;
          r10 = _v_25;
          r11 = _v_26;
          _fuel_0 = r0;
          _l1_0 = r1;
          _o_2 = r2;
          _s_1 = r3;
          _j_0 = r4;
          _w1_0 = r5;
          _ws_0 = r6;
          _x_5 = r7;
          _x_6 = r8;
          _x_7 = r9;
          _x_8 = r10;
          _x_9 = r11;
          WL_AGAIN(spin_98);
        }
      } else {
        Term _a_7 = 1ull;
        Term _v_32 = 0;
        u32 _v_33 = 0;
        u32 _v_34 = 0;
        u32 _v_35 = 0;
        Term _o_8[1];
        if (spin_34(e, _o_8, _ws_0, 0) == 0) {
          return 0;
        }
        _v_35 = _o_8[0];
        _v_34 = _v_35;
        u32 _v_36 = 0;
        Term _o_9[1];
        if (spin_11(e, _o_9, _v_34, _w1_0) == 0) {
          return 0;
        }
        _v_36 = _o_9[0];
        _v_33 = _v_36;
        Term _v_37 = 0;
        Term _o_10[1];
        if (spin_41(e, _o_10, _v_33, _j_0, _ws_0) == 0) {
          return 0;
        }
        _v_37 = _o_10[0];
        _v_32 = _v_37;
        Term _v_38 = 0;
        u32 _v_39 = 0;
        u32 _v_40 = 0;
        u32 _v_41 = 0;
        u32 _v_42 = 0;
        Term _a_8 = nat_chk(e, _s_1 + _j_0);
        Term _a_9 = 3ull;
        Term _v_43 = 0;
        u32 _v_44 = 0;
        u32 _v_45 = 0;
        u32 _v_46 = 0;
        u32 _v_47 = 0;
        Term _o_11[5];
        if (spin_2(e, _o_11, _x_5, nat_chk(e, _o_2 + (_a_8 < _a_9 ? 0 : _a_8 - _a_9))) == 0) {
          return 0;
        }
        _v_43 = _o_11[0];
        _v_44 = _o_11[1];
        _v_45 = _o_11[2];
        _v_46 = _o_11[3];
        _v_47 = _o_11[4];
        _v_38 = _v_43;
        _v_39 = _v_44;
        _v_40 = _v_45;
        _v_41 = _v_46;
        _v_42 = _v_47;
        r0 = _f_0;
        r1 = _x_9;
        r2 = _o_2;
        r3 = _s_1;
        r4 = (_j_0 < _a_7 ? 0 : _j_0 - _a_7);
        r5 = _x_7;
        r6 = _v_32;
        r7 = _v_38;
        r8 = _v_39;
        r9 = _v_40;
        r10 = _v_41;
        r11 = _v_42;
        _fuel_0 = r0;
        _l1_0 = r1;
        _o_2 = r2;
        _s_1 = r3;
        _j_0 = r4;
        _w1_0 = r5;
        _ws_0 = r6;
        _x_5 = r7;
        _x_6 = r8;
        _x_7 = r9;
        _x_8 = r10;
        _x_9 = r11;
        WL_AGAIN(spin_98);
      }
    }
  break;
  }
  o[0] = _v_12;
  o[1] = _v_13;
  return 1;
}

INLINE Term spin_99(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7, u32 r8, u32 r9, u32 r10) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _fuel_0 = r0;
  u32 _inr_0 = r1;
  Term _o_2 = r2;
  Term _p_0 = r3;
  Term _e_1 = r4;
  Term _raw_0 = r5;
  Term _x_0 = r6;
  u32 _x_1 = r7;
  u32 _x_2 = r8;
  u32 _x_3 = r9;
  u32 _x_4 = r10;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      _v_12 = _x_0;
      _v_13 = _raw_0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_inr_0 == 1) {
        if (_x_2 == 1) {
          _v_12 = _x_0;
          _v_13 = nat_chk(e, _p_0 + 1);
        } else {
          Term _v_14 = 0;
          u32 _v_15 = 0;
          u32 _v_16 = 0;
          u32 _v_17 = 0;
          u32 _v_18 = 0;
          Term _v_19 = 0;
          u32 _v_20 = 0;
          u32 _v_21 = 0;
          u32 _v_22 = 0;
          u32 _v_23 = 0;
          Term _o_3[5];
          if (spin_2(e, _o_3, _x_0, nat_chk(e, _o_2 + nat_chk(e, _p_0 + 1))) == 0) {
            return 0;
          }
          _v_19 = _o_3[0];
          _v_20 = _o_3[1];
          _v_21 = _o_3[2];
          _v_22 = _o_3[3];
          _v_23 = _o_3[4];
          _v_14 = _v_19;
          _v_15 = _v_20;
          _v_16 = _v_21;
          _v_17 = _v_22;
          _v_18 = _v_23;
          r0 = _f_0;
          r1 = (nat_chk(e, _p_0 + 1) < _e_1);
          r2 = _o_2;
          r3 = nat_chk(e, _p_0 + 1);
          r4 = _e_1;
          r5 = _raw_0;
          r6 = _v_14;
          r7 = _v_15;
          r8 = _v_16;
          r9 = _v_17;
          r10 = _v_18;
          _fuel_0 = r0;
          _inr_0 = r1;
          _o_2 = r2;
          _p_0 = r3;
          _e_1 = r4;
          _raw_0 = r5;
          _x_0 = r6;
          _x_1 = r7;
          _x_2 = r8;
          _x_3 = r9;
          _x_4 = r10;
          WL_AGAIN(spin_99);
        }
      } else {
        _v_12 = _x_0;
        _v_13 = _raw_0;
      }
    }
  break;
  }
  o[0] = _v_12;
  o[1] = _v_13;
  return 1;
}

INLINE Term spin_100(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _s_1 = r0;
  Term _e_1 = r1;
  Term _x_0 = r2;
  WL_SPIN
    Term _v_3 = 0;
    Term _v_4 = 0;
    Term _o_0[1];
    if (spin_83(e, _o_0, nat_chk(e, _s_1 + 1), _x_0) == 0) {
      return 0;
    }
    _v_4 = _o_0[0];
    _v_3 = _v_4;
    Term _v_5 = 0;
    Term _o_1[1];
    if (spin_67(e, _o_1, _e_1, _v_3) == 0) {
      return 0;
    }
    _v_5 = _o_1[0];
    _v_2 = _v_5;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_101(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  u32 _v_24 = 0;
  Term _a_0 = r0;
  Term _len_1 = r1;
  Term _p_0 = r2;
  WL_SPIN
    Term _v_25 = 0;
    Term _v_26 = 0;
    Term _v_27 = 0;
    u32 _v_28 = 0;
    Term _v_29 = 0;
    Term _v_30 = 0;
    Term _v_31 = 0;
    u32 _v_32 = 0;
    Term _o_0[4];
    if (spin_63(e, _o_0, _a_0, _len_1, _p_0) == 0) {
      return 0;
    }
    _v_29 = _o_0[0];
    _v_30 = _o_0[1];
    _v_31 = _o_0[2];
    _v_32 = _o_0[3];
    _v_25 = _v_29;
    _v_26 = _v_30;
    _v_27 = _v_31;
    _v_28 = _v_32;
    Term _v_33 = 0;
    Term _v_34 = 0;
    Term _v_35 = 0;
    Term _v_36 = 0;
    Term _v_37 = 0;
    u32 _v_38 = 0;
    Term _o_1[6];
    if (spin_92(e, _o_1, _len_1, _v_25, _v_26, _v_27, _v_28) == 0) {
      return 0;
    }
    _v_33 = _o_1[0];
    _v_34 = _o_1[1];
    _v_35 = _o_1[2];
    _v_36 = _o_1[3];
    _v_37 = _o_1[4];
    _v_38 = _o_1[5];
    _v_19 = _v_33;
    _v_20 = _v_34;
    _v_21 = _v_35;
    _v_22 = _v_36;
    _v_23 = _v_37;
    _v_24 = _v_38;
  break;
  }
  o[0] = _v_19;
  o[1] = _v_20;
  o[2] = _v_21;
  o[3] = _v_22;
  o[4] = _v_23;
  o[5] = _v_24;
  return 1;
}

INLINE Term spin_102(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9, u32 r10) {
  u32 wpoll = 0;
  Term _v_46 = 0;
  Term _v_47 = 0;
  Term _v_48 = 0;
  Term _v_49 = 0;
  u32 _v_50 = 0;
  Term _v_51 = 0;
  Term _v_52 = 0;
  Term _n_1 = r0;
  Term _q_0 = r1;
  Term _mtf_0 = r2;
  Term _mdl_0 = r3;
  Term _pp_0 = r4;
  Term _x_7 = r5;
  Term _x_8 = r6;
  Term _x_9 = r7;
  Term _x_10 = r8;
  Term _x_11 = r9;
  u32 _x_12 = r10;
  WL_SPIN
    u32 _v_53 = 0;
    u32 _v_54 = 0;
    u32 _v_55 = 0;
    Term _o_3[1];
    if (spin_11(e, _o_3, (_x_8 < _n_1), (0 < _x_9)) == 0) {
      return 0;
    }
    _v_55 = _o_3[0];
    _v_54 = _v_55;
    u32 _v_56 = 0;
    Term _o_4[1];
    if (spin_11(e, _o_4, _x_12, _v_54) == 0) {
      return 0;
    }
    _v_56 = _o_4[0];
    _v_53 = _v_56;
    Term _v_57 = 0;
    Term _v_58 = 0;
    Term _v_59 = 0;
    Term _v_60 = 0;
    u32 _v_61 = 0;
    Term _v_62 = 0;
    Term _v_63 = 0;
    Term _o_5[7];
    if (spin_93(e, _o_5, _v_53, _q_0, _mtf_0, _mdl_0, _pp_0, _x_8, _x_9, _x_10, _x_11, _x_7) == 0) {
      return 0;
    }
    _v_57 = _o_5[0];
    _v_58 = _o_5[1];
    _v_59 = _o_5[2];
    _v_60 = _o_5[3];
    _v_61 = _o_5[4];
    _v_62 = _o_5[5];
    _v_63 = _o_5[6];
    _v_46 = _v_57;
    _v_47 = _v_58;
    _v_48 = _v_59;
    _v_49 = _v_60;
    _v_50 = _v_61;
    _v_51 = _v_62;
    _v_52 = _v_63;
  break;
  }
  o[0] = _v_46;
  o[1] = _v_47;
  o[2] = _v_48;
  o[3] = _v_49;
  o[4] = _v_50;
  o[5] = _v_51;
  o[6] = _v_52;
  return 1;
}

INLINE Term spin_103(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6) {
  u32 wpoll = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _fuel_0 = r0;
  Term _k_1 = r1;
  Term _i_0 = r2;
  Term _x_3 = r3;
  Term _x_4 = r4;
  Term _x_5 = r5;
  Term _x_6 = r6;
  WL_SPIN
    if (_fuel_0 == 0) {
      _v_16 = _x_3;
      _v_17 = _x_4;
      _v_18 = _x_5;
      _v_19 = _x_6;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _o_1[4];
      if (spin_94(e, _o_1, _k_1, _i_0, _x_3, _x_4, _x_5, _x_6) == 0) {
        return 0;
      }
      _v_24 = _o_1[0];
      _v_25 = _o_1[1];
      _v_26 = _o_1[2];
      _v_27 = _o_1[3];
      _v_20 = _v_24;
      _v_21 = _v_25;
      _v_22 = _v_26;
      _v_23 = _v_27;
      r0 = _f_0;
      r1 = _k_1;
      r2 = nat_chk(e, _i_0 + 1);
      r3 = _v_20;
      r4 = _v_21;
      r5 = _v_22;
      r6 = _v_23;
      _fuel_0 = r0;
      _k_1 = r1;
      _i_0 = r2;
      _x_3 = r3;
      _x_4 = r4;
      _x_5 = r5;
      _x_6 = r6;
      WL_AGAIN(spin_103);
    }
  break;
  }
  o[0] = _v_16;
  o[1] = _v_17;
  o[2] = _v_18;
  o[3] = _v_19;
  return 1;
}

INLINE Term spin_104(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_30 = 0;
  Term _v_31 = 0;
  Term _p_0 = r0;
  Term _x_7 = r1;
  Term _x_8 = r2;
  Term _x_9 = r3;
  Term _x_10 = r4;
  WL_SPIN
    term_sink(e, _x_7);
    Term _v_32 = 0;
    Term _v_33 = 0;
    Term _o_3[1];
    if (spin_95(e, _o_3, _x_10) == 0) {
      return 0;
    }
    _v_33 = _o_3[0];
    _v_32 = _v_33;
    _v_30 = _p_0;
    _v_31 = _v_32;
  break;
  }
  o[0] = _v_30;
  o[1] = _v_31;
  return 1;
}

INLINE Term spin_107(Env e, THR Term* o, Term r0, u32 r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_22 = 0;
  Term _n_1 = r0;
  u32 _b_2 = r1;
  Term _r_1 = r2;
  u32 _r_2 = r3;
  WL_SPIN
    Term _v_23 = 0;
    Term _v_24 = 0;
    Term _o_2[1];
    if (spin_4(e, _o_2, _n_1, 4ull) == 0) {
      return 0;
    }
    _v_24 = _o_2[0];
    _v_23 = _v_24;
    Term _v_25 = 0;
    Term _v_26 = 0;
    Term _o_3[1];
    if (spin_7(e, _o_3, _n_1, 4ull) == 0) {
      return 0;
    }
    _v_26 = _o_3[0];
    _v_25 = _v_26;
    Term _a_0 = nat_mul(e, 8ull, _v_25);
    Term _at_2 = blk_loc(e.mem, _r_1);
    Term _at_3 = blk_at(_r_1, ((u64)(u32)(_v_23)), 0);
    u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
    blk_write(e.mem, 0, _at_2, _at_3 + 0, U32_BIN(_r_2, |, (_a_0 >= 32 ? 0 : U32_BIN(_b_2, <<, _a_0))));
    _v_22 = _r_1;
  break;
  }
  o[0] = _v_22;
  return 1;
}

INLINE Term spin_106(Env e, THR Term* o, Term r0, Term r1, u32 r2) {
  u32 wpoll = 0;
  Term _v_18 = 0;
  Term _o_0 = r0;
  Term _n_0 = r1;
  u32 _b_1 = r2;
  WL_SPIN
    Term _v_19 = 0;
    Term _v_20 = 0;
    Term _o_1[1];
    if (spin_4(e, _o_1, _n_0, 4ull) == 0) {
      return 0;
    }
    _v_20 = _o_1[0];
    _v_19 = _v_20;
    Term _at_0 = blk_loc(e.mem, _o_0);
    Term _at_1 = blk_at(_o_0, ((u64)(u32)(_v_19)), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    Term _v_21 = 0;
    Term _o_4[1];
    if (spin_107(e, _o_4, _n_0, _b_1, _o_0, _c_0) == 0) {
      return 0;
    }
    _v_21 = _o_4[0];
    _v_18 = _v_21;
  break;
  }
  o[0] = _v_18;
  return 1;
}

INLINE Term spin_105(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  u32 _b_0 = r0;
  Term _w_2 = r1;
  Term _w_3 = r2;
  WL_SPIN
    Term _v_16 = 0;
    Term _v_17 = 0;
    Term _o_5[1];
    if (spin_106(e, _o_5, _w_2, _w_3, _b_0) == 0) {
      return 0;
    }
    _v_17 = _o_5[0];
    _v_16 = _v_17;
    _v_14 = _v_16;
    _v_15 = nat_chk(e, _w_3 + 1);
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  return 1;
}

INLINE Term spin_109(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_37 = 0;
  Term _fuel_0 = r0;
  u32 _more_0 = r1;
  Term _v_36 = r2;
  Term _c_2 = r3;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      _v_37 = _c_2;
    } else {
      Term _f_4 = (_fuel_0 - 1);
      if (_more_0 == 1) {
        u32 _v_38 = 0;
        Term _v_39 = 0;
        Term _v_40 = 0;
        Term _o_8[1];
        if (spin_4(e, _o_8, _v_36, 10ull) == 0) {
          return 0;
        }
        _v_40 = _o_8[0];
        _v_39 = _v_40;
        u32 _v_41 = 0;
        Term _o_9[1];
        if (spin_39(e, _o_9, 10ull, _v_39) == 0) {
          return 0;
        }
        _v_41 = _o_9[0];
        _v_38 = _v_41;
        Term _v_42 = 0;
        Term _v_43 = 0;
        Term _o_10[1];
        if (spin_4(e, _o_10, _v_36, 10ull) == 0) {
          return 0;
        }
        _v_43 = _o_10[0];
        _v_42 = _v_43;
        r0 = _f_4;
        r1 = _v_38;
        r2 = _v_42;
        r3 = nat_chk(e, _c_2 + 1);
        _fuel_0 = r0;
        _more_0 = r1;
        _v_36 = r2;
        _c_2 = r3;
        WL_AGAIN(spin_109);
      } else {
        _v_37 = _c_2;
      }
    }
  break;
  }
  o[0] = _v_37;
  return 1;
}

INLINE Term spin_110(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_49 = 0;
  Term _fuel_2 = r0;
  Term _n_2 = r1;
  Term _v_48 = r2;
  Term _o_13 = r3;
  WL_SPIN
    if (_fuel_2 == 0) {
      _v_49 = _o_13;
    } else {
      Term _f_5 = (_fuel_2 - 1);
      Term _a_1 = 1ull;
      Term _v_50 = 0;
      Term _v_51 = 0;
      Term _o_14[1];
      if (spin_4(e, _o_14, _v_48, 10ull) == 0) {
        return 0;
      }
      _v_51 = _o_14[0];
      _v_50 = _v_51;
      Term _v_52 = 0;
      Term _a_2 = 1ull;
      Term _v_53 = 0;
      Term _v_54 = 0;
      Term _o_15[1];
      if (spin_7(e, _o_15, _v_48, 10ull) == 0) {
        return 0;
      }
      _v_54 = _o_15[0];
      _v_53 = _v_54;
      Term _v_55 = 0;
      Term _o_16[1];
      if (spin_106(e, _o_16, _o_13, (_n_2 < _a_2 ? 0 : _n_2 - _a_2), ((u64)(u32)(nat_chk(e, 48ull + _v_53)))) == 0) {
        return 0;
      }
      _v_55 = _o_16[0];
      _v_52 = _v_55;
      r0 = _f_5;
      r1 = (_n_2 < _a_1 ? 0 : _n_2 - _a_1);
      r2 = _v_50;
      r3 = _v_52;
      _fuel_2 = r0;
      _n_2 = r1;
      _v_48 = r2;
      _o_13 = r3;
      WL_AGAIN(spin_110);
    }
  break;
  }
  o[0] = _v_49;
  return 1;
}

INLINE Term spin_108(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_30 = 0;
  Term _v_31 = 0;
  Term _v_29 = r0;
  Term _w_4 = r1;
  Term _w_5 = r2;
  WL_SPIN
    Term _v_32 = 0;
    u32 _v_33 = 0;
    u32 _v_34 = 0;
    Term _o_7[1];
    if (spin_39(e, _o_7, 10ull, _v_29) == 0) {
      return 0;
    }
    _v_34 = _o_7[0];
    _v_33 = _v_34;
    Term _v_35 = 0;
    Term _o_11[1];
    if (spin_109(e, _o_11, 20ull, _v_33, _v_29, 1ull) == 0) {
      return 0;
    }
    _v_35 = _o_11[0];
    _v_32 = _v_35;
    Term _e_0 = nat_chk(e, nat_chk(e, _w_5 + 1) + _v_32);
    Term _v_44 = 0;
    Term _v_45 = 0;
    Term _v_46 = 0;
    Term _o_12[1];
    if (spin_106(e, _o_12, _w_4, _w_5, 32ull) == 0) {
      return 0;
    }
    _v_46 = _o_12[0];
    _v_45 = _v_46;
    Term _v_47 = 0;
    Term _o_17[1];
    if (spin_110(e, _o_17, _v_32, _e_0, _v_29, _v_45) == 0) {
      return 0;
    }
    _v_47 = _o_17[0];
    _v_44 = _v_47;
    _v_30 = _v_44;
    _v_31 = _e_0;
  break;
  }
  o[0] = _v_30;
  o[1] = _v_31;
  return 1;
}

INLINE Term spin_111(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, u32 r5) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  u32 _v_6 = 0;
  Term _v_7 = 0;
  Term _qo_1 = r0;
  Term _ql_1 = r1;
  Term _a_1 = r2;
  Term _eo_0 = r3;
  Term _r_2 = r4;
  u32 _r_3 = r5;
  WL_SPIN
    Term _v_8 = 0;
    u32 _v_9 = 0;
    Term _v_10 = 0;
    Term _v_11 = 0;
    u32 _v_12 = 0;
    Term _v_13 = 0;
    Term _o_0[3];
    if (spin_96(e, _o_0, _a_1, _eo_0, _r_3, _qo_1, _ql_1) == 0) {
      return 0;
    }
    _v_11 = _o_0[0];
    _v_12 = _o_0[1];
    _v_13 = _o_0[2];
    _v_8 = _v_11;
    _v_9 = _v_12;
    _v_10 = _v_13;
    Term _v_14 = 0;
    Term _v_15 = 0;
    u32 _v_16 = 0;
    Term _v_17 = 0;
    Term _o_1[4];
    if (spin_97(e, _o_1, _r_2, _v_8, _v_9, _v_10) == 0) {
      return 0;
    }
    _v_14 = _o_1[0];
    _v_15 = _o_1[1];
    _v_16 = _o_1[2];
    _v_17 = _o_1[3];
    _v_4 = _v_14;
    _v_5 = _v_15;
    _v_6 = _v_16;
    _v_7 = _v_17;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_112(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3, u32 r4, u32 r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _o_3 = r0;
  Term _s_1 = r1;
  Term _x_0 = r2;
  u32 _x_1 = r3;
  u32 _x_2 = r4;
  u32 _x_3 = r5;
  u32 _x_4 = r6;
  WL_SPIN
    Term _a_3 = 1200ull;
    Term _a_4 = 600ull;
    Term _v_18 = 0;
    u32 _v_19 = 0;
    u32 _v_20 = 0;
    u32 _v_21 = 0;
    u32 _v_22 = 0;
    Term _a_5 = 1200ull;
    Term _a_6 = 2ull;
    Term _v_23 = 0;
    u32 _v_24 = 0;
    u32 _v_25 = 0;
    u32 _v_26 = 0;
    u32 _v_27 = 0;
    Term _o_4[5];
    if (spin_2(e, _o_4, _x_0, nat_chk(e, _o_3 + nat_chk(e, _s_1 + (_a_5 < _a_6 ? 0 : _a_5 - _a_6)))) == 0) {
      return 0;
    }
    _v_23 = _o_4[0];
    _v_24 = _o_4[1];
    _v_25 = _o_4[2];
    _v_26 = _o_4[3];
    _v_27 = _o_4[4];
    _v_18 = _v_23;
    _v_19 = _v_24;
    _v_20 = _v_25;
    _v_21 = _v_26;
    _v_22 = _v_27;
    Term _v_28 = 0;
    Term _v_29 = 0;
    Term _o_5[2];
    if (spin_98(e, _o_5, (_a_3 < _a_4 ? 0 : _a_3 - _a_4), _x_4, _o_3, _s_1, 1200ull, _x_2, 0, _v_18, _v_19, _v_20, _v_21, _v_22) == 0) {
      return 0;
    }
    _v_28 = _o_5[0];
    _v_29 = _o_5[1];
    _v_16 = _v_28;
    _v_17 = _v_29;
  break;
  }
  o[0] = _v_16;
  o[1] = _v_17;
  return 1;
}

INLINE Term spin_113(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_32 = 0;
  Term _v_33 = 0;
  Term _s_2 = r0;
  Term _r_0 = r1;
  Term _r_1 = r2;
  WL_SPIN
    Term _v_34 = 0;
    Term _v_35 = 0;
    Term _o_7[1];
    if (spin_83(e, _o_7, nat_chk(e, _s_2 + 1), nat_chk(e, _s_2 + _r_1)) == 0) {
      return 0;
    }
    _v_35 = _o_7[0];
    _v_34 = _v_35;
    _v_32 = _r_0;
    _v_33 = _v_34;
  break;
  }
  o[0] = _v_32;
  o[1] = _v_33;
  return 1;
}

INLINE Term spin_114(Env e, THR Term* o, Term r0, u32 r1, Term r2, Term r3, Term r4, Term r5, u32 r6, u32 r7, u32 r8, u32 r9) {
  u32 wpoll = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _fuel_0 = r0;
  u32 _inr_0 = r1;
  Term _o_3 = r2;
  Term _p_0 = r3;
  Term _e_1 = r4;
  Term _x_0 = r5;
  u32 _x_1 = r6;
  u32 _x_2 = r7;
  u32 _x_3 = r8;
  u32 _x_4 = r9;
  WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      _v_18 = _x_0;
      _v_19 = _p_0;
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_inr_0 == 1) {
        if (_x_3 == 1) {
          Term _v_20 = 0;
          u32 _v_21 = 0;
          u32 _v_22 = 0;
          u32 _v_23 = 0;
          u32 _v_24 = 0;
          Term _v_25 = 0;
          u32 _v_26 = 0;
          u32 _v_27 = 0;
          u32 _v_28 = 0;
          u32 _v_29 = 0;
          Term _o_4[5];
          if (spin_2(e, _o_4, _x_0, nat_chk(e, _o_3 + nat_chk(e, _p_0 + 1))) == 0) {
            return 0;
          }
          _v_25 = _o_4[0];
          _v_26 = _o_4[1];
          _v_27 = _o_4[2];
          _v_28 = _o_4[3];
          _v_29 = _o_4[4];
          _v_20 = _v_25;
          _v_21 = _v_26;
          _v_22 = _v_27;
          _v_23 = _v_28;
          _v_24 = _v_29;
          r0 = _f_0;
          r1 = (nat_chk(e, _p_0 + 1) < _e_1);
          r2 = _o_3;
          r3 = nat_chk(e, _p_0 + 1);
          r4 = _e_1;
          r5 = _v_20;
          r6 = _v_21;
          r7 = _v_22;
          r8 = _v_23;
          r9 = _v_24;
          _fuel_0 = r0;
          _inr_0 = r1;
          _o_3 = r2;
          _p_0 = r3;
          _e_1 = r4;
          _x_0 = r5;
          _x_1 = r6;
          _x_2 = r7;
          _x_3 = r8;
          _x_4 = r9;
          WL_AGAIN(spin_114);
        } else {
          _v_18 = _x_0;
          _v_19 = _p_0;
        }
      } else {
        _v_18 = _x_0;
        _v_19 = _p_0;
      }
    }
  break;
  }
  o[0] = _v_18;
  o[1] = _v_19;
  return 1;
}

INLINE Term spin_115(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_32 = 0;
  Term _v_33 = 0;
  Term _o_6 = r0;
  Term _e_2 = r1;
  Term _r_0 = r2;
  Term _r_1 = r3;
  WL_SPIN
    Term _v_34 = 0;
    u32 _v_35 = 0;
    u32 _v_36 = 0;
    u32 _v_37 = 0;
    u32 _v_38 = 0;
    Term _v_39 = 0;
    u32 _v_40 = 0;
    u32 _v_41 = 0;
    u32 _v_42 = 0;
    u32 _v_43 = 0;
    Term _o_7[5];
    if (spin_2(e, _o_7, _r_0, nat_chk(e, _o_6 + _r_1)) == 0) {
      return 0;
    }
    _v_39 = _o_7[0];
    _v_40 = _o_7[1];
    _v_41 = _o_7[2];
    _v_42 = _o_7[3];
    _v_43 = _o_7[4];
    _v_34 = _v_39;
    _v_35 = _v_40;
    _v_36 = _v_41;
    _v_37 = _v_42;
    _v_38 = _v_43;
    Term _v_44 = 0;
    Term _v_45 = 0;
    Term _o_8[2];
    if (spin_99(e, _o_8, 200ull, (_r_1 < _e_2), _o_6, _r_1, _e_2, _r_1, _v_34, _v_35, _v_36, _v_37, _v_38) == 0) {
      return 0;
    }
    _v_44 = _o_8[0];
    _v_45 = _o_8[1];
    _v_32 = _v_44;
    _v_33 = _v_45;
  break;
  }
  o[0] = _v_32;
  o[1] = _v_33;
  return 1;
}

INLINE Term spin_116(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_48 = 0;
  Term _v_49 = 0;
  Term _s_1 = r0;
  Term _e_3 = r1;
  Term _r_2 = r2;
  Term _r_3 = r3;
  WL_SPIN
    Term _v_50 = 0;
    Term _v_51 = 0;
    Term _o_10[1];
    if (spin_100(e, _o_10, _s_1, _e_3, _r_3) == 0) {
      return 0;
    }
    _v_51 = _o_10[0];
    _v_50 = _v_51;
    _v_48 = _r_2;
    _v_49 = _v_50;
  break;
  }
  o[0] = _v_48;
  o[1] = _v_49;
  return 1;
}

INLINE Term spin_118(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  u32 _short_0 = r0;
  Term _o_3 = r1;
  Term _s_1 = r2;
  Term _n_2 = r3;
  Term _a_1 = r4;
  WL_SPIN
    if (_short_0 == 1) {
      Term _v_12 = 0;
      Term _v_13 = 0;
      Term _o_4[1];
      if (spin_83(e, _o_4, nat_chk(e, _s_1 + 1), _n_2) == 0) {
        return 0;
      }
      _v_13 = _o_4[0];
      _v_12 = _v_13;
      _v_10 = _a_1;
      _v_11 = _v_12;
    } else {
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _v_16 = 0;
      u32 _v_17 = 0;
      u32 _v_18 = 0;
      u32 _v_19 = 0;
      u32 _v_20 = 0;
      Term _a_2 = 1200ull;
      Term _a_3 = 1ull;
      Term _v_21 = 0;
      u32 _v_22 = 0;
      u32 _v_23 = 0;
      u32 _v_24 = 0;
      u32 _v_25 = 0;
      Term _o_5[5];
      if (spin_2(e, _o_5, _a_1, nat_chk(e, _o_3 + nat_chk(e, _s_1 + (_a_2 < _a_3 ? 0 : _a_2 - _a_3)))) == 0) {
        return 0;
      }
      _v_21 = _o_5[0];
      _v_22 = _o_5[1];
      _v_23 = _o_5[2];
      _v_24 = _o_5[3];
      _v_25 = _o_5[4];
      _v_16 = _v_21;
      _v_17 = _v_22;
      _v_18 = _v_23;
      _v_19 = _v_24;
      _v_20 = _v_25;
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _o_6[2];
      if (spin_112(e, _o_6, _o_3, _s_1, _v_16, _v_17, _v_18, _v_19, _v_20) == 0) {
        return 0;
      }
      _v_26 = _o_6[0];
      _v_27 = _o_6[1];
      _v_14 = _v_26;
      _v_15 = _v_27;
      Term _v_28 = 0;
      Term _v_29 = 0;
      Term _o_7[2];
      if (spin_113(e, _o_7, _s_1, _v_14, _v_15) == 0) {
        return 0;
      }
      _v_28 = _o_7[0];
      _v_29 = _o_7[1];
      _v_10 = _v_28;
      _v_11 = _v_29;
    }
  break;
  }
  o[0] = _v_10;
  o[1] = _v_11;
  return 1;
}

INLINE Term spin_117(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _a_0 = r0;
  Term _o_1 = r1;
  Term _s_0 = r2;
  Term _n_1 = r3;
  WL_SPIN
    u32 _v_6 = 0;
    u32 _v_7 = 0;
    Term _o_2[1];
    if (spin_39(e, _o_2, (_n_1 < _s_0 ? 0 : _n_1 - _s_0), 1200ull) == 0) {
      return 0;
    }
    _v_7 = _o_2[0];
    _v_6 = _v_7;
    Term _v_8 = 0;
    Term _v_9 = 0;
    Term _o_8[2];
    if (spin_118(e, _o_8, _v_6, _o_1, _s_0, _n_1, _a_0) == 0) {
      return 0;
    }
    _v_8 = _o_8[0];
    _v_9 = _o_8[1];
    _v_4 = _v_8;
    _v_5 = _v_9;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_119(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_34 = 0;
  Term _v_35 = 0;
  Term _v_36 = 0;
  u32 _v_37 = 0;
  Term _s_2 = r0;
  Term _n_3 = r1;
  Term _r_2 = r2;
  Term _r_3 = r3;
  WL_SPIN
    _v_34 = _r_2;
    _v_35 = _s_2;
    _v_36 = _r_3;
    _v_37 = (_r_3 < _n_3);
  break;
  }
  o[0] = _v_34;
  o[1] = _v_35;
  o[2] = _v_36;
  o[3] = _v_37;
  return 1;
}

INLINE Term spin_120(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _v_6 = 0;
  u32 _v_7 = 0;
  u32 _ok_0 = r0;
  Term _len_1 = r1;
  Term _p_1 = r2;
  Term _a_0 = r3;
  WL_SPIN
    if (_ok_0 == 1) {
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _v_10 = 0;
      u32 _v_11 = 0;
      Term _v_12 = 0;
      Term _v_13 = 0;
      Term _v_14 = 0;
      u32 _v_15 = 0;
      Term _o_0[4];
      if (spin_56(e, _o_0, _a_0, _len_1, nat_chk(e, _p_1 + 1)) == 0) {
        return 0;
      }
      _v_12 = _o_0[0];
      _v_13 = _o_0[1];
      _v_14 = _o_0[2];
      _v_15 = _o_0[3];
      _v_8 = _v_12;
      _v_9 = _v_13;
      _v_10 = _v_14;
      _v_11 = _v_15;
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _v_18 = 0;
      u32 _v_19 = 0;
      Term _o_1[4];
      if (spin_68(e, _o_1, _len_1, 10ull, _v_8, _v_9, _v_10, _v_11) == 0) {
        return 0;
      }
      _v_16 = _o_1[0];
      _v_17 = _o_1[1];
      _v_18 = _o_1[2];
      _v_19 = _o_1[3];
      _v_4 = _v_16;
      _v_5 = _v_17;
      _v_6 = _v_18;
      _v_7 = _v_19;
    } else {
      _v_4 = _a_0;
      _v_5 = 0;
      _v_6 = _p_1;
      _v_7 = 0;
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_121(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7, Term r8, Term r9) {
  u32 wpoll = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  Term _v_11 = 0;
  Term _v_12 = 0;
  u32 _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _cnt_1 = r0;
  Term _len_1 = r1;
  Term _n_1 = r2;
  Term _x_0 = r3;
  Term _x_1 = r4;
  Term _x_2 = r5;
  Term _x_3 = r6;
  u32 _x_4 = r7;
  Term _x_5 = r8;
  Term _x_6 = r9;
  WL_SPIN
    if (_cnt_1 == 0) {
      _v_9 = _x_0;
      _v_10 = _x_1;
      _v_11 = _x_2;
      _v_12 = _x_3;
      _v_13 = _x_4;
      _v_14 = _x_5;
      _v_15 = _x_6;
    } else {
      Term _k_0 = (_cnt_1 - 1);
      if (_x_4 == 0) {
        _v_9 = _x_0;
        _v_10 = _x_1;
        _v_11 = _x_2;
        _v_12 = _x_3;
        _v_13 = 0;
        _v_14 = _x_5;
        _v_15 = _x_6;
      } else {
        Term _v_16 = 0;
        Term _v_17 = 0;
        Term _v_18 = 0;
        Term _v_19 = 0;
        u32 _v_20 = 0;
        Term _v_21 = 0;
        Term _v_22 = 0;
        Term _v_23 = 0;
        Term _v_24 = 0;
        Term _v_25 = 0;
        Term _v_26 = 0;
        Term _v_27 = 0;
        u32 _v_28 = 0;
        Term _v_29 = 0;
        Term _v_30 = 0;
        Term _v_31 = 0;
        Term _v_32 = 0;
        Term _v_33 = 0;
        u32 _v_34 = 0;
        Term _o_1[6];
        if (spin_101(e, _o_1, _x_0, _len_1, _x_1) == 0) {
          return 0;
        }
        _v_29 = _o_1[0];
        _v_30 = _o_1[1];
        _v_31 = _o_1[2];
        _v_32 = _o_1[3];
        _v_33 = _o_1[4];
        _v_34 = _o_1[5];
        _v_23 = _v_29;
        _v_24 = _v_30;
        _v_25 = _v_31;
        _v_26 = _v_32;
        _v_27 = _v_33;
        _v_28 = _v_34;
        Term _v_35 = 0;
        Term _v_36 = 0;
        Term _v_37 = 0;
        Term _v_38 = 0;
        u32 _v_39 = 0;
        Term _v_40 = 0;
        Term _v_41 = 0;
        Term _o_2[7];
        if (spin_102(e, _o_2, _n_1, _x_3, _x_5, _x_6, _x_2, _v_23, _v_24, _v_25, _v_26, _v_27, _v_28) == 0) {
          return 0;
        }
        _v_35 = _o_2[0];
        _v_36 = _o_2[1];
        _v_37 = _o_2[2];
        _v_38 = _o_2[3];
        _v_39 = _o_2[4];
        _v_40 = _o_2[5];
        _v_41 = _o_2[6];
        _v_16 = _v_35;
        _v_17 = _v_36;
        _v_18 = _v_37;
        _v_19 = _v_38;
        _v_20 = _v_39;
        _v_21 = _v_40;
        _v_22 = _v_41;
        r0 = _k_0;
        r1 = _len_1;
        r2 = _n_1;
        r3 = _v_16;
        r4 = _v_17;
        r5 = _v_18;
        r6 = _v_19;
        r7 = _v_20;
        r8 = _v_21;
        r9 = _v_22;
        _cnt_1 = r0;
        _len_1 = r1;
        _n_1 = r2;
        _x_0 = r3;
        _x_1 = r4;
        _x_2 = r5;
        _x_3 = r6;
        _x_4 = r7;
        _x_5 = r8;
        _x_6 = r9;
        WL_AGAIN(spin_121);
      }
    }
  break;
  }
  o[0] = _v_9;
  o[1] = _v_10;
  o[2] = _v_11;
  o[3] = _v_12;
  o[4] = _v_13;
  o[5] = _v_14;
  o[6] = _v_15;
  return 1;
}

INLINE Term spin_122(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _k_2 = r0;
  Term _n_2 = r1;
  Term _x_0 = r2;
  Term _x_1 = r3;
  Term _x_2 = r4;
  WL_SPIN
    Term _v_6 = 0;
    Term _v_7 = 0;
    Term _v_8 = 0;
    Term _v_9 = 0;
    Term _v_10 = 0;
    Term _v_11 = 0;
    Term _v_12 = 0;
    Term _v_13 = 0;
    Term _at_0 = blk_loc(e.mem, _x_1);
    Term _at_1 = blk_at(_x_1, 0ull, 0);
    Term _c_0 = blk_read(e.mem, 1, _at_0, _at_1 + 0);
    Term _v_14 = 0;
    Term _v_15 = 0;
    Term _v_16 = 0;
    Term _v_17 = 0;
    Term _o_1[4];
    if (spin_86(e, _o_1, 0, term_pak(CID_NIL, 0), _x_1, _c_0) == 0) {
      return 0;
    }
    _v_14 = _o_1[0];
    _v_15 = _o_1[1];
    _v_16 = _o_1[2];
    _v_17 = _o_1[3];
    _v_10 = _v_14;
    _v_11 = _v_15;
    _v_12 = _v_16;
    _v_13 = _v_17;
    Term _v_18 = 0;
    Term _v_19 = 0;
    Term _v_20 = 0;
    Term _v_21 = 0;
    Term _o_2[4];
    if (spin_103(e, _o_2, _n_2, _k_2, 0, _v_10, _v_11, _v_12, _v_13) == 0) {
      return 0;
    }
    _v_18 = _o_2[0];
    _v_19 = _o_2[1];
    _v_20 = _o_2[2];
    _v_21 = _o_2[3];
    _v_6 = _v_18;
    _v_7 = _v_19;
    _v_8 = _v_20;
    _v_9 = _v_21;
    Term _v_22 = 0;
    Term _v_23 = 0;
    Term _o_3[2];
    if (spin_104(e, _o_3, _x_0, _v_6, _v_7, _v_8, _v_9) == 0) {
      return 0;
    }
    _v_22 = _o_3[0];
    _v_23 = _o_3[1];
    _v_4 = _v_22;
    _v_5 = _v_23;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_123(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  Term _v_7 = 0;
  Term _len_2 = r0;
  WL_SPIN
    Term _v_8 = 0;
    Term _v_9 = 0;
    Term _v_10 = 0;
    Term _o_0[1];
    if (spin_4(e, _o_0, _len_2, 4ull) == 0) {
      return 0;
    }
    _v_10 = _o_0[0];
    _v_9 = _v_10;
    Term _v_11 = 0;
    Term _o_1[1];
    if (spin_80(e, _o_1, nat_chk(e, _v_9 + 1)) == 0) {
      return 0;
    }
    _v_11 = _o_1[0];
    _v_8 = _v_11;
    Term _fv_0[1];
    _fv_0[0] = 0ull;
    _v_6 = blk_new(e, 0, _v_8, 0, 1, _fv_0);
    _v_7 = 0;
  break;
  }
  o[0] = _v_6;
  o[1] = _v_7;
  return 1;
}

INLINE Term spin_124(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _hs_0 = r0;
  Term _r_2 = r1;
  Term _i_0 = r2;
  Term _w_0 = r3;
  Term _w_1 = r4;
  WL_SPIN
    if (term_aux(_hs_0) == CID_NIL) {
      _v_14 = _w_0;
      _v_15 = _w_1;
    } else {
      u64 _sp_0 = term_loc(_hs_0);
      Term _f_0 = e.mem[_sp_0 + 0];
      Term _f_1 = e.mem[_sp_0 + 1];
      u64 _sp_1 = term_loc(_f_0);
      Term _f_2 = e.mem[_sp_1 + 0];
      Term _f_3 = e.mem[_sp_1 + 1];
      heap_free(e, cls_fit(2), _sp_1);
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _v_18 = 0;
      Term _v_19 = 0;
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _v_28 = 0;
      Term _v_29 = 0;
      Term _o_3[2];
      if (spin_105(e, _o_3, 104ull, _w_0, _w_1) == 0) {
        return 0;
      }
      _v_28 = _o_3[0];
      _v_29 = _o_3[1];
      _v_26 = _v_28;
      _v_27 = _v_29;
      Term _v_30 = 0;
      Term _v_31 = 0;
      Term _o_4[2];
      if (spin_108(e, _o_4, _r_2, _v_26, _v_27) == 0) {
        return 0;
      }
      _v_30 = _o_4[0];
      _v_31 = _o_4[1];
      _v_24 = _v_30;
      _v_25 = _v_31;
      Term _v_32 = 0;
      Term _v_33 = 0;
      Term _o_5[2];
      if (spin_108(e, _o_5, _i_0, _v_24, _v_25) == 0) {
        return 0;
      }
      _v_32 = _o_5[0];
      _v_33 = _o_5[1];
      _v_22 = _v_32;
      _v_23 = _v_33;
      Term _v_34 = 0;
      Term _v_35 = 0;
      Term _o_6[2];
      if (spin_108(e, _o_6, _f_3, _v_22, _v_23) == 0) {
        return 0;
      }
      _v_34 = _o_6[0];
      _v_35 = _o_6[1];
      _v_20 = _v_34;
      _v_21 = _v_35;
      Term _v_36 = 0;
      Term _v_37 = 0;
      Term _o_7[2];
      if (spin_108(e, _o_7, _f_2, _v_20, _v_21) == 0) {
        return 0;
      }
      _v_36 = _o_7[0];
      _v_37 = _o_7[1];
      _v_18 = _v_36;
      _v_19 = _v_37;
      Term _v_38 = 0;
      Term _v_39 = 0;
      Term _o_8[2];
      if (spin_105(e, _o_8, 10ull, _v_18, _v_19) == 0) {
        return 0;
      }
      _v_38 = _o_8[0];
      _v_39 = _o_8[1];
      _v_16 = _v_38;
      _v_17 = _v_39;
      heap_free(e, cls_fit(2), _sp_0);
      r0 = _f_1;
      r1 = _r_2;
      r2 = nat_chk(e, _i_0 + 1);
      r3 = _v_16;
      r4 = _v_17;
      _hs_0 = r0;
      _r_2 = r1;
      _i_0 = r2;
      _w_0 = r3;
      _w_1 = r4;
      WL_AGAIN(spin_124);
    }
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  return 1;
}

INLINE Term spin_125(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, u32 r5) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  u32 _v_6 = 0;
  Term _v_7 = 0;
  Term _id_1 = r0;
  Term _qo_1 = r1;
  Term _ql_1 = r2;
  Term _a_1 = r3;
  Term _r_0 = r4;
  u32 _r_1 = r5;
  WL_SPIN
    Term _at_2 = blk_loc(e.mem, _r_0);
    Term _at_3 = blk_at(_r_0, ((u64)(u32)(nat_chk(e, nat_mul(e, 2ull, _id_1) + 1))), 0);
    u32 _c_1 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
    Term _v_8 = 0;
    Term _v_9 = 0;
    u32 _v_10 = 0;
    Term _v_11 = 0;
    Term _o_0[4];
    if (spin_111(e, _o_0, _qo_1, _ql_1, _a_1, _r_1, _r_0, _c_1) == 0) {
      return 0;
    }
    _v_8 = _o_0[0];
    _v_9 = _o_0[1];
    _v_10 = _o_0[2];
    _v_11 = _o_0[3];
    _v_4 = _v_8;
    _v_5 = _v_9;
    _v_6 = _v_10;
    _v_7 = _v_11;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_126(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _s_0 = r0;
  Term _w_2 = r1;
  Term _w_3 = r2;
  WL_SPIN
    if (term_aux(_s_0) == CID_SNIL) {
      _v_4 = _w_2;
      _v_5 = _w_3;
    } else {
      Term _fb_0[2];
      u64 _sp_0 = ctr_take(e, _s_0, 2, _fb_0);
      u32 _f_0 = _fb_0[0];
      Term _f_1 = _fb_0[1];
      Term _v_6 = 0;
      Term _v_7 = 0;
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _o_0[2];
      if (spin_105(e, _o_0, _f_0, _w_2, _w_3) == 0) {
        return 0;
      }
      _v_8 = _o_0[0];
      _v_9 = _o_0[1];
      _v_6 = _v_8;
      _v_7 = _v_9;
      spare_free(e, cls_fit(2), _sp_0);
      r0 = _f_1;
      r1 = _v_6;
      r2 = _v_7;
      _s_0 = r0;
      _w_2 = r1;
      _w_3 = r2;
      WL_AGAIN(spin_126);
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_127(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  Term _a_0 = r0;
  Term _o_1 = r1;
  Term _s_0 = r2;
  Term _e_0 = r3;
  WL_SPIN
    Term _v_10 = 0;
    Term _a_1 = 200ull;
    Term _v_11 = 0;
    Term _o_2[1];
    if (spin_83(e, _o_2, nat_chk(e, _s_0 + 1), (_e_0 < _a_1 ? 0 : _e_0 - _a_1)) == 0) {
      return 0;
    }
    _v_11 = _o_2[0];
    _v_10 = _v_11;
    Term _v_12 = 0;
    Term _v_13 = 0;
    Term _v_14 = 0;
    Term _v_15 = 0;
    Term _v_16 = 0;
    u32 _v_17 = 0;
    u32 _v_18 = 0;
    u32 _v_19 = 0;
    u32 _v_20 = 0;
    Term _v_21 = 0;
    u32 _v_22 = 0;
    u32 _v_23 = 0;
    u32 _v_24 = 0;
    u32 _v_25 = 0;
    Term _o_3[5];
    if (spin_2(e, _o_3, _a_0, nat_chk(e, _o_1 + _v_10)) == 0) {
      return 0;
    }
    _v_21 = _o_3[0];
    _v_22 = _o_3[1];
    _v_23 = _o_3[2];
    _v_24 = _o_3[3];
    _v_25 = _o_3[4];
    _v_16 = _v_21;
    _v_17 = _v_22;
    _v_18 = _v_23;
    _v_19 = _v_24;
    _v_20 = _v_25;
    Term _v_26 = 0;
    Term _v_27 = 0;
    Term _o_4[2];
    if (spin_114(e, _o_4, 200ull, (_v_10 < _e_0), _o_1, _v_10, _e_0, _v_16, _v_17, _v_18, _v_19, _v_20) == 0) {
      return 0;
    }
    _v_26 = _o_4[0];
    _v_27 = _o_4[1];
    _v_14 = _v_26;
    _v_15 = _v_27;
    Term _v_28 = 0;
    Term _v_29 = 0;
    Term _o_5[2];
    if (spin_115(e, _o_5, _o_1, _e_0, _v_14, _v_15) == 0) {
      return 0;
    }
    _v_28 = _o_5[0];
    _v_29 = _o_5[1];
    _v_12 = _v_28;
    _v_13 = _v_29;
    Term _v_30 = 0;
    Term _v_31 = 0;
    Term _o_6[2];
    if (spin_116(e, _o_6, _s_0, _e_0, _v_12, _v_13) == 0) {
      return 0;
    }
    _v_30 = _o_6[0];
    _v_31 = _o_6[1];
    _v_8 = _v_30;
    _v_9 = _v_31;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  return 1;
}

INLINE Term spin_128(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_36 = 0;
  Term _v_37 = 0;
  Term _v_38 = 0;
  u32 _v_39 = 0;
  Term _o_8 = r0;
  Term _n_1 = r1;
  Term _r_0 = r2;
  Term _r_1 = r3;
  WL_SPIN
    Term _v_40 = 0;
    Term _v_41 = 0;
    Term _v_42 = 0;
    Term _v_43 = 0;
    Term _o_9[2];
    if (spin_117(e, _o_9, _r_0, _o_8, _r_1, _n_1) == 0) {
      return 0;
    }
    _v_42 = _o_9[0];
    _v_43 = _o_9[1];
    _v_40 = _v_42;
    _v_41 = _v_43;
    Term _v_44 = 0;
    Term _v_45 = 0;
    Term _v_46 = 0;
    u32 _v_47 = 0;
    Term _o_10[4];
    if (spin_119(e, _o_10, _r_1, _n_1, _v_40, _v_41) == 0) {
      return 0;
    }
    _v_44 = _o_10[0];
    _v_45 = _o_10[1];
    _v_46 = _o_10[2];
    _v_47 = _o_10[3];
    _v_36 = _v_44;
    _v_37 = _v_45;
    _v_38 = _v_46;
    _v_39 = _v_47;
  break;
  }
  o[0] = _v_36;
  o[1] = _v_37;
  o[2] = _v_38;
  o[3] = _v_39;
  return 1;
}

INLINE Term spin_129(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_50 = 0;
  Term _v_51 = 0;
  Term _c_0 = r0;
  Term _c_1 = r1;
  Term _r_2 = r2;
  Term _r_3 = r3;
  WL_SPIN
    u64 _nd_4 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_4 + 0] = _c_0;
    e.mem[_nd_4 + 1] = _c_1;
    u64 _nd_5 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_5 + 0] = term_ctr(CID_CORE_CHUNK, _nd_4);
    e.mem[_nd_5 + 1] = _r_3;
    _v_50 = _r_2;
    _v_51 = term_ctr(CID_CON, _nd_5);
  break;
  }
  o[0] = _v_50;
  o[1] = _v_51;
  return 1;
}

INLINE Term spin_130(Env e, THR Term* o, Term r0, Term r1, Term r2, u32 r3) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  u32 _v_11 = 0;
  Term _len_1 = r0;
  Term _p_1 = r1;
  Term _r_0 = r2;
  u32 _r_1 = r3;
  WL_SPIN
    Term _v_12 = 0;
    Term _v_13 = 0;
    Term _v_14 = 0;
    u32 _v_15 = 0;
    Term _o_1[4];
    if (spin_120(e, _o_1, _r_1, _len_1, _p_1, _r_0) == 0) {
      return 0;
    }
    _v_12 = _o_1[0];
    _v_13 = _o_1[1];
    _v_14 = _o_1[2];
    _v_15 = _o_1[3];
    _v_8 = _v_12;
    _v_9 = _v_13;
    _v_10 = _v_14;
    _v_11 = _v_15;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  return 1;
}

INLINE Term spin_131(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9) {
  u32 wpoll = 0;
  Term _v_7 = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  Term _v_10 = 0;
  u32 _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  u32 _ok_0 = r0;
  Term _len_1 = r1;
  Term _n_1 = r2;
  Term _q_1 = r3;
  Term _mtf_1 = r4;
  Term _mdl_1 = r5;
  Term _pp_1 = r6;
  Term _cnt_0 = r7;
  Term _p_0 = r8;
  Term _a_0 = r9;
  WL_SPIN
    if (_ok_0 == 0) {
      _v_7 = _a_0;
      _v_8 = _p_0;
      _v_9 = _pp_1;
      _v_10 = _q_1;
      _v_11 = 0;
      _v_12 = _mtf_1;
      _v_13 = _mdl_1;
    } else {
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _o_0[1];
      if (spin_77(e, _o_0, nat_chk(e, _q_1 + 1), _pp_1) == 0) {
        return 0;
      }
      _v_15 = _o_0[0];
      _v_14 = _v_15;
      Term _at_0 = blk_loc(e.mem, _v_14);
      Term _at_1 = blk_at(_v_14, ((u64)(u32)(_q_1)), 0);
      u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      blk_write(e.mem, 0, _at_0, _at_1 + 0, ((u64)(u32)(_cnt_0)));
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _v_18 = 0;
      Term _v_19 = 0;
      u32 _v_20 = 0;
      Term _v_21 = 0;
      Term _v_22 = 0;
      Term _o_1[7];
      if (spin_121(e, _o_1, _cnt_0, _len_1, _n_1, _a_0, _p_0, _v_14, nat_chk(e, _q_1 + 1), 1, _mtf_1, _mdl_1) == 0) {
        return 0;
      }
      _v_16 = _o_1[0];
      _v_17 = _o_1[1];
      _v_18 = _o_1[2];
      _v_19 = _o_1[3];
      _v_20 = _o_1[4];
      _v_21 = _o_1[5];
      _v_22 = _o_1[6];
      _v_7 = _v_16;
      _v_8 = _v_17;
      _v_9 = _v_18;
      _v_10 = _v_19;
      _v_11 = _v_20;
      _v_12 = _v_21;
      _v_13 = _v_22;
    }
  break;
  }
  o[0] = _v_7;
  o[1] = _v_8;
  o[2] = _v_9;
  o[3] = _v_10;
  o[4] = _v_11;
  o[5] = _v_12;
  o[6] = _v_13;
  return 1;
}

INLINE Term spin_132(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  u32 _v_6 = 0;
  Term _v_7 = 0;
  u32 _ok_0 = r0;
  Term _id_1 = r1;
  Term _qo_1 = r2;
  Term _ql_1 = r3;
  Term _a_1 = r4;
  Term _ev_1 = r5;
  WL_SPIN
    if (_ok_0 == 0) {
      _v_4 = _a_1;
      _v_5 = _ev_1;
      _v_6 = 0;
      _v_7 = 0;
    } else {
      Term _at_0 = blk_loc(e.mem, _ev_1);
      Term _at_1 = blk_at(_ev_1, ((u64)(u32)(nat_mul(e, 2ull, _id_1))), 0);
      u32 _c_1 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      Term _v_8 = 0;
      Term _v_9 = 0;
      u32 _v_10 = 0;
      Term _v_11 = 0;
      Term _o_0[4];
      if (spin_125(e, _o_0, _id_1, _qo_1, _ql_1, _a_1, _ev_1, _c_1) == 0) {
        return 0;
      }
      _v_8 = _o_0[0];
      _v_9 = _o_0[1];
      _v_10 = _o_0[2];
      _v_11 = _o_0[3];
      _v_4 = _v_8;
      _v_5 = _v_9;
      _v_6 = _v_10;
      _v_7 = _v_11;
    }
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  o[2] = _v_6;
  o[3] = _v_7;
  return 1;
}

INLINE Term spin_133(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  u32 _v_16 = r0;
  Term _v_17 = r1;
  Term _w_0 = r2;
  Term _w_1 = r3;
  WL_SPIN
    if (_v_16 == 0) {
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _o_4[2];
      if (spin_126(e, _o_4, term_ctr(CID_SCON, STAT_OFF + 12), _w_0, _w_1) == 0) {
        return 0;
      }
      _v_20 = _o_4[0];
      _v_21 = _o_4[1];
      _v_18 = _v_20;
      _v_19 = _v_21;
    } else if (_v_16 == 1) {
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _o_5[2];
      if (spin_126(e, _o_5, term_ctr(CID_SCON, STAT_OFF + 24), _w_0, _w_1) == 0) {
        return 0;
      }
      _v_22 = _o_5[0];
      _v_23 = _o_5[1];
      _v_18 = _v_22;
      _v_19 = _v_23;
    } else if (_v_16 == 2) {
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _o_6[2];
      if (spin_126(e, _o_6, term_ctr(CID_SCON, STAT_OFF + 40), _w_0, _w_1) == 0) {
        return 0;
      }
      _v_24 = _o_6[0];
      _v_25 = _o_6[1];
      _v_18 = _v_24;
      _v_19 = _v_25;
    } else {
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _v_28 = 0;
      Term _v_29 = 0;
      Term _v_30 = 0;
      Term _v_31 = 0;
      Term _o_7[2];
      if (spin_105(e, _o_7, 32ull, _w_0, _w_1) == 0) {
        return 0;
      }
      _v_30 = _o_7[0];
      _v_31 = _o_7[1];
      _v_28 = _v_30;
      _v_29 = _v_31;
      Term _v_32 = 0;
      Term _v_33 = 0;
      Term _v_34 = 0;
      Term _v_35 = 0;
      Term _o_8[2];
      if (spin_105(e, _o_8, 111ull, _v_28, _v_29) == 0) {
        return 0;
      }
      _v_34 = _o_8[0];
      _v_35 = _o_8[1];
      _v_32 = _v_34;
      _v_33 = _v_35;
      Term _v_36 = 0;
      Term _v_37 = 0;
      Term _v_38 = 0;
      Term _v_39 = 0;
      Term _o_9[2];
      if (spin_105(e, _o_9, 107ull, _v_32, _v_33) == 0) {
        return 0;
      }
      _v_38 = _o_9[0];
      _v_39 = _o_9[1];
      _v_36 = _v_38;
      _v_37 = _v_39;
      Term _v_40 = 0;
      Term _v_41 = 0;
      Term _o_10[2];
      if (spin_108(e, _o_10, _v_17, _v_36, _v_37) == 0) {
        return 0;
      }
      _v_40 = _o_10[0];
      _v_41 = _o_10[1];
      _v_26 = _v_40;
      _v_27 = _v_41;
      Term _v_42 = 0;
      Term _v_43 = 0;
      Term _o_11[2];
      if (spin_105(e, _o_11, 10ull, _v_26, _v_27) == 0) {
        return 0;
      }
      _v_42 = _o_11[0];
      _v_43 = _o_11[1];
      _v_18 = _v_42;
      _v_19 = _v_43;
    }
  break;
  }
  o[0] = _v_18;
  o[1] = _v_19;
  return 1;
}

INLINE Term spin_134(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  Term _cs_0 = r0;
  Term _d_2 = r1;
  Term _w_0 = r2;
  Term _w_1 = r3;
  WL_SPIN
    if (term_aux(_cs_0) == CID_NIL) {
      _v_8 = _w_0;
      _v_9 = _w_1;
    } else {
      u64 _sp_0 = term_loc(_cs_0);
      Term _f_0 = e.mem[_sp_0 + 0];
      Term _f_1 = e.mem[_sp_0 + 1];
      u64 _sp_1 = term_loc(_f_0);
      Term _f_2 = e.mem[_sp_1 + 0];
      Term _f_3 = e.mem[_sp_1 + 1];
      heap_free(e, cls_fit(2), _sp_1);
      Term _v_10 = 0;
      Term _v_11 = 0;
      Term _v_12 = 0;
      Term _v_13 = 0;
      Term _v_14 = 0;
      Term _v_15 = 0;
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _v_18 = 0;
      Term _v_19 = 0;
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _o_1[2];
      if (spin_105(e, _o_1, 99ull, _w_0, _w_1) == 0) {
        return 0;
      }
      _v_20 = _o_1[0];
      _v_21 = _o_1[1];
      _v_18 = _v_20;
      _v_19 = _v_21;
      Term _v_22 = 0;
      Term _v_23 = 0;
      Term _o_2[2];
      if (spin_108(e, _o_2, _d_2, _v_18, _v_19) == 0) {
        return 0;
      }
      _v_22 = _o_2[0];
      _v_23 = _o_2[1];
      _v_16 = _v_22;
      _v_17 = _v_23;
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _o_3[2];
      if (spin_108(e, _o_3, _f_2, _v_16, _v_17) == 0) {
        return 0;
      }
      _v_24 = _o_3[0];
      _v_25 = _o_3[1];
      _v_14 = _v_24;
      _v_15 = _v_25;
      Term _v_26 = 0;
      Term _v_27 = 0;
      Term _o_4[2];
      if (spin_108(e, _o_4, _f_3, _v_14, _v_15) == 0) {
        return 0;
      }
      _v_26 = _o_4[0];
      _v_27 = _o_4[1];
      _v_12 = _v_26;
      _v_13 = _v_27;
      Term _v_28 = 0;
      Term _v_29 = 0;
      Term _o_5[2];
      if (spin_105(e, _o_5, 10ull, _v_12, _v_13) == 0) {
        return 0;
      }
      _v_28 = _o_5[0];
      _v_29 = _o_5[1];
      _v_10 = _v_28;
      _v_11 = _v_29;
      heap_free(e, cls_fit(2), _sp_0);
      r0 = _f_1;
      r1 = _d_2;
      r2 = _v_10;
      r3 = _v_11;
      _cs_0 = r0;
      _d_2 = r1;
      _w_0 = r2;
      _w_1 = r3;
      WL_AGAIN(spin_134);
    }
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  return 1;
}

INLINE Term spin_135(Env e, THR Term* o, Term r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  u32 _v_18 = 0;
  Term _a_0 = r0;
  Term _len_1 = r1;
  Term _p_0 = r2;
  WL_SPIN
    Term _v_19 = 0;
    u32 _v_20 = 0;
    Term _v_21 = 0;
    u32 _v_22 = 0;
    Term _o_0[2];
    if (spin_57(e, _o_0, _a_0, _len_1, _p_0, 84ull) == 0) {
      return 0;
    }
    _v_21 = _o_0[0];
    _v_22 = _o_0[1];
    _v_19 = _v_21;
    _v_20 = _v_22;
    Term _v_23 = 0;
    Term _v_24 = 0;
    Term _v_25 = 0;
    u32 _v_26 = 0;
    Term _o_1[4];
    if (spin_130(e, _o_1, _len_1, _p_0, _v_19, _v_20) == 0) {
      return 0;
    }
    _v_23 = _o_1[0];
    _v_24 = _o_1[1];
    _v_25 = _o_1[2];
    _v_26 = _o_1[3];
    _v_15 = _v_23;
    _v_16 = _v_24;
    _v_17 = _v_25;
    _v_18 = _v_26;
  break;
  }
  o[0] = _v_15;
  o[1] = _v_16;
  o[2] = _v_17;
  o[3] = _v_18;
  return 1;
}

INLINE Term spin_136(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, u32 r9) {
  u32 wpoll = 0;
  Term _v_34 = 0;
  Term _v_35 = 0;
  Term _v_36 = 0;
  Term _v_37 = 0;
  u32 _v_38 = 0;
  Term _v_39 = 0;
  Term _v_40 = 0;
  Term _len_2 = r0;
  Term _n_1 = r1;
  Term _q_0 = r2;
  Term _mtf_0 = r3;
  Term _mdl_0 = r4;
  Term _pp_0 = r5;
  Term _x_7 = r6;
  Term _x_8 = r7;
  Term _x_9 = r8;
  u32 _x_10 = r9;
  WL_SPIN
    Term _v_41 = 0;
    Term _v_42 = 0;
    Term _v_43 = 0;
    Term _v_44 = 0;
    u32 _v_45 = 0;
    Term _v_46 = 0;
    Term _v_47 = 0;
    Term _o_3[7];
    if (spin_131(e, _o_3, _x_10, _len_2, _n_1, _q_0, _mtf_0, _mdl_0, _pp_0, _x_8, _x_9, _x_7) == 0) {
      return 0;
    }
    _v_41 = _o_3[0];
    _v_42 = _o_3[1];
    _v_43 = _o_3[2];
    _v_44 = _o_3[3];
    _v_45 = _o_3[4];
    _v_46 = _o_3[5];
    _v_47 = _o_3[6];
    _v_34 = _v_41;
    _v_35 = _v_42;
    _v_36 = _v_43;
    _v_37 = _v_44;
    _v_38 = _v_45;
    _v_39 = _v_46;
    _v_40 = _v_47;
  break;
  }
  o[0] = _v_34;
  o[1] = _v_35;
  o[2] = _v_36;
  o[3] = _v_37;
  o[4] = _v_38;
  o[5] = _v_39;
  o[6] = _v_40;
  return 1;
}

INLINE Term spin_137(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  u32 _v_10 = 0;
  u32 _small_0 = r0;
  Term _n_1 = r1;
  Term _tot_1 = r2;
  Term _mdl_0 = r3;
  WL_SPIN
    if (_small_0 == 0) {
      _v_10 = 0;
    } else {
      Term _v_11 = 0;
      Term _v_12 = 0;
      Term _o_4[1];
      if (spin_4(e, _o_4, nat_mul(e, nat_mul(e, _mdl_0, _n_1), 1024ull), _tot_1) == 0) {
        return 0;
      }
      _v_12 = _o_4[0];
      _v_11 = _v_12;
      _v_10 = (_v_11 < nat_mul(e, 2097152ull, 4194304ull));
    }
  break;
  }
  o[0] = _v_10;
  return 1;
}

INLINE Term spin_138(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5) {
  u32 wpoll = 0;
  Term _v_8 = 0;
  Term _v_9 = 0;
  u32 _v_10 = 0;
  Term _v_11 = 0;
  Term _a_1 = r0;
  Term _ev_1 = r1;
  Term _c_1 = r2;
  Term _id_1 = r3;
  Term _qo_0 = r4;
  Term _ql_0 = r5;
  WL_SPIN
    Term _v_12 = 0;
    Term _v_13 = 0;
    u32 _v_14 = 0;
    Term _v_15 = 0;
    Term _o_0[4];
    if (spin_132(e, _o_0, (_id_1 < _c_1), _id_1, _qo_0, _ql_0, _a_1, _ev_1) == 0) {
      return 0;
    }
    _v_12 = _o_0[0];
    _v_13 = _o_0[1];
    _v_14 = _o_0[2];
    _v_15 = _o_0[3];
    _v_8 = _v_12;
    _v_9 = _v_13;
    _v_10 = _v_14;
    _v_11 = _v_15;
  break;
  }
  o[0] = _v_8;
  o[1] = _v_9;
  o[2] = _v_10;
  o[3] = _v_11;
  return 1;
}

INLINE Term spin_139(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, u32 r8, Term r9) {
  u32 wpoll = 0;
  u32 _v_27 = 0;
  Term _v_28 = 0;
  Term _v_29 = 0;
  Term _v_30 = 0;
  Term _v_31 = 0;
  Term _v_32 = 0;
  Term _v_33 = 0;
  Term _v_34 = 0;
  Term _v_35 = 0;
  Term _v_36 = 0;
  Term _v_37 = 0;
  Term _len_1 = r0;
  Term _pos_0 = r1;
  Term _d_1 = r2;
  Term _q_1 = r3;
  Term _r_1 = r4;
  Term _c_2 = r5;
  Term _x_0 = r6;
  Term _x_1 = r7;
  u32 _x_2 = r8;
  Term _x_3 = r9;
  WL_SPIN
    Term _v_38 = 0;
    Term _v_39 = 0;
    Term _v_40 = 0;
    Term _v_41 = 0;
    Term _v_42 = 0;
    Term _v_43 = 0;
    Term _v_44 = 0;
    Term _v_45 = 0;
    Term _a_2 = 90ull;
    Term _a_3 = 4ull;
    Term _a_4 = (_a_3 == 0 ? 0 : _a_2 / _a_3);
    Term _a_5 = (_a_3 == 0 ? _a_2 : _a_2 % _a_3);
    Term _v_46 = 0;
    Term _o_2[1];
    if (spin_5(e, _o_2, _a_4, _a_5) == 0) {
      return 0;
    }
    _v_46 = _o_2[0];
    _v_45 = _v_46;
    Term _v_47 = 0;
    Term _o_3[1];
    if (spin_80(e, _o_3, nat_chk(e, _v_45 + 1)) == 0) {
      return 0;
    }
    _v_47 = _o_3[0];
    _v_44 = _v_47;
    Term _fv_0[1];
    _fv_0[0] = 0ull;
    Term _v_48 = 0;
    Term _v_49 = 0;
    Term _o_4[2];
    if (spin_105(e, _o_4, 118ull, blk_new(e, 0, _v_44, 0, 1, _fv_0), 0) == 0) {
      return 0;
    }
    _v_48 = _o_4[0];
    _v_49 = _o_4[1];
    _v_42 = _v_48;
    _v_43 = _v_49;
    Term _v_50 = 0;
    Term _v_51 = 0;
    Term _o_5[2];
    if (spin_108(e, _o_5, _q_1, _v_42, _v_43) == 0) {
      return 0;
    }
    _v_50 = _o_5[0];
    _v_51 = _o_5[1];
    _v_40 = _v_50;
    _v_41 = _v_51;
    Term _v_52 = 0;
    Term _v_53 = 0;
    Term _o_6[2];
    if (spin_133(e, _o_6, _x_2, _x_3, _v_40, _v_41) == 0) {
      return 0;
    }
    _v_52 = _o_6[0];
    _v_53 = _o_6[1];
    _v_38 = _v_52;
    _v_39 = _v_53;
    _v_27 = 3;
    _v_28 = _v_38;
    _v_29 = _v_39;
    _v_30 = _x_0;
    _v_31 = _len_1;
    _v_32 = _pos_0;
    _v_33 = _d_1;
    _v_34 = nat_chk(e, _q_1 + 1);
    _v_35 = _r_1;
    _v_36 = _x_1;
    _v_37 = _c_2;
  break;
  }
  o[0] = _v_27;
  o[1] = _v_28;
  o[2] = _v_29;
  o[3] = _v_30;
  o[4] = _v_31;
  o[5] = _v_32;
  o[6] = _v_33;
  o[7] = _v_34;
  o[8] = _v_35;
  o[9] = _v_36;
  o[10] = _v_37;
  return 1;
}

INLINE Term spin_140(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _c_1 = r0;
  Term _p_1 = r1;
  Term _l_1 = r2;
  Term _ev_1 = r3;
  WL_SPIN
    Term _v_3 = 0;
    Term _v_4 = 0;
    Term _o_0[1];
    if (spin_78(e, _o_0, nat_mul(e, 2ull, nat_chk(e, _c_1 + 1)), _ev_1, (1ull << (blk_cls(_ev_1) - 0))) == 0) {
      return 0;
    }
    _v_4 = _o_0[0];
    _v_3 = _v_4;
    Term _at_0 = blk_loc(e.mem, _v_3);
    Term _at_1 = blk_at(_v_3, ((u64)(u32)(nat_mul(e, 2ull, _c_1))), 0);
    u32 _c_2 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    blk_write(e.mem, 0, _at_0, _at_1 + 0, ((u64)(u32)(_p_1)));
    Term _at_2 = blk_loc(e.mem, _v_3);
    Term _at_3 = blk_at(_v_3, ((u64)(u32)(nat_chk(e, nat_mul(e, 2ull, _c_1) + 1))), 0);
    u32 _c_3 = blk_read(e.mem, 0, _at_2, _at_3 + 0);
    blk_write(e.mem, 0, _at_2, _at_3 + 0, ((u64)(u32)(_l_1)));
    _v_2 = _v_3;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_141(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, u32 r7, Term r8, Term r9) {
  u32 wpoll = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  u32 _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _t_0 = r0;
  Term _len_1 = r1;
  Term _n_0 = r2;
  Term _x_0 = r3;
  Term _x_1 = r4;
  Term _x_2 = r5;
  Term _x_3 = r6;
  u32 _x_4 = r7;
  Term _x_5 = r8;
  Term _x_6 = r9;
  WL_SPIN
    if (_t_0 == 0) {
      _v_14 = _x_0;
      _v_15 = _x_1;
      _v_16 = _x_2;
      _v_17 = _x_3;
      _v_18 = _x_4;
      _v_19 = _x_5;
      _v_20 = _x_6;
    } else {
      Term _u_0 = (_t_0 - 1);
      if (_x_4 == 0) {
        _v_14 = _x_0;
        _v_15 = _x_1;
        _v_16 = _x_2;
        _v_17 = _x_3;
        _v_18 = 0;
        _v_19 = _x_5;
        _v_20 = _x_6;
      } else {
        Term _v_21 = 0;
        Term _v_22 = 0;
        Term _v_23 = 0;
        Term _v_24 = 0;
        u32 _v_25 = 0;
        Term _v_26 = 0;
        Term _v_27 = 0;
        Term _v_28 = 0;
        Term _v_29 = 0;
        Term _v_30 = 0;
        u32 _v_31 = 0;
        Term _v_32 = 0;
        Term _v_33 = 0;
        Term _v_34 = 0;
        u32 _v_35 = 0;
        Term _o_0[4];
        if (spin_135(e, _o_0, _x_0, _len_1, _x_1) == 0) {
          return 0;
        }
        _v_32 = _o_0[0];
        _v_33 = _o_0[1];
        _v_34 = _o_0[2];
        _v_35 = _o_0[3];
        _v_28 = _v_32;
        _v_29 = _v_33;
        _v_30 = _v_34;
        _v_31 = _v_35;
        Term _v_36 = 0;
        Term _v_37 = 0;
        Term _v_38 = 0;
        Term _v_39 = 0;
        u32 _v_40 = 0;
        Term _v_41 = 0;
        Term _v_42 = 0;
        Term _o_1[7];
        if (spin_136(e, _o_1, _len_1, _n_0, _x_3, _x_5, _x_6, _x_2, _v_28, _v_29, _v_30, _v_31) == 0) {
          return 0;
        }
        _v_36 = _o_1[0];
        _v_37 = _o_1[1];
        _v_38 = _o_1[2];
        _v_39 = _o_1[3];
        _v_40 = _o_1[4];
        _v_41 = _o_1[5];
        _v_42 = _o_1[6];
        _v_21 = _v_36;
        _v_22 = _v_37;
        _v_23 = _v_38;
        _v_24 = _v_39;
        _v_25 = _v_40;
        _v_26 = _v_41;
        _v_27 = _v_42;
        r0 = _u_0;
        r1 = _len_1;
        r2 = _n_0;
        r3 = _v_21;
        r4 = _v_22;
        r5 = _v_23;
        r6 = _v_24;
        r7 = _v_25;
        r8 = _v_26;
        r9 = _v_27;
        _t_0 = r0;
        _len_1 = r1;
        _n_0 = r2;
        _x_0 = r3;
        _x_1 = r4;
        _x_2 = r5;
        _x_3 = r6;
        _x_4 = r7;
        _x_5 = r8;
        _x_6 = r9;
        WL_AGAIN(spin_141);
      }
    }
  break;
  }
  o[0] = _v_14;
  o[1] = _v_15;
  o[2] = _v_16;
  o[3] = _v_17;
  o[4] = _v_18;
  o[5] = _v_19;
  o[6] = _v_20;
  return 1;
}

INLINE Term spin_142(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9, Term r10) {
  u32 wpoll = 0;
  u32 _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  u32 _ok_1 = r0;
  Term _len_1 = r1;
  Term _p_1 = r2;
  Term _l_0 = r3;
  Term _id_0 = r4;
  Term _d_1 = r5;
  Term _q_1 = r6;
  Term _r_1 = r7;
  Term _a_1 = r8;
  Term _ev_1 = r9;
  Term _c_1 = r10;
  WL_SPIN
    if (_ok_1 == 0) {
      term_sink(e, _a_1);
      term_sink(e, _ev_1);
      _v_13 = 1;
      _v_14 = term_ctr(CID_SCON, STAT_OFF + 216);
      _v_15 = 0;
      _v_16 = 0;
      _v_17 = 0;
      _v_18 = 0;
      _v_19 = 0;
      _v_20 = 0;
      _v_21 = 0;
      _v_22 = 0;
      _v_23 = 0;
    } else {
      Term _v_24 = 0;
      Term _v_25 = 0;
      u32 _v_26 = 0;
      Term _v_27 = 0;
      Term _v_28 = 0;
      Term _v_29 = 0;
      u32 _v_30 = 0;
      Term _v_31 = 0;
      Term _o_1[4];
      if (spin_138(e, _o_1, _a_1, _ev_1, _c_1, _id_0, _p_1, _l_0) == 0) {
        return 0;
      }
      _v_28 = _o_1[0];
      _v_29 = _o_1[1];
      _v_30 = _o_1[2];
      _v_31 = _o_1[3];
      _v_24 = _v_28;
      _v_25 = _v_29;
      _v_26 = _v_30;
      _v_27 = _v_31;
      u32 _v_32 = 0;
      Term _v_33 = 0;
      Term _v_34 = 0;
      Term _v_35 = 0;
      Term _v_36 = 0;
      Term _v_37 = 0;
      Term _v_38 = 0;
      Term _v_39 = 0;
      Term _v_40 = 0;
      Term _v_41 = 0;
      Term _v_42 = 0;
      Term _o_2[11];
      if (spin_139(e, _o_2, _len_1, nat_chk(e, _p_1 + _l_0), _d_1, _q_1, _r_1, _c_1, _v_24, _v_25, _v_26, _v_27) == 0) {
        return 0;
      }
      _v_32 = _o_2[0];
      _v_33 = _o_2[1];
      _v_34 = _o_2[2];
      _v_35 = _o_2[3];
      _v_36 = _o_2[4];
      _v_37 = _o_2[5];
      _v_38 = _o_2[6];
      _v_39 = _o_2[7];
      _v_40 = _o_2[8];
      _v_41 = _o_2[9];
      _v_42 = _o_2[10];
      _v_13 = _v_32;
      _v_14 = _v_33;
      _v_15 = _v_34;
      _v_16 = _v_35;
      _v_17 = _v_36;
      _v_18 = _v_37;
      _v_19 = _v_38;
      _v_20 = _v_39;
      _v_21 = _v_40;
      _v_22 = _v_41;
      _v_23 = _v_42;
    }
  break;
  }
  o[0] = _v_13;
  o[1] = _v_14;
  o[2] = _v_15;
  o[3] = _v_16;
  o[4] = _v_17;
  o[5] = _v_18;
  o[6] = _v_19;
  o[7] = _v_20;
  o[8] = _v_21;
  o[9] = _v_22;
  o[10] = _v_23;
  return 1;
}

INLINE Term spin_143(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9) {
  u32 wpoll = 0;
  u32 _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  Term _v_22 = 0;
  Term _v_23 = 0;
  u32 _ok_1 = r0;
  Term _len_1 = r1;
  Term _p_1 = r2;
  Term _l_0 = r3;
  Term _d_1 = r4;
  Term _q_1 = r5;
  Term _r_1 = r6;
  Term _a_1 = r7;
  Term _ev_1 = r8;
  Term _c_1 = r9;
  WL_SPIN
    if (_ok_1 == 0) {
      term_sink(e, _a_1);
      term_sink(e, _ev_1);
      _v_13 = 1;
      _v_14 = term_ctr(CID_SCON, STAT_OFF + 260);
      _v_15 = 0;
      _v_16 = 0;
      _v_17 = 0;
      _v_18 = 0;
      _v_19 = 0;
      _v_20 = 0;
      _v_21 = 0;
      _v_22 = 0;
      _v_23 = 0;
    } else {
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _o_1[1];
      if (spin_140(e, _o_1, _c_1, _p_1, _l_0, _ev_1) == 0) {
        return 0;
      }
      _v_25 = _o_1[0];
      _v_24 = _v_25;
      _v_13 = 2;
      _v_14 = _a_1;
      _v_15 = _len_1;
      _v_16 = nat_chk(e, _p_1 + _l_0);
      _v_17 = _d_1;
      _v_18 = _q_1;
      _v_19 = _r_1;
      _v_20 = _v_24;
      _v_21 = nat_chk(e, _c_1 + 1);
      _v_22 = 0;
      _v_23 = 0;
    }
  break;
  }
  o[0] = _v_13;
  o[1] = _v_14;
  o[2] = _v_15;
  o[3] = _v_16;
  o[4] = _v_17;
  o[5] = _v_18;
  o[6] = _v_19;
  o[7] = _v_20;
  o[8] = _v_21;
  o[9] = _v_22;
  o[10] = _v_23;
  return 1;
}

INLINE Term spin_144(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, u32 r4) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  u32 _v_15 = 0;
  Term _ns_1 = r0;
  Term _x_4 = r1;
  Term _x_5 = r2;
  Term _x_6 = r3;
  u32 _x_7 = r4;
  WL_SPIN
    _v_12 = _x_4;
    _v_13 = _ns_1;
    _v_14 = _x_6;
    _v_15 = _x_7;
  break;
  }
  o[0] = _v_12;
  o[1] = _v_13;
  o[2] = _v_14;
  o[3] = _v_15;
  return 1;
}

INLINE Term spin_145(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9) {
  u32 wpoll = 0;
  u32 _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  u32 _ok_0 = r0;
  Term _ns_0 = r1;
  Term _p_0 = r2;
  Term _len_1 = r3;
  Term _d_1 = r4;
  Term _q_1 = r5;
  Term _r_1 = r6;
  Term _ev_1 = r7;
  Term _c_1 = r8;
  Term _a_0 = r9;
  WL_SPIN
    if (_ok_0 == 1) {
      if (term_aux(_ns_0) == CID_CON) {
        u64 _sp_0 = term_loc(_ns_0);
        Term _f_0 = e.mem[_sp_0 + 0];
        Term _f_1 = e.mem[_sp_0 + 1];
        if (term_aux(_f_1) == CID_CON) {
          u64 _sp_1 = term_loc(_f_1);
          Term _f_2 = e.mem[_sp_1 + 0];
          Term _f_3 = e.mem[_sp_1 + 1];
          if (term_aux(_f_3) == CID_NIL) {
            u32 _v_22 = 0;
            u32 _v_23 = 0;
            Term _o_0[1];
            if (spin_39(e, _o_0, nat_chk(e, _p_0 + _f_0), _len_1) == 0) {
              return 0;
            }
            _v_23 = _o_0[0];
            _v_22 = _v_23;
            u32 _v_24 = 0;
            Term _v_25 = 0;
            Term _v_26 = 0;
            Term _v_27 = 0;
            Term _v_28 = 0;
            Term _v_29 = 0;
            Term _v_30 = 0;
            Term _v_31 = 0;
            Term _v_32 = 0;
            Term _v_33 = 0;
            Term _v_34 = 0;
            Term _o_1[11];
            if (spin_142(e, _o_1, _v_22, _len_1, _p_0, _f_0, _f_2, _d_1, _q_1, _r_1, _a_0, _ev_1, _c_1) == 0) {
              return 0;
            }
            _v_24 = _o_1[0];
            _v_25 = _o_1[1];
            _v_26 = _o_1[2];
            _v_27 = _o_1[3];
            _v_28 = _o_1[4];
            _v_29 = _o_1[5];
            _v_30 = _o_1[6];
            _v_31 = _o_1[7];
            _v_32 = _o_1[8];
            _v_33 = _o_1[9];
            _v_34 = _o_1[10];
            _v_11 = _v_24;
            _v_12 = _v_25;
            _v_13 = _v_26;
            _v_14 = _v_27;
            _v_15 = _v_28;
            _v_16 = _v_29;
            _v_17 = _v_30;
            _v_18 = _v_31;
            _v_19 = _v_32;
            _v_20 = _v_33;
            _v_21 = _v_34;
            heap_free(e, cls_fit(2), _sp_1);
            heap_free(e, cls_fit(2), _sp_0);
          } else {
            term_sink(e, _f_3);
            term_sink(e, _ev_1);
            term_sink(e, _a_0);
            _v_11 = 1;
            _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
            _v_13 = 0;
            _v_14 = 0;
            _v_15 = 0;
            _v_16 = 0;
            _v_17 = 0;
            _v_18 = 0;
            _v_19 = 0;
            _v_20 = 0;
            _v_21 = 0;
            heap_free(e, cls_fit(2), _sp_1);
            heap_free(e, cls_fit(2), _sp_0);
          }
        } else {
          term_sink(e, _f_1);
          term_sink(e, _ev_1);
          term_sink(e, _a_0);
          _v_11 = 1;
          _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
          _v_13 = 0;
          _v_14 = 0;
          _v_15 = 0;
          _v_16 = 0;
          _v_17 = 0;
          _v_18 = 0;
          _v_19 = 0;
          _v_20 = 0;
          _v_21 = 0;
          heap_free(e, cls_fit(2), _sp_0);
        }
      } else {
        term_sink(e, _ns_0);
        term_sink(e, _ev_1);
        term_sink(e, _a_0);
        _v_11 = 1;
        _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
        _v_13 = 0;
        _v_14 = 0;
        _v_15 = 0;
        _v_16 = 0;
        _v_17 = 0;
        _v_18 = 0;
        _v_19 = 0;
        _v_20 = 0;
        _v_21 = 0;
      }
    } else {
      term_sink(e, _ns_0);
      term_sink(e, _ev_1);
      term_sink(e, _a_0);
      _v_11 = 1;
      _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
      _v_13 = 0;
      _v_14 = 0;
      _v_15 = 0;
      _v_16 = 0;
      _v_17 = 0;
      _v_18 = 0;
      _v_19 = 0;
      _v_20 = 0;
      _v_21 = 0;
    }
  break;
  }
  o[0] = _v_11;
  o[1] = _v_12;
  o[2] = _v_13;
  o[3] = _v_14;
  o[4] = _v_15;
  o[5] = _v_16;
  o[6] = _v_17;
  o[7] = _v_18;
  o[8] = _v_19;
  o[9] = _v_20;
  o[10] = _v_21;
  return 1;
}

INLINE Term spin_146(Env e, THR Term* o, u32 r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, Term r9) {
  u32 wpoll = 0;
  u32 _v_11 = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  Term _v_15 = 0;
  Term _v_16 = 0;
  Term _v_17 = 0;
  Term _v_18 = 0;
  Term _v_19 = 0;
  Term _v_20 = 0;
  Term _v_21 = 0;
  u32 _ok_0 = r0;
  Term _ns_0 = r1;
  Term _p_0 = r2;
  Term _len_1 = r3;
  Term _d_1 = r4;
  Term _q_1 = r5;
  Term _r_1 = r6;
  Term _ev_1 = r7;
  Term _c_1 = r8;
  Term _a_0 = r9;
  WL_SPIN
    if (_ok_0 == 1) {
      if (term_aux(_ns_0) == CID_CON) {
        u64 _sp_0 = term_loc(_ns_0);
        Term _f_0 = e.mem[_sp_0 + 0];
        Term _f_1 = e.mem[_sp_0 + 1];
        if (term_aux(_f_1) == CID_NIL) {
          u32 _v_22 = 0;
          u32 _v_23 = 0;
          Term _o_0[1];
          if (spin_39(e, _o_0, nat_chk(e, _p_0 + _f_0), _len_1) == 0) {
            return 0;
          }
          _v_23 = _o_0[0];
          _v_22 = _v_23;
          u32 _v_24 = 0;
          Term _v_25 = 0;
          Term _v_26 = 0;
          Term _v_27 = 0;
          Term _v_28 = 0;
          Term _v_29 = 0;
          Term _v_30 = 0;
          Term _v_31 = 0;
          Term _v_32 = 0;
          Term _v_33 = 0;
          Term _v_34 = 0;
          Term _o_1[11];
          if (spin_143(e, _o_1, _v_22, _len_1, _p_0, _f_0, _d_1, _q_1, _r_1, _a_0, _ev_1, _c_1) == 0) {
            return 0;
          }
          _v_24 = _o_1[0];
          _v_25 = _o_1[1];
          _v_26 = _o_1[2];
          _v_27 = _o_1[3];
          _v_28 = _o_1[4];
          _v_29 = _o_1[5];
          _v_30 = _o_1[6];
          _v_31 = _o_1[7];
          _v_32 = _o_1[8];
          _v_33 = _o_1[9];
          _v_34 = _o_1[10];
          _v_11 = _v_24;
          _v_12 = _v_25;
          _v_13 = _v_26;
          _v_14 = _v_27;
          _v_15 = _v_28;
          _v_16 = _v_29;
          _v_17 = _v_30;
          _v_18 = _v_31;
          _v_19 = _v_32;
          _v_20 = _v_33;
          _v_21 = _v_34;
          heap_free(e, cls_fit(2), _sp_0);
        } else {
          term_sink(e, _f_1);
          term_sink(e, _ev_1);
          term_sink(e, _a_0);
          _v_11 = 1;
          _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
          _v_13 = 0;
          _v_14 = 0;
          _v_15 = 0;
          _v_16 = 0;
          _v_17 = 0;
          _v_18 = 0;
          _v_19 = 0;
          _v_20 = 0;
          _v_21 = 0;
          heap_free(e, cls_fit(2), _sp_0);
        }
      } else {
        term_sink(e, _ns_0);
        term_sink(e, _ev_1);
        term_sink(e, _a_0);
        _v_11 = 1;
        _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
        _v_13 = 0;
        _v_14 = 0;
        _v_15 = 0;
        _v_16 = 0;
        _v_17 = 0;
        _v_18 = 0;
        _v_19 = 0;
        _v_20 = 0;
        _v_21 = 0;
      }
    } else {
      term_sink(e, _ns_0);
      term_sink(e, _ev_1);
      term_sink(e, _a_0);
      _v_11 = 1;
      _v_12 = term_ctr(CID_SCON, STAT_OFF + 314);
      _v_13 = 0;
      _v_14 = 0;
      _v_15 = 0;
      _v_16 = 0;
      _v_17 = 0;
      _v_18 = 0;
      _v_19 = 0;
      _v_20 = 0;
      _v_21 = 0;
    }
  break;
  }
  o[0] = _v_11;
  o[1] = _v_12;
  o[2] = _v_13;
  o[3] = _v_14;
  o[4] = _v_15;
  o[5] = _v_16;
  o[6] = _v_17;
  o[7] = _v_18;
  o[8] = _v_19;
  o[9] = _v_20;
  o[10] = _v_21;
  return 1;
}

INLINE Term spin_147(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, u32 r6) {
  u32 wpoll = 0;
  Term _v_12 = 0;
  Term _v_13 = 0;
  Term _v_14 = 0;
  u32 _v_15 = 0;
  Term _c_1 = r0;
  Term _len_1 = r1;
  Term _ns_0 = r2;
  Term _x_0 = r3;
  Term _x_1 = r4;
  Term _x_2 = r5;
  u32 _x_3 = r6;
  WL_SPIN
    if (_c_1 == 0) {
      u64 _nd_0 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_0 + 0] = _x_1;
      e.mem[_nd_0 + 1] = _ns_0;
      Term _v_16 = 0;
      Term _v_17 = 0;
      Term _v_18 = 0;
      u32 _v_19 = 0;
      Term _v_20 = 0;
      Term _v_21 = 0;
      Term _v_22 = 0;
      u32 _v_23 = 0;
      Term _o_1[4];
      if (spin_68(e, _o_1, _len_1, 10ull, _x_0, _x_1, _x_2, _x_3) == 0) {
        return 0;
      }
      _v_20 = _o_1[0];
      _v_21 = _o_1[1];
      _v_22 = _o_1[2];
      _v_23 = _o_1[3];
      _v_16 = _v_20;
      _v_17 = _v_21;
      _v_18 = _v_22;
      _v_19 = _v_23;
      Term _v_24 = 0;
      Term _v_25 = 0;
      Term _v_26 = 0;
      u32 _v_27 = 0;
      Term _o_2[4];
      if (spin_144(e, _o_2, term_ctr(CID_CON, _nd_0), _v_16, _v_17, _v_18, _v_19) == 0) {
        return 0;
      }
      _v_24 = _o_2[0];
      _v_25 = _o_2[1];
      _v_26 = _o_2[2];
      _v_27 = _o_2[3];
      _v_12 = _v_24;
      _v_13 = _v_25;
      _v_14 = _v_26;
      _v_15 = _v_27;
    } else {
      Term _c2_1 = (_c_1 - 1);
      if (_x_3 == 0) {
        _v_12 = _x_0;
        _v_13 = _ns_0;
        _v_14 = _x_2;
        _v_15 = 0;
      } else {
        u64 _nd_1 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_1 + 0] = _x_1;
        e.mem[_nd_1 + 1] = _ns_0;
        Term _v_28 = 0;
        Term _v_29 = 0;
        Term _v_30 = 0;
        u32 _v_31 = 0;
        Term _v_32 = 0;
        Term _v_33 = 0;
        Term _v_34 = 0;
        u32 _v_35 = 0;
        Term _o_3[4];
        if (spin_56(e, _o_3, _x_0, _len_1, _x_2) == 0) {
          return 0;
        }
        _v_32 = _o_3[0];
        _v_33 = _o_3[1];
        _v_34 = _o_3[2];
        _v_35 = _o_3[3];
        _v_28 = _v_32;
        _v_29 = _v_33;
        _v_30 = _v_34;
        _v_31 = _v_35;
        r0 = _c2_1;
        r1 = _len_1;
        r2 = term_ctr(CID_CON, _nd_1);
        r3 = _v_28;
        r4 = _v_29;
        r5 = _v_30;
        r6 = _v_31;
        _c_1 = r0;
        _len_1 = r1;
        _ns_0 = r2;
        _x_0 = r3;
        _x_1 = r4;
        _x_2 = r5;
        _x_3 = r6;
        WL_AGAIN(spin_147);
      }
    }
  break;
  }
  o[0] = _v_12;
  o[1] = _v_13;
  o[2] = _v_14;
  o[3] = _v_15;
  return 1;
}

INLINE Term spin_148(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3) {
  u32 wpoll = 0;
  u32 _v_1 = 0;
  u32 _d_0 = r0;
  u32 _e_0 = r1;
  u32 _q_0 = r2;
  u32 _r_0 = r3;
  WL_SPIN
    if (_d_0 == 1) {
      _v_1 = 0;
    } else {
      if (_e_0 == 1) {
        _v_1 = 1;
      } else {
        if (_q_0 == 1) {
          _v_1 = 2;
        } else {
          if (_r_0 == 1) {
            _v_1 = 3;
          } else {
            _v_1 = 4;
          }
        }
      }
    }
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_149(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, u32 r9) {
  u32 wpoll = 0;
  u32 _v_54 = 0;
  Term _v_55 = 0;
  Term _v_56 = 0;
  Term _v_57 = 0;
  Term _v_58 = 0;
  Term _v_59 = 0;
  Term _v_60 = 0;
  Term _v_61 = 0;
  Term _v_62 = 0;
  Term _v_63 = 0;
  Term _v_64 = 0;
  Term _len_3 = r0;
  Term _d_3 = r1;
  Term _q_3 = r2;
  Term _r_3 = r3;
  Term _ev_3 = r4;
  Term _c_3 = r5;
  Term _x_0 = r6;
  Term _x_1 = r7;
  Term _x_2 = r8;
  u32 _x_3 = r9;
  WL_SPIN
    u32 _v_65 = 0;
    Term _v_66 = 0;
    Term _v_67 = 0;
    Term _v_68 = 0;
    Term _v_69 = 0;
    Term _v_70 = 0;
    Term _v_71 = 0;
    Term _v_72 = 0;
    Term _v_73 = 0;
    Term _v_74 = 0;
    Term _v_75 = 0;
    Term _o_7[11];
    if (spin_146(e, _o_7, _x_3, _x_1, _x_2, _len_3, _d_3, _q_3, _r_3, _ev_3, _c_3, _x_0) == 0) {
      return 0;
    }
    _v_65 = _o_7[0];
    _v_66 = _o_7[1];
    _v_67 = _o_7[2];
    _v_68 = _o_7[3];
    _v_69 = _o_7[4];
    _v_70 = _o_7[5];
    _v_71 = _o_7[6];
    _v_72 = _o_7[7];
    _v_73 = _o_7[8];
    _v_74 = _o_7[9];
    _v_75 = _o_7[10];
    _v_54 = _v_65;
    _v_55 = _v_66;
    _v_56 = _v_67;
    _v_57 = _v_68;
    _v_58 = _v_69;
    _v_59 = _v_70;
    _v_60 = _v_71;
    _v_61 = _v_72;
    _v_62 = _v_73;
    _v_63 = _v_74;
    _v_64 = _v_75;
  break;
  }
  o[0] = _v_54;
  o[1] = _v_55;
  o[2] = _v_56;
  o[3] = _v_57;
  o[4] = _v_58;
  o[5] = _v_59;
  o[6] = _v_60;
  o[7] = _v_61;
  o[8] = _v_62;
  o[9] = _v_63;
  o[10] = _v_64;
  return 1;
}

INLINE Term spin_150(Env e, THR Term* o, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term r6, Term r7, Term r8, u32 r9) {
  u32 wpoll = 0;
  u32 _v_103 = 0;
  Term _v_104 = 0;
  Term _v_105 = 0;
  Term _v_106 = 0;
  Term _v_107 = 0;
  Term _v_108 = 0;
  Term _v_109 = 0;
  Term _v_110 = 0;
  Term _v_111 = 0;
  Term _v_112 = 0;
  Term _v_113 = 0;
  Term _len_4 = r0;
  Term _d_4 = r1;
  Term _q_4 = r2;
  Term _r_4 = r3;
  Term _ev_4 = r4;
  Term _c_4 = r5;
  Term _x_4 = r6;
  Term _x_5 = r7;
  Term _x_6 = r8;
  u32 _x_7 = r9;
  WL_SPIN
    u32 _v_114 = 0;
    Term _v_115 = 0;
    Term _v_116 = 0;
    Term _v_117 = 0;
    Term _v_118 = 0;
    Term _v_119 = 0;
    Term _v_120 = 0;
    Term _v_121 = 0;
    Term _v_122 = 0;
    Term _v_123 = 0;
    Term _v_124 = 0;
    Term _o_11[11];
    if (spin_145(e, _o_11, _x_7, _x_5, _x_6, _len_4, _d_4, _q_4, _r_4, _ev_4, _c_4, _x_4) == 0) {
      return 0;
    }
    _v_114 = _o_11[0];
    _v_115 = _o_11[1];
    _v_116 = _o_11[2];
    _v_117 = _o_11[3];
    _v_118 = _o_11[4];
    _v_119 = _o_11[5];
    _v_120 = _o_11[6];
    _v_121 = _o_11[7];
    _v_122 = _o_11[8];
    _v_123 = _o_11[9];
    _v_124 = _o_11[10];
    _v_103 = _v_114;
    _v_104 = _v_115;
    _v_105 = _v_116;
    _v_106 = _v_117;
    _v_107 = _v_118;
    _v_108 = _v_119;
    _v_109 = _v_120;
    _v_110 = _v_121;
    _v_111 = _v_122;
    _v_112 = _v_123;
    _v_113 = _v_124;
  break;
  }
  o[0] = _v_103;
  o[1] = _v_104;
  o[2] = _v_105;
  o[3] = _v_106;
  o[4] = _v_107;
  o[5] = _v_108;
  o[6] = _v_109;
  o[7] = _v_110;
  o[8] = _v_111;
  o[9] = _v_112;
  o[10] = _v_113;
  return 1;
}

INLINE Term spin_151(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_2 = 0;
  u32 _b_0 = r0;
  WL_SPIN
    u32 _v_3 = 0;
    Term _o_0[1];
    if (spin_148(e, _o_0, U32_BIN(_b_0, ==, 68ull), U32_BIN(_b_0, ==, 69ull), U32_BIN(_b_0, ==, 81ull), U32_BIN(_b_0, ==, 82ull)) == 0) {
      return 0;
    }
    _v_3 = _o_0[0];
    _v_2 = _v_3;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_152(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  u32 _code_0 = r0;
  Term _msg_0 = r1;
  Term _k_0 = r2;
  WL_SPIN
    term_sink(e, _k_0);
    u64 _nd_0 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_0 + 0] = _code_0;
    e.mem[_nd_0 + 1] = _msg_0;
    _v_1 = term_ctr(CID_HALT, _nd_0);
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_154(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_7 = 0;
  u32 _r_0 = r0;
  Term _r_1 = r1;
  Term _r_2 = r2;
  WL_SPIN
    if (_r_0 == 1) {
      u64 _nd_5 = heap_alloc(e, cls_fit(1));
      e.mem[_nd_5 + 0] = _r_1;
      _v_7 = term_clo(FID_IO_PASS_C240, _nd_5);
    } else {
      u64 _nd_6 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_6 + 0] = _r_1;
      e.mem[_nd_6 + 1] = _r_2;
      _v_7 = term_clo(FID_IO_PASS_C241, _nd_6);
    }
  break;
  }
  o[0] = _v_7;
  return 1;
}

INLINE Term spin_153(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_5 = 0;
  Term _act_0 = r0;
  WL_SPIN
    u64 _nd_4 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_4 + 0] = _act_0;
    _v_5 = term_clo(FID_IO_TRY_C238, _nd_4);
  break;
  }
  o[0] = _v_5;
  return 1;
}

INLINE Term spin_159(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_6 = 0;
  u32 _code_0 = r0;
  Term _msg_0 = r1;
  Term _k_0 = r2;
  WL_SPIN
    term_sink(e, _k_0);
    u64 _nd_4 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_4 + 0] = _code_0;
    e.mem[_nd_4 + 1] = _msg_0;
    _v_6 = term_ctr(CID_HALT, _nd_4);
  break;
  }
  o[0] = _v_6;
  return 1;
}

INLINE Term spin_158(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  u32 _r_0 = r0;
  Term _r_1 = r1;
  Term _r_2 = r2;
  WL_SPIN
    if (_r_0 == 1) {
      u64 _nd_2 = heap_alloc(e, cls_fit(1));
      e.mem[_nd_2 + 0] = _r_1;
      _v_4 = term_clo(FID_IO_PASS_C261, _nd_2);
    } else {
      u64 _nd_3 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_3 + 0] = _r_1;
      e.mem[_nd_3 + 1] = _r_2;
      _v_4 = term_clo(FID_IO_PASS_C262, _nd_3);
    }
  break;
  }
  o[0] = _v_4;
  return 1;
}

INLINE Term spin_157(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _act_0 = r0;
  WL_SPIN
    u64 _nd_0 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_0 + 0] = _act_0;
    _v_2 = term_clo(FID_IO_TRY_C259, _nd_0);
  break;
  }
  o[0] = _v_2;
  return 1;
}

// Work
// ====

// A host self-jump is a tail call: as a loop, clang hoisted constants into
// symreg's entry (3.05 s against 2.51 s).
#if !DEVICE
#undef  WL_SPIN
#undef  WL_SPUN
#undef  WL_AGAIN
#define WL_SPIN
#define WL_SPUN
#define WL_AGAIN(F) __attribute__((musttail)) return WL_##F(WL_ALL)

typedef Term (PRESERVE(preserve_none) *WlFn)(WL_SIG);
#define WL_X(F) WL_FN WL_##F(WL_SIG);
WL_TABLE WL_X(FID_ENTER)
#undef WL_X
#define WL_X(F) WL_##F,
static const WlFn wl_tab[] = { WL_TABLE };
#undef WL_X
#endif

static Term work_loop(Env e, DEV Term* sp, Term t, u32 seq) {
  WL_BANK
  u32 rn = 0;
  r0 = t;
#if DEVICE
  u32 fid   = FID_ENTER;
  u32 wpoll = 0;
  for (;;) {
  if (err_spun(e.mem, &wpoll)) {
    return 0;
  }
  switch (fid) {
#else
  return WL_FID_ENTER(WL_ALL);
}
#endif

// Segments
// ========

// A task enters through its words: a continuation's results ride r0..
// and its parameters the stack; any other segment's parameters ride r0..

#if !DEVICE
  WL_CASE(FID_SCORE_FRAC)
  {
    Term _fuel_0 = r0;
    Term _y_0 = r1;
    Term _bit_0 = r2;
    WL_OPEN
    WL_SPIN
    if (_fuel_0 == 0) {
      r0 = 0;
      WL_RETN(1);
    } else {
      Term _f_0 = (_fuel_0 - 1);
      Term _v_0 = 0;
      Term _v_1 = 0;
      Term _o_0[1];
      if (spin_4(e, _o_0, nat_mul(e, _y_0, _y_0), 65536ull) == 0) {
        return 0;
      }
      _v_1 = _o_0[0];
      _v_0 = _v_1;
      u32 _v_2 = 0;
      u32 _v_3 = 0;
      Term _o_2[1];
      if (spin_39(e, _o_2, 131072ull, _v_0) == 0) {
        return 0;
      }
      _v_3 = _o_2[0];
      _v_2 = _v_3;
      Term _v_7 = 0;
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _o_3[1];
      if (spin_4(e, _o_3, _v_0, 2ull) == 0) {
        return 0;
      }
      _v_9 = _o_3[0];
      _v_8 = _v_9;
      Term _v_10 = 0;
      Term _o_4[1];
      if (spin_41(e, _o_4, _v_2, _v_8, _v_0) == 0) {
        return 0;
      }
      _v_10 = _o_4[0];
      _v_7 = _v_10;
      Term _v_12 = 0;
      Term _v_13 = 0;
      Term _o_5[1];
      if (spin_4(e, _o_5, _bit_0, 2ull) == 0) {
        return 0;
      }
      _v_13 = _o_5[0];
      _v_12 = _v_13;
      if (seq) {
        WL_ROOM(3);
        STK(0) = _bit_0;
        STK(1) = _v_2;
        STK(2) = FID_SCORE_FRAC_K27;
        WL_PUSHN(3);
      } else {
        u64 _t_0 = task_node(e, FID_SCORE_FRAC_K27, WL_CONT, WL_IDX, 1);
        e.mem[_t_0 + 0] = _bit_0;
        e.mem[_t_0 + 1] = _v_2;
        WL_CONT = term_tsk(FID_SCORE_FRAC_K27, _t_0);
        WL_IDX = 2;
      }
      r0 = _f_0;
      r1 = _v_7;
      r2 = _v_12;
      _fuel_0 = r0;
      _y_0 = r1;
      _bit_0 = r2;
      WL_AGAIN(FID_SCORE_FRAC);
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_FRAC_K27)
  {
    WL_POPN(2);
    Term _bit_1 = STK(0);
    u32 _v_14 = STK(1);
    Term _h_0 = r0;
    WL_OPEN
    Term _v_15 = 0;
    Term _v_16 = 0;
    Term _o_6[1];
    if (spin_41(e, _o_6, _v_14, _bit_1, 0) == 0) {
      return 0;
    }
    _v_16 = _o_6[0];
    _v_15 = _v_16;
    r0 = nat_chk(e, _v_15 + _h_0);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_NAT_POW)
  {
    Term _a_0 = r0;
    Term _b_0 = r1;
    WL_OPEN
    WL_SPIN
    if (_b_0 == 0) {
      r0 = 1ull;
      WL_RETN(1);
    } else {
      Term _p_0 = (_b_0 - 1);
      if (seq) {
        WL_ROOM(2);
        STK(0) = _a_0;
        STK(1) = FID_NAT_POW_K29;
        WL_PUSHN(2);
      } else {
        u64 _t_0 = task_node(e, FID_NAT_POW_K29, WL_CONT, WL_IDX, 1);
        e.mem[_t_0 + 0] = _a_0;
        WL_CONT = term_tsk(FID_NAT_POW_K29, _t_0);
        WL_IDX = 1;
      }
      r0 = _a_0;
      r1 = _p_0;
      _a_0 = r0;
      _b_0 = r1;
      WL_AGAIN(FID_NAT_POW);
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_NAT_POW_K29)
  {
    WL_POPN(1);
    Term _a_1 = STK(0);
    Term _h_0 = r0;
    WL_OPEN
    r0 = nat_mul(e, _a_1, _h_0);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_ILOG2)
  {
    Term _fuel_0 = r0;
    u32 _more_0 = r1;
    Term _n_0 = r2;
    WL_OPEN
    WL_SPIN
    if (_fuel_0 == 0) {
      Term _fuel_1 = (_fuel_0 - 0);
      r0 = 0;
      WL_RETN(1);
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_more_0 == 1) {
        u32 _v_0 = 0;
        u32 _v_1 = 0;
        Term _o_0[1];
        if (spin_39(e, _o_0, 4ull, _n_0) == 0) {
          return 0;
        }
        _v_1 = _o_0[0];
        _v_0 = _v_1;
        Term _v_2 = 0;
        Term _v_3 = 0;
        Term _o_1[1];
        if (spin_4(e, _o_1, _n_0, 2ull) == 0) {
          return 0;
        }
        _v_3 = _o_1[0];
        _v_2 = _v_3;
        if (seq) {
          WL_ROOM(1);
          STK(0) = FID_SCORE_ILOG2_K31;
          WL_PUSHN(1);
        } else {
          u64 _t_0 = task_node(e, FID_SCORE_ILOG2_K31, WL_CONT, WL_IDX, 1);
          WL_CONT = term_tsk(FID_SCORE_ILOG2_K31, _t_0);
          WL_IDX = 0;
        }
        r0 = _f_0;
        r1 = _v_0;
        r2 = _v_2;
        _fuel_0 = r0;
        _more_0 = r1;
        _n_0 = r2;
        WL_AGAIN(FID_SCORE_ILOG2);
      } else {
        r0 = 0;
        WL_RETN(1);
      }
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_ILOG2_K31)
  {
    Term _h_0 = r0;
    WL_OPEN
    r0 = nat_chk(e, _h_0 + 1);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_LOG2FX)
  {
    Term _m_0 = r0;
    WL_OPEN
    u32 _v_0 = 0;
    u32 _v_1 = 0;
    Term _o_0[1];
    if (spin_39(e, _o_0, 2ull, _m_0) == 0) {
      return 0;
    }
    _v_1 = _o_0[0];
    _v_0 = _v_1;
    if (seq) {
      WL_ROOM(2);
      STK(0) = _m_0;
      STK(1) = FID_SCORE_LOG2FX_K36;
      WL_PUSHN(2);
    } else {
      u64 _t_0 = task_node(e, FID_SCORE_LOG2FX_K36, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _m_0;
      WL_CONT = term_tsk(FID_SCORE_LOG2FX_K36, _t_0);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_SCORE_ILOG2)) {
      u64 _t_1 = task_node(e, FID_SCORE_ILOG2, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = 64ull;
      e.mem[_t_1 + 1] = _v_0;
      e.mem[_t_1 + 2] = _m_0;
      return term_tsk(FID_SCORE_ILOG2, _t_1);
    }
    r0 = 64ull;
    r1 = _v_0;
    r2 = _m_0;
    WL_JMP(FID_SCORE_ILOG2);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_LOG2FX_K36)
  {
    WL_POPN(1);
    Term _m_1 = STK(0);
    Term _k_0 = r0;
    WL_OPEN
    if (seq) {
      WL_ROOM(3);
      STK(0) = _m_1;
      STK(1) = _k_0;
      STK(2) = FID_SCORE_LOG2FX_K37;
      WL_PUSHN(3);
    } else {
      u64 _t_2 = task_node(e, FID_SCORE_LOG2FX_K37, WL_CONT, WL_IDX, 1);
      e.mem[_t_2 + 0] = _m_1;
      e.mem[_t_2 + 1] = _k_0;
      WL_CONT = term_tsk(FID_SCORE_LOG2FX_K37, _t_2);
      WL_IDX = 2;
    }
    if (!DEVICE && !seq && fid_nofk(FID_NAT_POW)) {
      u64 _t_3 = task_node(e, FID_NAT_POW, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = 2ull;
      e.mem[_t_3 + 1] = _k_0;
      return term_tsk(FID_NAT_POW, _t_3);
    }
    r0 = 2ull;
    r1 = _k_0;
    WL_JMP(FID_NAT_POW);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_LOG2FX_K37)
  {
    WL_POPN(2);
    Term _m_2 = STK(0);
    Term _k_1 = STK(1);
    Term _h_0 = r0;
    WL_OPEN
    Term _v_2 = 0;
    Term _v_3 = 0;
    Term _o_1[1];
    if (spin_4(e, _o_1, nat_mul(e, _m_2, 65536ull), _h_0) == 0) {
      return 0;
    }
    _v_3 = _o_1[0];
    _v_2 = _v_3;
    if (seq) {
      WL_ROOM(2);
      STK(0) = _k_1;
      STK(1) = FID_SCORE_LOG2FX_K38;
      WL_PUSHN(2);
    } else {
      u64 _t_4 = task_node(e, FID_SCORE_LOG2FX_K38, WL_CONT, WL_IDX, 1);
      e.mem[_t_4 + 0] = _k_1;
      WL_CONT = term_tsk(FID_SCORE_LOG2FX_K38, _t_4);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_SCORE_FRAC)) {
      u64 _t_5 = task_node(e, FID_SCORE_FRAC, WL_CONT, WL_IDX, 0);
      e.mem[_t_5 + 0] = 16ull;
      e.mem[_t_5 + 1] = _v_2;
      e.mem[_t_5 + 2] = 32768ull;
      return term_tsk(FID_SCORE_FRAC, _t_5);
    }
    r0 = 16ull;
    r1 = _v_2;
    r2 = 32768ull;
    WL_JMP(FID_SCORE_FRAC);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_LOG2FX_K38)
  {
    WL_POPN(1);
    Term _k_2 = STK(0);
    Term _h_1 = r0;
    WL_OPEN
    r0 = nat_chk(e, nat_mul(e, _k_2, 65536ull) + _h_1);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_IDF_GO)
  {
    Term _n_0 = r0;
    Term _df_0 = r1;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _df_0;
      STK(1) = FID_SCORE_IDF_GO_K46;
      WL_PUSHN(2);
    } else {
      u64 _t_0 = task_node(e, FID_SCORE_IDF_GO_K46, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _df_0;
      WL_CONT = term_tsk(FID_SCORE_IDF_GO_K46, _t_0);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_SCORE_LOG2FX)) {
      u64 _t_1 = task_node(e, FID_SCORE_LOG2FX, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = nat_chk(e, nat_mul(e, 2ull, _n_0) + 2ull);
      return term_tsk(FID_SCORE_LOG2FX, _t_1);
    }
    r0 = nat_chk(e, nat_mul(e, 2ull, _n_0) + 2ull);
    WL_JMP(FID_SCORE_LOG2FX);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_IDF_GO_K46)
  {
    WL_POPN(1);
    Term _df_1 = STK(0);
    Term _h_0 = r0;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _h_0;
      STK(1) = FID_SCORE_IDF_GO_K47;
      WL_PUSHN(2);
    } else {
      u64 _t_2 = task_node(e, FID_SCORE_IDF_GO_K47, WL_CONT, WL_IDX, 1);
      e.mem[_t_2 + 0] = _h_0;
      WL_CONT = term_tsk(FID_SCORE_IDF_GO_K47, _t_2);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_SCORE_LOG2FX)) {
      u64 _t_3 = task_node(e, FID_SCORE_LOG2FX, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = nat_chk(e, nat_mul(e, 2ull, _df_1) + 1ull);
      return term_tsk(FID_SCORE_LOG2FX, _t_3);
    }
    r0 = nat_chk(e, nat_mul(e, 2ull, _df_1) + 1ull);
    WL_JMP(FID_SCORE_LOG2FX);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_IDF_GO_K47)
  {
    WL_POPN(1);
    Term _h_2 = STK(0);
    Term _h_1 = r0;
    WL_OPEN
    r0 = (_h_2 < _h_1 ? 0 : _h_2 - _h_1);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_SCORE_IDF)
  {
    Term _n_0 = r0;
    Term _df_0 = r1;
    WL_OPEN
    if (_df_0 == 0) {
      if (!DEVICE && !seq && fid_nofk(FID_SCORE_IDF_GO)) {
        u64 _t_0 = task_node(e, FID_SCORE_IDF_GO, WL_CONT, WL_IDX, 0);
        e.mem[_t_0 + 0] = _n_0;
        e.mem[_t_0 + 1] = 0;
        return term_tsk(FID_SCORE_IDF_GO, _t_0);
      }
      r0 = _n_0;
      r1 = 0;
      WL_JMP(FID_SCORE_IDF_GO);
    } else {
      Term _d_0 = (_df_0 - 1);
      if (!DEVICE && !seq && fid_nofk(FID_SCORE_IDF_GO)) {
        u64 _t_1 = task_node(e, FID_SCORE_IDF_GO, WL_CONT, WL_IDX, 0);
        e.mem[_t_1 + 0] = _n_0;
        e.mem[_t_1 + 1] = nat_chk(e, _d_0 + 1);
        return term_tsk(FID_SCORE_IDF_GO, _t_1);
      }
      r0 = _n_0;
      r1 = nat_chk(e, _d_0 + 1);
      WL_JMP(FID_SCORE_IDF_GO);
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_TERM)
  {
    Term _tot_0 = r0;
    Term _n_0 = r1;
    Term _x_0 = r2;
    Term _x_1 = r3;
    Term _x_2 = r4;
    WL_OPEN
    Term _at_0 = blk_loc(e.mem, _x_0);
    Term _at_1 = blk_at(_x_0, ((u64)(u32)(_x_2)), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    if (seq) {
      WL_ROOM(7);
      STK(0) = _x_2;
      STK(1) = _tot_0;
      STK(2) = _n_0;
      STK(3) = _x_1;
      STK(4) = _x_0;
      STK(5) = _c_0;
      STK(6) = FID_CORE_TERM_K88;
      WL_PUSHN(7);
    } else {
      u64 _t_0 = task_node(e, FID_CORE_TERM_K88, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _x_2;
      e.mem[_t_0 + 1] = _tot_0;
      e.mem[_t_0 + 2] = _n_0;
      e.mem[_t_0 + 3] = _x_1;
      e.mem[_t_0 + 4] = _x_0;
      e.mem[_t_0 + 5] = _c_0;
      WL_CONT = term_tsk(FID_CORE_TERM_K88, _t_0);
      WL_IDX = 6;
    }
    if (!DEVICE && !seq && fid_nofk(FID_SCORE_IDF)) {
      u64 _t_1 = task_node(e, FID_SCORE_IDF, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = _n_0;
      e.mem[_t_1 + 1] = _c_0;
      return term_tsk(FID_SCORE_IDF, _t_1);
    }
    r0 = _n_0;
    r1 = _c_0;
    WL_JMP(FID_SCORE_IDF);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_TERM_K88)
  {
    WL_POPN(6);
    Term _x_3 = STK(0);
    Term _tot_1 = STK(1);
    Term _n_1 = STK(2);
    Term _x_4 = STK(3);
    Term _x_5 = STK(4);
    Term _c_1 = STK(5);
    Term _h_0 = r0;
    WL_OPEN
    Term _v_0 = 0;
    Term _v_1 = 0;
    Term _v_2 = 0;
    Term _v_3 = 0;
    Term _o_0[2];
    if (spin_84(e, _o_0, _c_1, nat_chk(e, _x_3 + 1), _h_0, _tot_1, _n_1, _x_5, _x_4) == 0) {
      return 0;
    }
    _v_2 = _o_0[0];
    _v_3 = _o_0[1];
    _v_0 = _v_2;
    _v_1 = _v_3;
    Term _v_4 = 0;
    Term _v_5 = 0;
    Term _v_6 = 0;
    Term _o_1[3];
    if (spin_85(e, _o_1, nat_chk(e, nat_chk(e, _x_3 + 1) + nat_mul(e, 3ull, _c_1)), _v_0, _v_1) == 0) {
      return 0;
    }
    _v_4 = _o_1[0];
    _v_5 = _o_1[1];
    _v_6 = _o_1[2];
    r0 = _v_4;
    r1 = _v_5;
    r2 = _v_6;
    WL_RETN(3);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_ADD_TERMS)
  {
    Term _t_0 = r0;
    Term _tot_0 = r1;
    Term _n_0 = r2;
    Term _x_0 = r3;
    Term _x_1 = r4;
    Term _x_2 = r5;
    WL_OPEN
    if (_t_0 == 0) {
      r0 = _x_0;
      r1 = _x_1;
      r2 = _x_2;
      WL_RETN(3);
    } else {
      Term _u_0 = (_t_0 - 1);
      if (seq) {
        WL_ROOM(4);
        STK(0) = _u_0;
        STK(1) = _tot_0;
        STK(2) = _n_0;
        STK(3) = FID_CORE_ADD_TERMS_K110;
        WL_PUSHN(4);
      } else {
        u64 _t_1 = task_node(e, FID_CORE_ADD_TERMS_K110, WL_CONT, WL_IDX, 1);
        e.mem[_t_1 + 0] = _u_0;
        e.mem[_t_1 + 1] = _tot_0;
        e.mem[_t_1 + 2] = _n_0;
        WL_CONT = term_tsk(FID_CORE_ADD_TERMS_K110, _t_1);
        WL_IDX = 3;
      }
      if (!DEVICE && !seq && fid_nofk(FID_CORE_TERM)) {
        u64 _t_2 = task_node(e, FID_CORE_TERM, WL_CONT, WL_IDX, 0);
        e.mem[_t_2 + 0] = _tot_0;
        e.mem[_t_2 + 1] = _n_0;
        e.mem[_t_2 + 2] = _x_0;
        e.mem[_t_2 + 3] = _x_1;
        e.mem[_t_2 + 4] = _x_2;
        return term_tsk(FID_CORE_TERM, _t_2);
      }
      r0 = _tot_0;
      r1 = _n_0;
      r2 = _x_0;
      r3 = _x_1;
      r4 = _x_2;
      WL_JMP(FID_CORE_TERM);
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_ADD_TERMS_K110)
  {
    WL_POPN(3);
    Term _u_1 = STK(0);
    Term _tot_1 = STK(1);
    Term _n_1 = STK(2);
    Term _h_0 = r0;
    Term _h_1 = r1;
    Term _h_2 = r2;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_CORE_ADD_TERMS)) {
      u64 _t_3 = task_node(e, FID_CORE_ADD_TERMS, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _u_1;
      e.mem[_t_3 + 1] = _tot_1;
      e.mem[_t_3 + 2] = _n_1;
      e.mem[_t_3 + 3] = _h_0;
      e.mem[_t_3 + 4] = _h_1;
      e.mem[_t_3 + 5] = _h_2;
      return term_tsk(FID_CORE_ADD_TERMS, _t_3);
    }
    r0 = _u_1;
    r1 = _tot_1;
    r2 = _n_1;
    r3 = _h_0;
    r4 = _h_1;
    r5 = _h_2;
    WL_JMP(FID_CORE_ADD_TERMS);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_RANK)
  {
    Term _k_0 = r0;
    Term _n_0 = r1;
    Term _tot_0 = r2;
    Term _p_0 = r3;
    Term _t_0 = r4;
    WL_OPEN
    Term _v_0 = 0;
    Term _v_1 = 0;
    Term _o_0[1];
    if (spin_80(e, _o_0, _n_0) == 0) {
      return 0;
    }
    _v_1 = _o_0[0];
    _v_0 = _v_1;
    Term _fv_0[1];
    _fv_0[0] = 0;
    if (seq) {
      WL_ROOM(3);
      STK(0) = _k_0;
      STK(1) = _n_0;
      STK(2) = FID_CORE_RANK_K130;
      WL_PUSHN(3);
    } else {
      u64 _t_1 = task_node(e, FID_CORE_RANK_K130, WL_CONT, WL_IDX, 1);
      e.mem[_t_1 + 0] = _k_0;
      e.mem[_t_1 + 1] = _n_0;
      WL_CONT = term_tsk(FID_CORE_RANK_K130, _t_1);
      WL_IDX = 2;
    }
    if (!DEVICE && !seq && fid_nofk(FID_CORE_ADD_TERMS)) {
      u64 _t_2 = task_node(e, FID_CORE_ADD_TERMS, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = _t_0;
      e.mem[_t_2 + 1] = _tot_0;
      e.mem[_t_2 + 2] = _n_0;
      e.mem[_t_2 + 3] = _p_0;
      e.mem[_t_2 + 4] = blk_new(e, 1, _v_0, 0, 1, _fv_0);
      e.mem[_t_2 + 5] = 0;
      return term_tsk(FID_CORE_ADD_TERMS, _t_2);
    }
    r0 = _t_0;
    r1 = _tot_0;
    r2 = _n_0;
    r3 = _p_0;
    r4 = blk_new(e, 1, _v_0, 0, 1, _fv_0);
    r5 = 0;
    WL_JMP(FID_CORE_ADD_TERMS);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_RANK_K130)
  {
    WL_POPN(2);
    Term _k_1 = STK(0);
    Term _n_1 = STK(1);
    Term _h_0 = r0;
    Term _h_1 = r1;
    Term _h_2 = r2;
    WL_OPEN
    Term _v_2 = 0;
    Term _v_3 = 0;
    Term _o_4[2];
    if (spin_122(e, _o_4, _k_1, _n_1, _h_0, _h_1, _h_2) == 0) {
      return 0;
    }
    _v_2 = _o_4[0];
    _v_3 = _o_4[1];
    r0 = _v_2;
    r1 = _v_3;
    WL_RETN(2);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_CHUNK_GO)
  {
    Term _fuel_0 = r0;
    Term _o_0 = r1;
    Term _n_0 = r2;
    Term _x_0 = r3;
    Term _x_1 = r4;
    Term _x_2 = r5;
    u32 _x_3 = r6;
    WL_OPEN
    WL_SPIN
    if (_fuel_0 == 0) {
      u64 _nd_0 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_0 + 0] = _x_1;
      e.mem[_nd_0 + 1] = _n_0;
      u64 _nd_1 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_1 + 0] = term_ctr(CID_CORE_CHUNK, _nd_0);
      e.mem[_nd_1 + 1] = term_pak(CID_NIL, 0);
      r0 = _x_0;
      r1 = term_ctr(CID_CON, _nd_1);
      WL_RETN(2);
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_3 == 0) {
        u64 _nd_2 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_2 + 0] = _x_1;
        e.mem[_nd_2 + 1] = _n_0;
        u64 _nd_3 = heap_alloc(e, cls_fit(2));
        e.mem[_nd_3 + 0] = term_ctr(CID_CORE_CHUNK, _nd_2);
        e.mem[_nd_3 + 1] = term_pak(CID_NIL, 0);
        r0 = _x_0;
        r1 = term_ctr(CID_CON, _nd_3);
        WL_RETN(2);
      } else {
        Term _v_0 = 0;
        Term _v_1 = 0;
        Term _v_2 = 0;
        u32 _v_3 = 0;
        Term _v_4 = 0;
        Term _v_5 = 0;
        Term _v_6 = 0;
        Term _v_7 = 0;
        Term _o_7[2];
        if (spin_127(e, _o_7, _x_0, _o_0, _x_1, _x_2) == 0) {
          return 0;
        }
        _v_6 = _o_7[0];
        _v_7 = _o_7[1];
        _v_4 = _v_6;
        _v_5 = _v_7;
        Term _v_32 = 0;
        Term _v_33 = 0;
        Term _v_34 = 0;
        u32 _v_35 = 0;
        Term _o_11[4];
        if (spin_128(e, _o_11, _o_0, _n_0, _v_4, _v_5) == 0) {
          return 0;
        }
        _v_32 = _o_11[0];
        _v_33 = _o_11[1];
        _v_34 = _o_11[2];
        _v_35 = _o_11[3];
        _v_0 = _v_32;
        _v_1 = _v_33;
        _v_2 = _v_34;
        _v_3 = _v_35;
        if (seq) {
          WL_ROOM(3);
          STK(0) = _x_1;
          STK(1) = _x_2;
          STK(2) = FID_CORE_CHUNK_GO_K143;
          WL_PUSHN(3);
        } else {
          u64 _t_0 = task_node(e, FID_CORE_CHUNK_GO_K143, WL_CONT, WL_IDX, 1);
          e.mem[_t_0 + 0] = _x_1;
          e.mem[_t_0 + 1] = _x_2;
          WL_CONT = term_tsk(FID_CORE_CHUNK_GO_K143, _t_0);
          WL_IDX = 2;
        }
        r0 = _f_0;
        r1 = _o_0;
        r2 = _n_0;
        r3 = _v_0;
        r4 = _v_1;
        r5 = _v_2;
        r6 = _v_3;
        _fuel_0 = r0;
        _o_0 = r1;
        _n_0 = r2;
        _x_0 = r3;
        _x_1 = r4;
        _x_2 = r5;
        _x_3 = r6;
        WL_AGAIN(FID_CORE_CHUNK_GO);
      }
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_CHUNK_GO_K143)
  {
    WL_POPN(2);
    Term _x_4 = STK(0);
    Term _x_5 = STK(1);
    Term _h_0 = r0;
    Term _h_1 = r1;
    WL_OPEN
    Term _v_48 = 0;
    Term _v_49 = 0;
    Term _o_12[2];
    if (spin_129(e, _o_12, _x_4, _x_5, _h_0, _h_1) == 0) {
      return 0;
    }
    _v_48 = _o_12[0];
    _v_49 = _o_12[1];
    r0 = _v_48;
    r1 = _v_49;
    WL_RETN(2);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_LIST_LENGTH)
  {
    Term _xs_0 = r0;
    WL_OPEN
    WL_SPIN
    if (term_aux(_xs_0) == CID_NIL) {
      r0 = 0;
      WL_RETN(1);
    } else {
      u64 _sp_0 = term_peek(e.mem, _xs_0);
      Term _f_0 = e.mem[_sp_0 + 0];
      Term _f_1 = e.mem[_sp_0 + 1];
      if (seq) {
        WL_ROOM(1);
        STK(0) = FID_LIST_LENGTH_K145;
        WL_PUSHN(1);
      } else {
        u64 _t_0 = task_node(e, FID_LIST_LENGTH_K145, WL_CONT, WL_IDX, 1);
        WL_CONT = term_tsk(FID_LIST_LENGTH_K145, _t_0);
        WL_IDX = 0;
      }
      r0 = _f_1;
      _xs_0 = r0;
      WL_AGAIN(FID_LIST_LENGTH);
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_LIST_LENGTH_K145)
  {
    Term _h_0 = r0;
    WL_OPEN
    r0 = nat_chk(e, _h_0 + 1);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_CORE_CHUNK_DOC)
  {
    Term _a_0 = r0;
    Term _o_0 = r1;
    Term _n_0 = r2;
    WL_OPEN
    if (_n_0 == 0) {
      r0 = _a_0;
      r1 = term_pak(CID_NIL, 0);
      WL_RETN(2);
    } else {
      Term _m_0 = (_n_0 - 1);
      Term _v_0 = 0;
      Term _v_1 = 0;
      Term _v_2 = 0;
      u32 _v_3 = 0;
      Term _v_4 = 0;
      Term _v_5 = 0;
      Term _v_6 = 0;
      Term _v_7 = 0;
      Term _o_1[2];
      if (spin_117(e, _o_1, _a_0, _o_0, 0, nat_chk(e, _m_0 + 1)) == 0) {
        return 0;
      }
      _v_6 = _o_1[0];
      _v_7 = _o_1[1];
      _v_4 = _v_6;
      _v_5 = _v_7;
      Term _v_8 = 0;
      Term _v_9 = 0;
      Term _v_10 = 0;
      u32 _v_11 = 0;
      Term _o_2[4];
      if (spin_119(e, _o_2, 0, nat_chk(e, _m_0 + 1), _v_4, _v_5) == 0) {
        return 0;
      }
      _v_8 = _o_2[0];
      _v_9 = _o_2[1];
      _v_10 = _o_2[2];
      _v_11 = _o_2[3];
      _v_0 = _v_8;
      _v_1 = _v_9;
      _v_2 = _v_10;
      _v_3 = _v_11;
      if (!DEVICE && !seq && fid_nofk(FID_CORE_CHUNK_GO)) {
        u64 _t_0 = task_node(e, FID_CORE_CHUNK_GO, WL_CONT, WL_IDX, 0);
        e.mem[_t_0 + 0] = nat_chk(e, _m_0 + 1);
        e.mem[_t_0 + 1] = _o_0;
        e.mem[_t_0 + 2] = nat_chk(e, _m_0 + 1);
        e.mem[_t_0 + 3] = _v_0;
        e.mem[_t_0 + 4] = _v_1;
        e.mem[_t_0 + 5] = _v_2;
        e.mem[_t_0 + 6] = _v_3;
        return term_tsk(FID_CORE_CHUNK_GO, _t_0);
      }
      r0 = nat_chk(e, _m_0 + 1);
      r1 = _o_0;
      r2 = nat_chk(e, _m_0 + 1);
      r3 = _v_0;
      r4 = _v_1;
      r5 = _v_2;
      r6 = _v_3;
      WL_JMP(FID_CORE_CHUNK_GO);
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_PURE)
  {
    Term _x_0 = r0;
    Term _k_0 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_0 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_0 + 0] = _k_0;
      e.mem[_t_0 + 1] = _x_0;
      return term_tsk(FID_CLO_APPLY, _t_0);
    }
    r0 = _k_0;
    r1 = _x_0;
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_FRAME)
  {
    Term _x_0 = r0;
    Term _x_1 = r1;
    Term _x_2 = r2;
    Term _x_3 = r3;
    Term _x_4 = r4;
    Term _x_5 = r5;
    Term _x_6 = r6;
    Term _x_7 = r7;
    WL_OPEN
    u32 _s_0 = (_x_2 < _x_1);
    if (_s_0 == 0) {
      term_sink(e, _x_6);
      term_sink(e, _x_0);
      r0 = 0;
      r1 = 0;
      r2 = 0;
      r3 = 0;
      r4 = 0;
      r5 = 0;
      r6 = 0;
      r7 = 0;
      r8 = 0;
      r9 = 0;
      r10 = 0;
      WL_RETN(11);
    } else {
      Term _v_0 = 0;
      u32 _v_1 = 0;
      Term _v_2 = 0;
      u32 _v_3 = 0;
      Term _o_0[2];
      if (spin_3(e, _o_0, _x_0, _x_2) == 0) {
        return 0;
      }
      _v_2 = _o_0[0];
      _v_3 = _o_0[1];
      _v_0 = _v_2;
      _v_1 = _v_3;
      u32 _v_4 = 0;
      u32 _v_5 = 0;
      Term _o_1[1];
      if (spin_151(e, _o_1, _v_1) == 0) {
        return 0;
      }
      _v_5 = _o_1[0];
      _v_4 = _v_5;
      if (_v_4 == 0) {
        Term _a_0 = nat_chk(e, _x_2 + 1);
        Term _v_6 = 0;
        Term _v_7 = 0;
        Term _v_8 = 0;
        u32 _v_9 = 0;
        Term _v_10 = 0;
        Term _v_11 = 0;
        Term _v_12 = 0;
        u32 _v_13 = 0;
        Term _v_14 = 0;
        Term _v_15 = 0;
        Term _v_16 = 0;
        u32 _v_17 = 0;
        Term _o_2[4];
        if (spin_56(e, _o_2, _v_0, _x_1, _a_0) == 0) {
          return 0;
        }
        _v_14 = _o_2[0];
        _v_15 = _o_2[1];
        _v_16 = _o_2[2];
        _v_17 = _o_2[3];
        _v_10 = _v_14;
        _v_11 = _v_15;
        _v_12 = _v_16;
        _v_13 = _v_17;
        Term _v_18 = 0;
        Term _v_19 = 0;
        Term _v_20 = 0;
        u32 _v_21 = 0;
        Term _o_3[4];
        if (spin_147(e, _o_3, 0, _x_1, term_pak(CID_NIL, 0), _v_10, _v_11, _v_12, _v_13) == 0) {
          return 0;
        }
        _v_18 = _o_3[0];
        _v_19 = _o_3[1];
        _v_20 = _o_3[2];
        _v_21 = _o_3[3];
        _v_6 = _v_18;
        _v_7 = _v_19;
        _v_8 = _v_20;
        _v_9 = _v_21;
        if (_v_9 == 1) {
          if (term_aux(_v_7) == CID_CON) {
            u64 _sp_0 = term_loc(_v_7);
            Term _f_0 = e.mem[_sp_0 + 0];
            Term _f_1 = e.mem[_sp_0 + 1];
            if (term_aux(_f_1) == CID_NIL) {
              u32 _v_22 = 0;
              u32 _v_23 = 0;
              Term _o_4[1];
              if (spin_39(e, _o_4, nat_chk(e, _v_8 + _f_0), _x_1) == 0) {
                return 0;
              }
              _v_23 = _o_4[0];
              _v_22 = _v_23;
              if (_v_22 == 0) {
                term_sink(e, _v_6);
                term_sink(e, _x_6);
                heap_free(e, cls_fit(2), _sp_0);
                r0 = 1;
                r1 = term_ctr(CID_SCON, STAT_OFF + 358);
                r2 = 0;
                r3 = 0;
                r4 = 0;
                r5 = 0;
                r6 = 0;
                r7 = 0;
                r8 = 0;
                r9 = 0;
                r10 = 0;
                WL_RETN(11);
              } else {
                heap_free(e, cls_fit(2), _sp_0);
                if (seq) {
                  WL_ROOM(9);
                  STK(0) = _x_1;
                  STK(1) = _v_8;
                  STK(2) = _f_0;
                  STK(3) = _x_3;
                  STK(4) = _x_4;
                  STK(5) = _x_5;
                  STK(6) = _x_6;
                  STK(7) = _x_7;
                  STK(8) = FID_FRAME_K228;
                  WL_PUSHN(9);
                } else {
                  u64 _t_0 = task_node(e, FID_FRAME_K228, WL_CONT, WL_IDX, 1);
                  e.mem[_t_0 + 0] = _x_1;
                  e.mem[_t_0 + 1] = _v_8;
                  e.mem[_t_0 + 2] = _f_0;
                  e.mem[_t_0 + 3] = _x_3;
                  e.mem[_t_0 + 4] = _x_4;
                  e.mem[_t_0 + 5] = _x_5;
                  e.mem[_t_0 + 6] = _x_6;
                  e.mem[_t_0 + 7] = _x_7;
                  WL_CONT = term_tsk(FID_FRAME_K228, _t_0);
                  WL_IDX = 8;
                }
                if (!DEVICE && !seq && fid_nofk(FID_CORE_CHUNK_DOC)) {
                  u64 _t_1 = task_node(e, FID_CORE_CHUNK_DOC, WL_CONT, WL_IDX, 0);
                  e.mem[_t_1 + 0] = _v_6;
                  e.mem[_t_1 + 1] = _v_8;
                  e.mem[_t_1 + 2] = _f_0;
                  return term_tsk(FID_CORE_CHUNK_DOC, _t_1);
                }
                r0 = _v_6;
                r1 = _v_8;
                r2 = _f_0;
                WL_JMP(FID_CORE_CHUNK_DOC);
              }
            } else {
              term_sink(e, _f_1);
              term_sink(e, _x_6);
              term_sink(e, _v_6);
              heap_free(e, cls_fit(2), _sp_0);
              r0 = 1;
              r1 = term_ctr(CID_SCON, STAT_OFF + 314);
              r2 = 0;
              r3 = 0;
              r4 = 0;
              r5 = 0;
              r6 = 0;
              r7 = 0;
              r8 = 0;
              r9 = 0;
              r10 = 0;
              WL_RETN(11);
            }
          } else {
            term_sink(e, _v_7);
            term_sink(e, _x_6);
            term_sink(e, _v_6);
            r0 = 1;
            r1 = term_ctr(CID_SCON, STAT_OFF + 314);
            r2 = 0;
            r3 = 0;
            r4 = 0;
            r5 = 0;
            r6 = 0;
            r7 = 0;
            r8 = 0;
            r9 = 0;
            r10 = 0;
            WL_RETN(11);
          }
        } else {
          term_sink(e, _v_7);
          term_sink(e, _x_6);
          term_sink(e, _v_6);
          r0 = 1;
          r1 = term_ctr(CID_SCON, STAT_OFF + 314);
          r2 = 0;
          r3 = 0;
          r4 = 0;
          r5 = 0;
          r6 = 0;
          r7 = 0;
          r8 = 0;
          r9 = 0;
          r10 = 0;
          WL_RETN(11);
        }
      } else if (_v_4 == 1) {
        Term _a_1 = nat_chk(e, _x_2 + 1);
        Term _v_33 = 0;
        Term _v_34 = 0;
        Term _v_35 = 0;
        u32 _v_36 = 0;
        Term _v_37 = 0;
        Term _v_38 = 0;
        Term _v_39 = 0;
        u32 _v_40 = 0;
        Term _v_41 = 0;
        Term _v_42 = 0;
        Term _v_43 = 0;
        u32 _v_44 = 0;
        Term _o_7[4];
        if (spin_56(e, _o_7, _v_0, _x_1, _a_1) == 0) {
          return 0;
        }
        _v_41 = _o_7[0];
        _v_42 = _o_7[1];
        _v_43 = _o_7[2];
        _v_44 = _o_7[3];
        _v_37 = _v_41;
        _v_38 = _v_42;
        _v_39 = _v_43;
        _v_40 = _v_44;
        Term _v_45 = 0;
        Term _v_46 = 0;
        Term _v_47 = 0;
        u32 _v_48 = 0;
        Term _o_8[4];
        if (spin_147(e, _o_8, 0, _x_1, term_pak(CID_NIL, 0), _v_37, _v_38, _v_39, _v_40) == 0) {
          return 0;
        }
        _v_45 = _o_8[0];
        _v_46 = _o_8[1];
        _v_47 = _o_8[2];
        _v_48 = _o_8[3];
        _v_33 = _v_45;
        _v_34 = _v_46;
        _v_35 = _v_47;
        _v_36 = _v_48;
        u32 _v_49 = 0;
        Term _v_50 = 0;
        Term _v_51 = 0;
        Term _v_52 = 0;
        Term _v_53 = 0;
        Term _v_54 = 0;
        Term _v_55 = 0;
        Term _v_56 = 0;
        Term _v_57 = 0;
        Term _v_58 = 0;
        Term _v_59 = 0;
        Term _o_9[11];
        if (spin_149(e, _o_9, _x_1, _x_3, _x_4, _x_5, _x_6, _x_7, _v_33, _v_34, _v_35, _v_36) == 0) {
          return 0;
        }
        _v_49 = _o_9[0];
        _v_50 = _o_9[1];
        _v_51 = _o_9[2];
        _v_52 = _o_9[3];
        _v_53 = _o_9[4];
        _v_54 = _o_9[5];
        _v_55 = _o_9[6];
        _v_56 = _o_9[7];
        _v_57 = _o_9[8];
        _v_58 = _o_9[9];
        _v_59 = _o_9[10];
        r0 = _v_49;
        r1 = _v_50;
        r2 = _v_51;
        r3 = _v_52;
        r4 = _v_53;
        r5 = _v_54;
        r6 = _v_55;
        r7 = _v_56;
        r8 = _v_57;
        r9 = _v_58;
        r10 = _v_59;
        WL_RETN(11);
      } else if (_v_4 == 2) {
        Term _a_2 = nat_chk(e, _x_2 + 1);
        Term _v_60 = 0;
        Term _v_61 = 0;
        Term _v_62 = 0;
        u32 _v_63 = 0;
        Term _v_64 = 0;
        Term _v_65 = 0;
        Term _v_66 = 0;
        u32 _v_67 = 0;
        Term _v_68 = 0;
        Term _v_69 = 0;
        Term _v_70 = 0;
        u32 _v_71 = 0;
        Term _o_10[4];
        if (spin_56(e, _o_10, _v_0, _x_1, _a_2) == 0) {
          return 0;
        }
        _v_68 = _o_10[0];
        _v_69 = _o_10[1];
        _v_70 = _o_10[2];
        _v_71 = _o_10[3];
        _v_64 = _v_68;
        _v_65 = _v_69;
        _v_66 = _v_70;
        _v_67 = _v_71;
        Term _v_72 = 0;
        Term _v_73 = 0;
        Term _v_74 = 0;
        u32 _v_75 = 0;
        Term _o_11[4];
        if (spin_147(e, _o_11, 1ull, _x_1, term_pak(CID_NIL, 0), _v_64, _v_65, _v_66, _v_67) == 0) {
          return 0;
        }
        _v_72 = _o_11[0];
        _v_73 = _o_11[1];
        _v_74 = _o_11[2];
        _v_75 = _o_11[3];
        _v_60 = _v_72;
        _v_61 = _v_73;
        _v_62 = _v_74;
        _v_63 = _v_75;
        u32 _v_76 = 0;
        Term _v_77 = 0;
        Term _v_78 = 0;
        Term _v_79 = 0;
        Term _v_80 = 0;
        Term _v_81 = 0;
        Term _v_82 = 0;
        Term _v_83 = 0;
        Term _v_84 = 0;
        Term _v_85 = 0;
        Term _v_86 = 0;
        Term _o_12[11];
        if (spin_150(e, _o_12, _x_1, _x_3, _x_4, _x_5, _x_6, _x_7, _v_60, _v_61, _v_62, _v_63) == 0) {
          return 0;
        }
        _v_76 = _o_12[0];
        _v_77 = _o_12[1];
        _v_78 = _o_12[2];
        _v_79 = _o_12[3];
        _v_80 = _o_12[4];
        _v_81 = _o_12[5];
        _v_82 = _o_12[6];
        _v_83 = _o_12[7];
        _v_84 = _o_12[8];
        _v_85 = _o_12[9];
        _v_86 = _o_12[10];
        r0 = _v_76;
        r1 = _v_77;
        r2 = _v_78;
        r3 = _v_79;
        r4 = _v_80;
        r5 = _v_81;
        r6 = _v_82;
        r7 = _v_83;
        r8 = _v_84;
        r9 = _v_85;
        r10 = _v_86;
        WL_RETN(11);
      } else if (_v_4 == 3) {
        Term _a_3 = nat_chk(e, _x_2 + 1);
        Term _v_87 = 0;
        Term _v_88 = 0;
        Term _v_89 = 0;
        u32 _v_90 = 0;
        Term _v_91 = 0;
        Term _v_92 = 0;
        Term _v_93 = 0;
        u32 _v_94 = 0;
        Term _v_95 = 0;
        Term _v_96 = 0;
        Term _v_97 = 0;
        u32 _v_98 = 0;
        Term _o_13[4];
        if (spin_56(e, _o_13, _v_0, _x_1, _a_3) == 0) {
          return 0;
        }
        _v_95 = _o_13[0];
        _v_96 = _o_13[1];
        _v_97 = _o_13[2];
        _v_98 = _o_13[3];
        _v_91 = _v_95;
        _v_92 = _v_96;
        _v_93 = _v_97;
        _v_94 = _v_98;
        Term _v_99 = 0;
        Term _v_100 = 0;
        Term _v_101 = 0;
        u32 _v_102 = 0;
        Term _o_14[4];
        if (spin_147(e, _o_14, 3ull, _x_1, term_pak(CID_NIL, 0), _v_91, _v_92, _v_93, _v_94) == 0) {
          return 0;
        }
        _v_99 = _o_14[0];
        _v_100 = _o_14[1];
        _v_101 = _o_14[2];
        _v_102 = _o_14[3];
        _v_87 = _v_99;
        _v_88 = _v_100;
        _v_89 = _v_101;
        _v_90 = _v_102;
        if (_v_90 == 1) {
          if (term_aux(_v_88) == CID_CON) {
            u64 _sp_1 = term_loc(_v_88);
            Term _f_3 = e.mem[_sp_1 + 0];
            Term _f_4 = e.mem[_sp_1 + 1];
            if (term_aux(_f_4) == CID_CON) {
              u64 _sp_2 = term_loc(_f_4);
              Term _f_5 = e.mem[_sp_2 + 0];
              Term _f_6 = e.mem[_sp_2 + 1];
              if (term_aux(_f_6) == CID_CON) {
                u64 _sp_3 = term_loc(_f_6);
                Term _f_7 = e.mem[_sp_3 + 0];
                Term _f_8 = e.mem[_sp_3 + 1];
                if (term_aux(_f_8) == CID_CON) {
                  u64 _sp_4 = term_loc(_f_8);
                  Term _f_9 = e.mem[_sp_4 + 0];
                  Term _f_10 = e.mem[_sp_4 + 1];
                  if (term_aux(_f_10) == CID_NIL) {
                    Term _v_103 = 0;
                    Term _v_104 = 0;
                    Term _v_105 = 0;
                    Term _v_106 = 0;
                    u32 _v_107 = 0;
                    Term _v_108 = 0;
                    Term _v_109 = 0;
                    Term _fv_0[1];
                    _fv_0[0] = 0ull;
                    Term _v_110 = 0;
                    Term _v_111 = 0;
                    Term _v_112 = 0;
                    Term _v_113 = 0;
                    u32 _v_114 = 0;
                    Term _v_115 = 0;
                    Term _v_116 = 0;
                    Term _o_15[7];
                    if (spin_141(e, _o_15, _f_3, _x_1, _f_7, _v_87, _v_89, blk_new(e, 0, 10ull, 0, 1, _fv_0), 0, 1, 0, 0) == 0) {
                      return 0;
                    }
                    _v_110 = _o_15[0];
                    _v_111 = _o_15[1];
                    _v_112 = _o_15[2];
                    _v_113 = _o_15[3];
                    _v_114 = _o_15[4];
                    _v_115 = _o_15[5];
                    _v_116 = _o_15[6];
                    _v_103 = _v_110;
                    _v_104 = _v_111;
                    _v_105 = _v_112;
                    _v_106 = _v_113;
                    _v_107 = _v_114;
                    _v_108 = _v_115;
                    _v_109 = _v_116;
                    if (_v_107 == 0) {
                      term_sink(e, _x_6);
                      term_sink(e, _v_103);
                      term_sink(e, _v_105);
                      heap_free(e, cls_fit(2), _sp_4);
                      heap_free(e, cls_fit(2), _sp_3);
                      heap_free(e, cls_fit(2), _sp_2);
                      heap_free(e, cls_fit(2), _sp_1);
                      r0 = 1;
                      r1 = term_ctr(CID_SCON, STAT_OFF + 156);
                      r2 = 0;
                      r3 = 0;
                      r4 = 0;
                      r5 = 0;
                      r6 = 0;
                      r7 = 0;
                      r8 = 0;
                      r9 = 0;
                      r10 = 0;
                      WL_RETN(11);
                    } else {
                      u32 _v_117 = 0;
                      u32 _v_118 = 0;
                      u32 _v_119 = 0;
                      Term _o_16[1];
                      if (spin_11(e, _o_16, (_f_7 < 16777216ull), (_v_108 < 65536ull)) == 0) {
                        return 0;
                      }
                      _v_119 = _o_16[0];
                      _v_118 = _v_119;
                      u32 _v_120 = 0;
                      u32 _v_121 = 0;
                      u32 _v_122 = 0;
                      Term _o_17[1];
                      if (spin_11(e, _o_17, (_v_109 < 16384ull), (_f_3 < 32768ull)) == 0) {
                        return 0;
                      }
                      _v_122 = _o_17[0];
                      _v_121 = _v_122;
                      u32 _v_123 = 0;
                      Term _o_18[1];
                      if (spin_11(e, _o_18, _v_121, (_f_5 < nat_mul(e, 1048576ull, 1048576ull))) == 0) {
                        return 0;
                      }
                      _v_123 = _o_18[0];
                      _v_120 = _v_123;
                      u32 _v_124 = 0;
                      Term _o_19[1];
                      if (spin_11(e, _o_19, _v_118, _v_120) == 0) {
                        return 0;
                      }
                      _v_124 = _o_19[0];
                      _v_117 = _v_124;
                      u32 _v_125 = 0;
                      u32 _v_126 = 0;
                      Term _o_20[1];
                      if (spin_137(e, _o_20, _v_117, _f_7, _f_5, _v_109) == 0) {
                        return 0;
                      }
                      _v_126 = _o_20[0];
                      _v_125 = _v_126;
                      if (_v_125 == 0) {
                        term_sink(e, _x_6);
                        term_sink(e, _v_103);
                        term_sink(e, _v_105);
                        heap_free(e, cls_fit(2), _sp_4);
                        heap_free(e, cls_fit(2), _sp_3);
                        heap_free(e, cls_fit(2), _sp_2);
                        heap_free(e, cls_fit(2), _sp_1);
                        r0 = 1;
                        r1 = term_ctr(CID_SCON, STAT_OFF + 106);
                        r2 = 0;
                        r3 = 0;
                        r4 = 0;
                        r5 = 0;
                        r6 = 0;
                        r7 = 0;
                        r8 = 0;
                        r9 = 0;
                        r10 = 0;
                        WL_RETN(11);
                      } else {
                        heap_free(e, cls_fit(2), _sp_4);
                        heap_free(e, cls_fit(2), _sp_3);
                        heap_free(e, cls_fit(2), _sp_2);
                        heap_free(e, cls_fit(2), _sp_1);
                        if (seq) {
                          WL_ROOM(9);
                          STK(0) = _x_1;
                          STK(1) = _v_104;
                          STK(2) = _x_3;
                          STK(3) = _x_4;
                          STK(4) = _x_5;
                          STK(5) = _x_6;
                          STK(6) = _x_7;
                          STK(7) = _v_103;
                          STK(8) = FID_FRAME_K230;
                          WL_PUSHN(9);
                        } else {
                          u64 _t_4 = task_node(e, FID_FRAME_K230, WL_CONT, WL_IDX, 1);
                          e.mem[_t_4 + 0] = _x_1;
                          e.mem[_t_4 + 1] = _v_104;
                          e.mem[_t_4 + 2] = _x_3;
                          e.mem[_t_4 + 3] = _x_4;
                          e.mem[_t_4 + 4] = _x_5;
                          e.mem[_t_4 + 5] = _x_6;
                          e.mem[_t_4 + 6] = _x_7;
                          e.mem[_t_4 + 7] = _v_103;
                          WL_CONT = term_tsk(FID_FRAME_K230, _t_4);
                          WL_IDX = 8;
                        }
                        if (!DEVICE && !seq && fid_nofk(FID_CORE_RANK)) {
                          u64 _t_5 = task_node(e, FID_CORE_RANK, WL_CONT, WL_IDX, 0);
                          e.mem[_t_5 + 0] = _f_9;
                          e.mem[_t_5 + 1] = _f_7;
                          e.mem[_t_5 + 2] = _f_5;
                          e.mem[_t_5 + 3] = _v_105;
                          e.mem[_t_5 + 4] = _f_3;
                          return term_tsk(FID_CORE_RANK, _t_5);
                        }
                        r0 = _f_9;
                        r1 = _f_7;
                        r2 = _f_5;
                        r3 = _v_105;
                        r4 = _f_3;
                        WL_JMP(FID_CORE_RANK);
                      }
                    }
                  } else {
                    term_sink(e, _f_10);
                    term_sink(e, _x_6);
                    term_sink(e, _v_87);
                    heap_free(e, cls_fit(2), _sp_4);
                    heap_free(e, cls_fit(2), _sp_3);
                    heap_free(e, cls_fit(2), _sp_2);
                    heap_free(e, cls_fit(2), _sp_1);
                    r0 = 1;
                    r1 = term_ctr(CID_SCON, STAT_OFF + 314);
                    r2 = 0;
                    r3 = 0;
                    r4 = 0;
                    r5 = 0;
                    r6 = 0;
                    r7 = 0;
                    r8 = 0;
                    r9 = 0;
                    r10 = 0;
                    WL_RETN(11);
                  }
                } else {
                  term_sink(e, _f_8);
                  term_sink(e, _x_6);
                  term_sink(e, _v_87);
                  heap_free(e, cls_fit(2), _sp_3);
                  heap_free(e, cls_fit(2), _sp_2);
                  heap_free(e, cls_fit(2), _sp_1);
                  r0 = 1;
                  r1 = term_ctr(CID_SCON, STAT_OFF + 314);
                  r2 = 0;
                  r3 = 0;
                  r4 = 0;
                  r5 = 0;
                  r6 = 0;
                  r7 = 0;
                  r8 = 0;
                  r9 = 0;
                  r10 = 0;
                  WL_RETN(11);
                }
              } else {
                term_sink(e, _f_6);
                term_sink(e, _x_6);
                term_sink(e, _v_87);
                heap_free(e, cls_fit(2), _sp_2);
                heap_free(e, cls_fit(2), _sp_1);
                r0 = 1;
                r1 = term_ctr(CID_SCON, STAT_OFF + 314);
                r2 = 0;
                r3 = 0;
                r4 = 0;
                r5 = 0;
                r6 = 0;
                r7 = 0;
                r8 = 0;
                r9 = 0;
                r10 = 0;
                WL_RETN(11);
              }
            } else {
              term_sink(e, _f_4);
              term_sink(e, _x_6);
              term_sink(e, _v_87);
              heap_free(e, cls_fit(2), _sp_1);
              r0 = 1;
              r1 = term_ctr(CID_SCON, STAT_OFF + 314);
              r2 = 0;
              r3 = 0;
              r4 = 0;
              r5 = 0;
              r6 = 0;
              r7 = 0;
              r8 = 0;
              r9 = 0;
              r10 = 0;
              WL_RETN(11);
            }
          } else {
            term_sink(e, _v_88);
            term_sink(e, _x_6);
            term_sink(e, _v_87);
            r0 = 1;
            r1 = term_ctr(CID_SCON, STAT_OFF + 314);
            r2 = 0;
            r3 = 0;
            r4 = 0;
            r5 = 0;
            r6 = 0;
            r7 = 0;
            r8 = 0;
            r9 = 0;
            r10 = 0;
            WL_RETN(11);
          }
        } else {
          term_sink(e, _v_88);
          term_sink(e, _x_6);
          term_sink(e, _v_87);
          r0 = 1;
          r1 = term_ctr(CID_SCON, STAT_OFF + 314);
          r2 = 0;
          r3 = 0;
          r4 = 0;
          r5 = 0;
          r6 = 0;
          r7 = 0;
          r8 = 0;
          r9 = 0;
          r10 = 0;
          WL_RETN(11);
        }
      } else {
        term_sink(e, _x_6);
        term_sink(e, _v_0);
        r0 = 1;
        r1 = term_ctr(CID_SCON, STAT_OFF + 406);
        r2 = 0;
        r3 = 0;
        r4 = 0;
        r5 = 0;
        r6 = 0;
        r7 = 0;
        r8 = 0;
        r9 = 0;
        r10 = 0;
        WL_RETN(11);
      }
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_FRAME_K228)
  {
    WL_POPN(8);
    Term _x_8 = STK(0);
    Term _v_24 = STK(1);
    Term _f_2 = STK(2);
    Term _x_9 = STK(3);
    Term _x_10 = STK(4);
    Term _x_11 = STK(5);
    Term _x_12 = STK(6);
    Term _x_13 = STK(7);
    Term _h_0 = r0;
    Term _h_1 = r1;
    WL_OPEN
    Term _pos_0 = nat_chk(e, _v_24 + _f_2);
    if (seq) {
      WL_ROOM(10);
      STK(0) = _x_8;
      STK(1) = _pos_0;
      STK(2) = _x_9;
      STK(3) = _x_10;
      STK(4) = _x_11;
      STK(5) = _x_12;
      STK(6) = _x_13;
      STK(7) = _h_0;
      STK(8) = _h_1;
      STK(9) = FID_FRAME_K229;
      WL_PUSHN(10);
    } else {
      u64 _t_2 = task_node(e, FID_FRAME_K229, WL_CONT, WL_IDX, 1);
      e.mem[_t_2 + 0] = _x_8;
      e.mem[_t_2 + 1] = _pos_0;
      e.mem[_t_2 + 2] = _x_9;
      e.mem[_t_2 + 3] = _x_10;
      e.mem[_t_2 + 4] = _x_11;
      e.mem[_t_2 + 5] = _x_12;
      e.mem[_t_2 + 6] = _x_13;
      e.mem[_t_2 + 7] = _h_0;
      e.mem[_t_2 + 8] = _h_1;
      WL_CONT = term_tsk(FID_FRAME_K229, _t_2);
      WL_IDX = 9;
    }
    if (!DEVICE && !seq && fid_nofk(FID_LIST_LENGTH)) {
      u64 _t_3 = task_node(e, FID_LIST_LENGTH, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _h_1;
      return term_tsk(FID_LIST_LENGTH, _t_3);
    }
    r0 = _h_1;
    WL_JMP(FID_LIST_LENGTH);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_FRAME_K229)
  {
    WL_POPN(9);
    Term _x_14 = STK(0);
    Term _pos_1 = STK(1);
    Term _x_15 = STK(2);
    Term _x_16 = STK(3);
    Term _x_17 = STK(4);
    Term _x_18 = STK(5);
    Term _x_19 = STK(6);
    Term _h_3 = STK(7);
    Term _h_4 = STK(8);
    Term _h_2 = r0;
    WL_OPEN
    Term _v_25 = 0;
    Term _v_26 = 0;
    Term _v_27 = 0;
    Term _v_28 = 0;
    Term _v_29 = 0;
    Term _v_30 = 0;
    Term _o_5[2];
    if (spin_123(e, _o_5, nat_mul(e, 90ull, _h_2)) == 0) {
      return 0;
    }
    _v_29 = _o_5[0];
    _v_30 = _o_5[1];
    _v_27 = _v_29;
    _v_28 = _v_30;
    Term _v_31 = 0;
    Term _v_32 = 0;
    Term _o_6[2];
    if (spin_134(e, _o_6, _h_4, _x_15, _v_27, _v_28) == 0) {
      return 0;
    }
    _v_31 = _o_6[0];
    _v_32 = _o_6[1];
    _v_25 = _v_31;
    _v_26 = _v_32;
    r0 = 3;
    r1 = _v_25;
    r2 = _v_26;
    r3 = _h_3;
    r4 = _x_14;
    r5 = _pos_1;
    r6 = nat_chk(e, _x_15 + 1);
    r7 = _x_16;
    r8 = _x_17;
    r9 = _x_18;
    r10 = _x_19;
    WL_RETN(11);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_FRAME_K230)
  {
    WL_POPN(8);
    Term _x_20 = STK(0);
    Term _v_127 = STK(1);
    Term _x_21 = STK(2);
    Term _x_22 = STK(3);
    Term _x_23 = STK(4);
    Term _x_24 = STK(5);
    Term _x_25 = STK(6);
    Term _v_128 = STK(7);
    Term _h_5 = r0;
    Term _h_6 = r1;
    WL_OPEN
    term_sink(e, _h_5);
    if (seq) {
      WL_ROOM(10);
      STK(0) = _x_20;
      STK(1) = _v_127;
      STK(2) = _x_21;
      STK(3) = _x_22;
      STK(4) = _x_23;
      STK(5) = _x_24;
      STK(6) = _x_25;
      STK(7) = _v_128;
      STK(8) = _h_6;
      STK(9) = FID_FRAME_K231;
      WL_PUSHN(10);
    } else {
      u64 _t_6 = task_node(e, FID_FRAME_K231, WL_CONT, WL_IDX, 1);
      e.mem[_t_6 + 0] = _x_20;
      e.mem[_t_6 + 1] = _v_127;
      e.mem[_t_6 + 2] = _x_21;
      e.mem[_t_6 + 3] = _x_22;
      e.mem[_t_6 + 4] = _x_23;
      e.mem[_t_6 + 5] = _x_24;
      e.mem[_t_6 + 6] = _x_25;
      e.mem[_t_6 + 7] = _v_128;
      e.mem[_t_6 + 8] = _h_6;
      WL_CONT = term_tsk(FID_FRAME_K231, _t_6);
      WL_IDX = 9;
    }
    if (!DEVICE && !seq && fid_nofk(FID_LIST_LENGTH)) {
      u64 _t_7 = task_node(e, FID_LIST_LENGTH, WL_CONT, WL_IDX, 0);
      e.mem[_t_7 + 0] = _h_6;
      return term_tsk(FID_LIST_LENGTH, _t_7);
    }
    r0 = _h_6;
    WL_JMP(FID_LIST_LENGTH);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_FRAME_K231)
  {
    WL_POPN(9);
    Term _x_26 = STK(0);
    Term _v_129 = STK(1);
    Term _x_27 = STK(2);
    Term _x_28 = STK(3);
    Term _x_29 = STK(4);
    Term _x_30 = STK(5);
    Term _x_31 = STK(6);
    Term _v_130 = STK(7);
    Term _h_8 = STK(8);
    Term _h_7 = r0;
    WL_OPEN
    Term _v_131 = 0;
    Term _v_132 = 0;
    Term _v_133 = 0;
    Term _v_134 = 0;
    Term _v_135 = 0;
    Term _v_136 = 0;
    Term _o_21[2];
    if (spin_123(e, _o_21, nat_mul(e, 90ull, _h_7)) == 0) {
      return 0;
    }
    _v_135 = _o_21[0];
    _v_136 = _o_21[1];
    _v_133 = _v_135;
    _v_134 = _v_136;
    Term _v_137 = 0;
    Term _v_138 = 0;
    Term _o_22[2];
    if (spin_124(e, _o_22, _h_8, _x_29, 0, _v_133, _v_134) == 0) {
      return 0;
    }
    _v_137 = _o_22[0];
    _v_138 = _o_22[1];
    _v_131 = _v_137;
    _v_132 = _v_138;
    r0 = 3;
    r1 = _v_131;
    r2 = _v_132;
    r3 = _v_130;
    r4 = _x_26;
    r5 = _v_129;
    r6 = _x_27;
    r7 = _x_28;
    r8 = nat_chk(e, _x_29 + 1);
    r9 = _x_30;
    r10 = _x_31;
    WL_RETN(11);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN)
  {
    Term _fuel_0 = r0;
    u32 _x_0 = r1;
    Term _x_1 = r2;
    Term _x_2 = r3;
    Term _x_3 = r4;
    Term _x_4 = r5;
    Term _x_5 = r6;
    Term _x_6 = r7;
    Term _x_7 = r8;
    Term _x_8 = r9;
    Term _x_9 = r10;
    Term _x_10 = r11;
    WL_OPEN
    if (_fuel_0 == 0) {
      term_sink(e, _x_1);
      term_sink(e, _x_3);
      term_sink(e, _x_7);
      term_sink(e, _x_9);
      r0 = term_clo(FID_RUN_C233, 0);
      WL_RETN(1);
    } else {
      Term _f_0 = (_fuel_0 - 1);
      if (_x_0 == 0) {
        r0 = term_clo(FID_RUN_C234, 0);
        WL_RETN(1);
      } else if (_x_0 == 1) {
        u64 _nd_1 = heap_alloc(e, cls_fit(1));
        e.mem[_nd_1 + 0] = _x_1;
        r0 = term_clo(FID_RUN_C235, _nd_1);
        WL_RETN(1);
      } else if (_x_0 == 2) {
        if (seq) {
          WL_ROOM(2);
          STK(0) = _f_0;
          STK(1) = FID_RUN_K236;
          WL_PUSHN(2);
        } else {
          u64 _t_1 = task_node(e, FID_RUN_K236, WL_CONT, WL_IDX, 1);
          e.mem[_t_1 + 0] = _f_0;
          WL_CONT = term_tsk(FID_RUN_K236, _t_1);
          WL_IDX = 1;
        }
        if (!DEVICE && !seq && fid_nofk(FID_FRAME)) {
          u64 _t_2 = task_node(e, FID_FRAME, WL_CONT, WL_IDX, 0);
          e.mem[_t_2 + 0] = _x_1;
          e.mem[_t_2 + 1] = _x_2;
          e.mem[_t_2 + 2] = _x_3;
          e.mem[_t_2 + 3] = _x_4;
          e.mem[_t_2 + 4] = _x_5;
          e.mem[_t_2 + 5] = _x_6;
          e.mem[_t_2 + 6] = _x_7;
          e.mem[_t_2 + 7] = _x_8;
          return term_tsk(FID_FRAME, _t_2);
        }
        r0 = _x_1;
        r1 = _x_2;
        r2 = _x_3;
        r3 = _x_4;
        r4 = _x_5;
        r5 = _x_6;
        r6 = _x_7;
        r7 = _x_8;
        WL_JMP(FID_FRAME);
      } else {
        u64 _nd_2 = heap_alloc(e, cls_fit(11));
        e.mem[_nd_2 + 0] = _f_0;
        e.mem[_nd_2 + 1] = _x_1;
        e.mem[_nd_2 + 2] = _x_2;
        e.mem[_nd_2 + 3] = _x_3;
        e.mem[_nd_2 + 4] = _x_4;
        e.mem[_nd_2 + 5] = _x_5;
        e.mem[_nd_2 + 6] = _x_6;
        e.mem[_nd_2 + 7] = _x_7;
        e.mem[_nd_2 + 8] = _x_8;
        e.mem[_nd_2 + 9] = _x_9;
        e.mem[_nd_2 + 10] = _x_10;
        r0 = term_clo(FID_RUN_C237, _nd_2);
        WL_RETN(1);
      }
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_C233)
  {
    Term _x_11 = r0;
    WL_OPEN
    Term _v_0 = 0;
    Term _o_0[1];
    if (spin_152(e, _o_0, 2ull, term_ctr(CID_SCON, STAT_OFF + 456), _x_11) == 0) {
      return 0;
    }
    _v_0 = _o_0[0];
    r0 = _v_0;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_C234)
  {
    Term _x_12 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_IO_PURE)) {
      u64 _t_0 = task_node(e, FID_IO_PURE, WL_CONT, WL_IDX, 0);
      e.mem[_t_0 + 0] = term_pak(CID_UNIT, 0);
      e.mem[_t_0 + 1] = _x_12;
      return term_tsk(FID_IO_PURE, _t_0);
    }
    r0 = term_pak(CID_UNIT, 0);
    r1 = _x_12;
    WL_JMP(FID_IO_PURE);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_C235)
  {
    Term _x_14 = r0;
    Term _x_13 = r1;
    WL_OPEN
    Term _v_2 = 0;
    Term _o_1[1];
    if (spin_152(e, _o_1, 2ull, _x_14, _x_13) == 0) {
      return 0;
    }
    _v_2 = _o_1[0];
    r0 = _v_2;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_K236)
  {
    WL_POPN(1);
    Term _f_1 = STK(0);
    u32 _h_0 = r0;
    Term _h_1 = r1;
    Term _h_2 = r2;
    Term _h_3 = r3;
    Term _h_4 = r4;
    Term _h_5 = r5;
    Term _h_6 = r6;
    Term _h_7 = r7;
    Term _h_8 = r8;
    Term _h_9 = r9;
    Term _h_10 = r10;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_RUN)) {
      u64 _t_3 = task_node(e, FID_RUN, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _f_1;
      e.mem[_t_3 + 1] = _h_0;
      e.mem[_t_3 + 2] = _h_1;
      e.mem[_t_3 + 3] = _h_2;
      e.mem[_t_3 + 4] = _h_3;
      e.mem[_t_3 + 5] = _h_4;
      e.mem[_t_3 + 6] = _h_5;
      e.mem[_t_3 + 7] = _h_6;
      e.mem[_t_3 + 8] = _h_7;
      e.mem[_t_3 + 9] = _h_8;
      e.mem[_t_3 + 10] = _h_9;
      e.mem[_t_3 + 11] = _h_10;
      return term_tsk(FID_RUN, _t_3);
    }
    r0 = _f_1;
    r1 = _h_0;
    r2 = _h_1;
    r3 = _h_2;
    r4 = _h_3;
    r5 = _h_4;
    r6 = _h_5;
    r7 = _h_6;
    r8 = _h_7;
    r9 = _h_8;
    r10 = _h_9;
    r11 = _h_10;
    WL_JMP(FID_RUN);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_C237)
  {
    Term _f_2 = r0;
    Term _x_16 = r1;
    Term _x_17 = r2;
    Term _x_18 = r3;
    Term _x_19 = r4;
    Term _x_20 = r5;
    Term _x_21 = r6;
    Term _x_22 = r7;
    Term _x_23 = r8;
    Term _x_24 = r9;
    Term _x_25 = r10;
    Term _x_15 = r11;
    WL_OPEN
    Term _v_3 = 0;
    u64 _nd_3 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_3 + 0] = _x_16;
    e.mem[_nd_3 + 1] = _x_17;
    Term _v_4 = 0;
    Term _o_10[1];
    if (spin_153(e, _o_10, term_clo(FID_STDOUT_WRITE, _nd_3)) == 0) {
      return 0;
    }
    _v_4 = _o_10[0];
    _v_3 = _v_4;
    u64 _nd_7 = heap_alloc(e, cls_fit(9));
    e.mem[_nd_7 + 0] = _f_2;
    e.mem[_nd_7 + 1] = _x_18;
    e.mem[_nd_7 + 2] = _x_19;
    e.mem[_nd_7 + 3] = _x_20;
    e.mem[_nd_7 + 4] = _x_21;
    e.mem[_nd_7 + 5] = _x_22;
    e.mem[_nd_7 + 6] = _x_23;
    e.mem[_nd_7 + 7] = _x_24;
    e.mem[_nd_7 + 8] = _x_25;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_9 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_9 + 0] = _v_3;
      e.mem[_t_9 + 1] = term_clo(FID_RUN_C242, _nd_7);
      e.mem[_t_9 + 2] = _x_15;
      return term_tsk(FID_IO_BIND, _t_9);
    }
    r0 = _v_3;
    r1 = term_clo(FID_RUN_C242, _nd_7);
    r2 = _x_15;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_TRY_C238)
  {
    Term _act_1 = r0;
    Term _x_26 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_5 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_5 + 0] = _act_1;
      e.mem[_t_5 + 1] = term_clo(FID_IO_TRY_C239, 0);
      e.mem[_t_5 + 2] = _x_26;
      return term_tsk(FID_IO_BIND, _t_5);
    }
    r0 = _act_1;
    r1 = term_clo(FID_IO_TRY_C239, 0);
    r2 = _x_26;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_TRY_C239)
  {
    Term _x_27 = r0;
    WL_OPEN
    u32 _o_2 = 0;
    u32 _o_3 = 0;
    Term _o_4 = 0;
    if (term_aux(_x_27) == CID_FAIL) {
      _o_2 = 0;
      u64 _sp_0 = term_loc(_x_27);
      Term _f_3 = e.mem[_sp_0 + 0];
      heap_free(e, cls_fit(1), _sp_0);
      u64 _sp_1 = term_loc(_f_3);
      Term _f_4 = e.mem[_sp_1 + 0];
      Term _f_5 = e.mem[_sp_1 + 1];
      heap_free(e, cls_fit(2), _sp_1);
      _o_3 = _f_4;
      _o_4 = _f_5;
    } else {
      _o_2 = 1;
      u64 _sp_2 = term_loc(_x_27);
      Term _f_6 = e.mem[_sp_2 + 0];
      heap_free(e, cls_fit(1), _sp_2);
    }
    u32 _o_5 = 0;
    Term _o_6 = 0;
    Term _o_7 = 0;
    if (_o_2 == 0) {
      _o_5 = 0;
      _o_6 = _o_3;
      _o_7 = _o_4;
    } else {
      _o_5 = 1;
      _o_6 = term_pak(CID_UNIT, 0);
    }
    Term _v_6 = 0;
    Term _o_9[1];
    if (spin_154(e, _o_9, _o_5, _o_6, _o_7) == 0) {
      return 0;
    }
    _v_6 = _o_9[0];
    r0 = _v_6;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_PASS_C240)
  {
    Term _r_3 = r0;
    Term _x_28 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_IO_PURE)) {
      u64 _t_4 = task_node(e, FID_IO_PURE, WL_CONT, WL_IDX, 0);
      e.mem[_t_4 + 0] = _r_3;
      e.mem[_t_4 + 1] = _x_28;
      return term_tsk(FID_IO_PURE, _t_4);
    }
    r0 = _r_3;
    r1 = _x_28;
    WL_JMP(FID_IO_PURE);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_PASS_C241)
  {
    u32 _r_4 = r0;
    Term _r_5 = r1;
    Term _x_29 = r2;
    WL_OPEN
    Term _v_8 = 0;
    Term _o_8[1];
    if (spin_152(e, _o_8, _r_4, _r_5, _x_29) == 0) {
      return 0;
    }
    _v_8 = _o_8[0];
    r0 = _v_8;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_C242)
  {
    Term _f_7 = r0;
    Term _x_31 = r1;
    Term _x_32 = r2;
    Term _x_33 = r3;
    Term _x_34 = r4;
    Term _x_35 = r5;
    Term _x_36 = r6;
    Term _x_37 = r7;
    Term _x_38 = r8;
    Term _x_30 = r9;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _f_7;
      STK(1) = FID_RUN_K243;
      WL_PUSHN(2);
    } else {
      u64 _t_6 = task_node(e, FID_RUN_K243, WL_CONT, WL_IDX, 1);
      e.mem[_t_6 + 0] = _f_7;
      WL_CONT = term_tsk(FID_RUN_K243, _t_6);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_FRAME)) {
      u64 _t_7 = task_node(e, FID_FRAME, WL_CONT, WL_IDX, 0);
      e.mem[_t_7 + 0] = _x_31;
      e.mem[_t_7 + 1] = _x_32;
      e.mem[_t_7 + 2] = _x_33;
      e.mem[_t_7 + 3] = _x_34;
      e.mem[_t_7 + 4] = _x_35;
      e.mem[_t_7 + 5] = _x_36;
      e.mem[_t_7 + 6] = _x_37;
      e.mem[_t_7 + 7] = _x_38;
      return term_tsk(FID_FRAME, _t_7);
    }
    r0 = _x_31;
    r1 = _x_32;
    r2 = _x_33;
    r3 = _x_34;
    r4 = _x_35;
    r5 = _x_36;
    r6 = _x_37;
    r7 = _x_38;
    WL_JMP(FID_FRAME);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_RUN_K243)
  {
    WL_POPN(1);
    Term _f_8 = STK(0);
    u32 _h_11 = r0;
    Term _h_12 = r1;
    Term _h_13 = r2;
    Term _h_14 = r3;
    Term _h_15 = r4;
    Term _h_16 = r5;
    Term _h_17 = r6;
    Term _h_18 = r7;
    Term _h_19 = r8;
    Term _h_20 = r9;
    Term _h_21 = r10;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_RUN)) {
      u64 _t_8 = task_node(e, FID_RUN, WL_CONT, WL_IDX, 0);
      e.mem[_t_8 + 0] = _f_8;
      e.mem[_t_8 + 1] = _h_11;
      e.mem[_t_8 + 2] = _h_12;
      e.mem[_t_8 + 3] = _h_13;
      e.mem[_t_8 + 4] = _h_14;
      e.mem[_t_8 + 5] = _h_15;
      e.mem[_t_8 + 6] = _h_16;
      e.mem[_t_8 + 7] = _h_17;
      e.mem[_t_8 + 8] = _h_18;
      e.mem[_t_8 + 9] = _h_19;
      e.mem[_t_8 + 10] = _h_20;
      e.mem[_t_8 + 11] = _h_21;
      return term_tsk(FID_RUN, _t_8);
    }
    r0 = _f_8;
    r1 = _h_11;
    r2 = _h_12;
    r3 = _h_13;
    r4 = _h_14;
    r5 = _h_15;
    r6 = _h_16;
    r7 = _h_17;
    r8 = _h_18;
    r9 = _h_19;
    r10 = _h_20;
    r11 = _h_21;
    WL_JMP(FID_RUN);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND)
  {
    Term _m_0 = r0;
    Term _f_0 = r1;
    Term _k_0 = r2;
    WL_OPEN
    u64 _nd_0 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_0 + 0] = _f_0;
    e.mem[_nd_0 + 1] = _k_0;
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_3 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _m_0;
      e.mem[_t_3 + 1] = term_clo(FID_IO_BIND_C255, _nd_0);
      return term_tsk(FID_CLO_APPLY, _t_3);
    }
    r0 = _m_0;
    r1 = term_clo(FID_IO_BIND_C255, _nd_0);
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND_C255)
  {
    Term _f_1 = r0;
    Term _k_1 = r1;
    Term _x_0 = r2;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _k_1;
      STK(1) = FID_IO_BIND_K256;
      WL_PUSHN(2);
    } else {
      u64 _t_0 = task_node(e, FID_IO_BIND_K256, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _k_1;
      WL_CONT = term_tsk(FID_IO_BIND_K256, _t_0);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_1 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = _f_1;
      e.mem[_t_1 + 1] = _x_0;
      return term_tsk(FID_CLO_APPLY, _t_1);
    }
    r0 = _f_1;
    r1 = _x_0;
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND_K256)
  {
    WL_POPN(1);
    Term _k_2 = STK(0);
    Term _h_0 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_2 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = _h_0;
      e.mem[_t_2 + 1] = _k_2;
      return term_tsk(FID_CLO_APPLY, _t_2);
    }
    r0 = _h_0;
    r1 = _k_2;
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN)
  {
    WL_OPEN
    r0 = term_clo(FID_MAIN_C258, 0);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN_C258)
  {
    Term _x_0 = r0;
    WL_OPEN
    Term _v_0 = 0;
    Term _v_1 = 0;
    Term _o_8[1];
    if (spin_157(e, _o_8, term_clo(FID_STDIN_READ, 0)) == 0) {
      return 0;
    }
    _v_1 = _o_8[0];
    _v_0 = _v_1;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_5 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_5 + 0] = _v_0;
      e.mem[_t_5 + 1] = term_clo(FID_MAIN_C263, 0);
      e.mem[_t_5 + 2] = _x_0;
      return term_tsk(FID_IO_BIND, _t_5);
    }
    r0 = _v_0;
    r1 = term_clo(FID_MAIN_C263, 0);
    r2 = _x_0;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_TRY_C259)
  {
    Term _act_1 = r0;
    Term _x_1 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_1 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = _act_1;
      e.mem[_t_1 + 1] = term_clo(FID_IO_TRY_C260, 0);
      e.mem[_t_1 + 2] = _x_1;
      return term_tsk(FID_IO_BIND, _t_1);
    }
    r0 = _act_1;
    r1 = term_clo(FID_IO_TRY_C260, 0);
    r2 = _x_1;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_TRY_C260)
  {
    Term _x_2 = r0;
    WL_OPEN
    u32 _o_0 = 0;
    Term _o_1 = 0;
    Term _o_2 = 0;
    if (term_aux(_x_2) == CID_FAIL) {
      _o_0 = 0;
      u64 _sp_0 = term_loc(_x_2);
      Term _f_0 = e.mem[_sp_0 + 0];
      heap_free(e, cls_fit(1), _sp_0);
      u64 _sp_1 = term_loc(_f_0);
      Term _f_1 = e.mem[_sp_1 + 0];
      Term _f_2 = e.mem[_sp_1 + 1];
      heap_free(e, cls_fit(2), _sp_1);
      _o_1 = _f_1;
      _o_2 = _f_2;
    } else {
      _o_0 = 1;
      u64 _sp_2 = term_loc(_x_2);
      Term _f_3 = e.mem[_sp_2 + 0];
      heap_free(e, cls_fit(1), _sp_2);
      u64 _sp_3 = term_loc(_f_3);
      Term _f_4 = e.mem[_sp_3 + 0];
      Term _f_5 = e.mem[_sp_3 + 1];
      heap_free(e, cls_fit(2), _sp_3);
      _o_1 = _f_4;
      _o_2 = _f_5;
    }
    u32 _o_3 = 0;
    Term _o_4 = 0;
    Term _o_5 = 0;
    if (_o_0 == 0) {
      _o_3 = 0;
      _o_4 = _o_1;
      _o_5 = _o_2;
    } else {
      _o_3 = 1;
      u64 _nd_1 = heap_alloc(e, cls_fit(2));
      e.mem[_nd_1 + 0] = _o_1;
      e.mem[_nd_1 + 1] = _o_2;
      _o_4 = term_ctr(CID_TUPLE, _nd_1);
    }
    Term _v_3 = 0;
    Term _o_7[1];
    if (spin_158(e, _o_7, _o_3, _o_4, _o_5) == 0) {
      return 0;
    }
    _v_3 = _o_7[0];
    r0 = _v_3;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_PASS_C261)
  {
    Term _r_3 = r0;
    Term _x_3 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_IO_PURE)) {
      u64 _t_0 = task_node(e, FID_IO_PURE, WL_CONT, WL_IDX, 0);
      e.mem[_t_0 + 0] = _r_3;
      e.mem[_t_0 + 1] = _x_3;
      return term_tsk(FID_IO_PURE, _t_0);
    }
    r0 = _r_3;
    r1 = _x_3;
    WL_JMP(FID_IO_PURE);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_PASS_C262)
  {
    u32 _r_4 = r0;
    Term _r_5 = r1;
    Term _x_4 = r2;
    WL_OPEN
    Term _v_5 = 0;
    Term _o_6[1];
    if (spin_159(e, _o_6, _r_4, _r_5, _x_4) == 0) {
      return 0;
    }
    _v_5 = _o_6[0];
    r0 = _v_5;
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN_C263)
  {
    Term _x_5 = r0;
    WL_OPEN
    u64 _sp_4 = term_loc(_x_5);
    Term _f_6 = e.mem[_sp_4 + 0];
    Term _f_7 = e.mem[_sp_4 + 1];
    heap_free(e, cls_fit(2), _sp_4);
    Term _fv_0[1];
    _fv_0[0] = 0ull;
    if (seq) {
      WL_ROOM(2);
      STK(0) = _f_7;
      STK(1) = FID_MAIN_K264;
      WL_PUSHN(2);
    } else {
      u64 _t_2 = task_node(e, FID_MAIN_K264, WL_CONT, WL_IDX, 1);
      e.mem[_t_2 + 0] = _f_7;
      WL_CONT = term_tsk(FID_MAIN_K264, _t_2);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_FRAME)) {
      u64 _t_3 = task_node(e, FID_FRAME, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _f_6;
      e.mem[_t_3 + 1] = _f_7;
      e.mem[_t_3 + 2] = 0;
      e.mem[_t_3 + 3] = 0;
      e.mem[_t_3 + 4] = 0;
      e.mem[_t_3 + 5] = 0;
      e.mem[_t_3 + 6] = blk_new(e, 0, 4ull, 0, 1, _fv_0);
      e.mem[_t_3 + 7] = 0;
      return term_tsk(FID_FRAME, _t_3);
    }
    r0 = _f_6;
    r1 = _f_7;
    r2 = 0;
    r3 = 0;
    r4 = 0;
    r5 = 0;
    r6 = blk_new(e, 0, 4ull, 0, 1, _fv_0);
    r7 = 0;
    WL_JMP(FID_FRAME);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN_K264)
  {
    WL_POPN(1);
    Term _f_8 = STK(0);
    u32 _h_0 = r0;
    Term _h_1 = r1;
    Term _h_2 = r2;
    Term _h_3 = r3;
    Term _h_4 = r4;
    Term _h_5 = r5;
    Term _h_6 = r6;
    Term _h_7 = r7;
    Term _h_8 = r8;
    Term _h_9 = r9;
    Term _h_10 = r10;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_RUN)) {
      u64 _t_4 = task_node(e, FID_RUN, WL_CONT, WL_IDX, 0);
      e.mem[_t_4 + 0] = nat_chk(e, _f_8 + 1);
      e.mem[_t_4 + 1] = _h_0;
      e.mem[_t_4 + 2] = _h_1;
      e.mem[_t_4 + 3] = _h_2;
      e.mem[_t_4 + 4] = _h_3;
      e.mem[_t_4 + 5] = _h_4;
      e.mem[_t_4 + 6] = _h_5;
      e.mem[_t_4 + 7] = _h_6;
      e.mem[_t_4 + 8] = _h_7;
      e.mem[_t_4 + 9] = _h_8;
      e.mem[_t_4 + 10] = _h_9;
      e.mem[_t_4 + 11] = _h_10;
      return term_tsk(FID_RUN, _t_4);
    }
    r0 = nat_chk(e, _f_8 + 1);
    r1 = _h_0;
    r2 = _h_1;
    r3 = _h_2;
    r4 = _h_3;
    r5 = _h_4;
    r6 = _h_5;
    r7 = _h_6;
    r8 = _h_7;
    r9 = _h_8;
    r10 = _h_9;
    r11 = _h_10;
    WL_JMP(FID_RUN);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_STDIN_READ)
  {
    Term _k_2 = r0;
    WL_OPEN
    u64 _nd_2 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_2 + 0] = _k_2;
    r0 = term_ctr(CID_STDIN_READ, _nd_2);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_STDOUT_WRITE)
  {
    Term _a_1 = r0;
    Term _n_1 = r1;
    Term _k_3 = r2;
    WL_OPEN
    u64 _nd_3 = heap_alloc(e, cls_fit(3));
    e.mem[_nd_3 + 0] = _a_1;
    e.mem[_nd_3 + 1] = _n_1;
    e.mem[_nd_3 + 2] = _k_3;
    r0 = term_ctr(CID_STDOUT_WRITE, _nd_3);
    WL_RETN(1);
  }}
#endif

  WL_CASE(FID_ENTER)
  {
    Term t = r0;
    WL_OPEN
    u32 f   = (u32)term_aux(t);
    u64 a   = term_loc(t);
    u32 war = fid_arity(f);
    WL_FRAME(t)
    seq |= fid_nofk(f) << 1;
    if (fid_resw(f)) {
      u32 rw = fid_resw(f);
      WL_LOAD(a + war - rw, rw)
      WL_ARGS(a, war - rw + 1)
    } else {
      WL_LOAD(a, war)
    }
    heap_free(e, cls_fit(war + 2), a);
    WL_DYN(f);
  }}

  WL_CASE(FID_IO_EMIT)
  {
    Term x = r0;
    WL_OPEN
    u64 l = heap_alloc(e, 0);
    e.mem[l] = x;
    r0 = term_ctr(CID_EMIT, l);
    WL_RETN(1);
  }}

  WL_CASE(FID_CLO_APPLY)
  {
    Term fun = r0;
    Term arg = r1;
    WL_OPEN
    u32 f    = (u32)term_aux(fun);
    u32 war  = fid_arity(f) - 1;
    u64 a    = term_loc(fun);
    WL_LOAD(a, war)
    spare_free(e, cls_fit(war), a);
    WL_LAST(arg)
    WL_DYN(f);
  }}

  WL_CASE(FID_EXIT)
  {
    u32  n = rn;
    Term rv[WL_RESW];
    WL_SAVE(rv)
    WL_OPEN
    if (err_seen(e.mem)) {
      return 0;
    }
    sp -= 2 * LANE_STEP;
    Term cont = STK(0);
    u32  idx  = (u32)STK(1);
    if (cont != TERM_HOLE && fid_resw((u32)term_aux(cont))) {
      u32 wf = (u32)term_aux(cont);
      u64 wa = term_loc(cont);
      u32 wn = fid_arity(wf);
      WL_FRAME(cont)
      seq = (seq & 1) | fid_nofk(wf) << 1;
      WL_ARGS(wa, wn - n + 1)
      heap_free(e, cls_fit(wn + 2), wa);
      WL_TAKE(rv)
      WL_DYN(wf);
    }
    return task_deliver(e.mem, cont, idx, rv, n);
  }}

#if DEVICE
  default: {
    err_post(e.mem, ERR_FIDS);
    return 0;
  }
  }
  }
}
#endif

// Monk
// ====

// One turn on a ring: its head task below put0 runs (a growing
// lane skips a fork-free one). The host grows a row ring by
// ring and drains a ring; a device lane does both.
INLINE u32 monk_step(Env e, DEV Term* stk, u32 rg, u32 put0, u32 base, u32 stride,
  TG u32* cur) {
  DEV u64* H   = e.mem;
  bool     seq = stride == 0;
  DEV u32* get = ring_get(H, rg);
  if (*get == put0) {
    return 0;
  }
  DEV u32* lo = (DEV u32*)ring_slot(H, rg, *get);
  u32      hi = a32_load_acq(lo + 1);
  Term     t  = (((u64)hi << 32) | a32_load(lo)) & ~RFC_BIT;
  if ((hi >> 31) != ring_lap(*get) || (!seq && fid_nofk((u32)term_aux(t)))) {
    return 0;
  }
  a32_store(get, *get + 1);
  u32 spin = 0;
  for (;;) {
    Term r = work_loop(e, stk, t, seq);
    if (r == 0) {
      return 2;
    }
    if ((u32)H[task_tail(r) + 1] == 0) {
      if (err_spun(H, &spin)) {
        return 2;
      }
      if (stride != 0 && fid_nofk((u32)term_aux(r))) {
        ring_push(H, ring_pick(base, stride, cur), r);
        return 2;
      }
      t      = r;
      seq    = false;
      stride = 0;
      continue;
    }
    task_deal(H, r, base, stride, cur);
    return 1;
  }
}

// Dev
// ===

// One kernel: pass 0 grows the frontier, pass 1 drains each lane's ring,
// pass 2 packs the banks: in one group, each bank's [top, wr) slides onto
// rd, CUBE_T entries a step (loads, barrier, stores: rd <= top), off the
// host's pages. A grow pass ends when its group is full or nothing grew,
// so a spine of forks unrolls whole. TG_HOLD words of threadgroup memory
// hold one group per Apple core (bitonic 1.35x without).

#if DEVICE

INLINE void dev_cut(Env e) {
  if (err_seen(e.mem)) {
    return;
  }
  for (u32 c = 0; c < NCLS_ALL; c += 1) {
    u64 gen = (u64)KEEP(c) << c;
    while (ALC_LEN(e, c) >= gen) {
      u64 head = ALC_AT(e, c);
      u64 tail = head;
      for (u32 i = KEEP(c); --i;) {
        tail = e.mem[tail];
      }
      ALC_AT(e, c)    = e.mem[tail];
      ALC_LEN(e, c)  -= gen;
      e.mem[tail]     = 0;
      bank_push(e.mem, c, head);
    }
  }
}

INLINE void bank_pack(DEV u64* H, u32 lane) {
  for (u32 c = 0; c < NCLS_ALL; c += 1) {
    DEV Bank* b  = bank_at(H, c);
    u32       rd = b->rd;
    u32       n  = b->wr - b->top;
    for (u32 i = 0; i < n; i += CUBE_T) {
      Term v = i + lane < n ? H[b->off + b->top + i + lane] : 0;
      BAR();
      if (i + lane < n) {
        H[b->off + rd + i + lane] = v;
      }
    }
    BAR();
    if (lane == 0) {
      b->rd = b->wr = b->top = rd + n;
    }
  }
}

#ifdef __METAL_VERSION__
kernel void bend_dev(DEV u64* H [[buffer(0)]], constant u32& pass [[buffer(1)]],
  TG u32* vote [[threadgroup(0)]],
  u32 grids [[threadgroups_per_grid]],
  u32 row [[threadgroup_position_in_grid]],
  u32 lane [[thread_position_in_threadgroup]]) {
#else
extern "C" __global__ void bend_dev(DEV u64* H, u32 pass) {
  extern __shared__ u32 vote[];
  u32 grids = gridDim.x;
  u32 row   = blockIdx.x;
  u32 lane  = threadIdx.x;
#endif
  if (pass == 2) {
    bank_pack(H, lane);
    return;
  }
  u32  stride = grids == 1 ? CUBE_G : 1;
  u32  me     = row * CUBE_T + stride * lane;
  u32 rg     = pass ? ring_flip(me) : me;
  Env  e      = { H, H + ALC_OFF + me };
  DEV Term*  stk    = (DEV Term*)(H + STAK_OFF + me);
  if (lane == 0) {
    for (u32 i = 0; i < 3; i += 1) {
      a32_store(vote + i, 0);
    }
  }
  BAR();
  u32 put0      = a32_load(ring_put(H, rg));
  u32 seen_has  = 0;
  u32 seen_grew = 0;
  for (;;) {
    if (pass) {
      if (*ring_get(H, rg) == put0 || err_seen(H)) {
        break;
      }
    } else {
      put0 = a32_load(ring_put(H, rg));
      u32 has = put0 != a32_load(ring_get(H, rg));
      if (lane == 0 && (err_seen(H) || root_done(H))) {
        has = CUBE_T;
      }
      a32_add(vote + 2, has);
      BAR();
      has = a32_load(vote + 2);
      if (has - seen_has >= CUBE_T) {
        break;
      }
      seen_has = has;
    }
    u32 ran = monk_step(e, stk, rg, put0, row * CUBE_T, pass ? 0 : stride,
      vote);
    if (!pass) {
      if (ran == 1) {
        a32_add(vote + 1, 1);
      }
      BARD();
      u32 grew = a32_load(vote + 1);
      if (grew == seen_grew) {
        break;
      }
      seen_grew = grew;
    }
  }
  dev_cut(e);
}

#endif

// Window
// ======

// Linux's window fill (the Mac's is window_msl): an Image is a quadtree over
// 2^k x 2^k (Qua splits tl, tr, bl, br; Pix is 0xRRGGBB).
#if defined(__linux__) || defined(BEND_RTC)

INLINE u32 window_pix(DEV u64* H, Term t, u32 k, u32 x, u32 y) {
  for (u32 i = k; term_tag(t) == TAG_CTR;) {
    u32 j = 0;
    if (i > 0) {
      i -= 1;
      j = ((y >> i) & 1) * 2 + ((x >> i) & 1);
    }
    t = H[term_peek(H, t) + j];
  }
  return (u32)term_loc(t) & 0xFFFFFF;
}

#ifdef BEND_RTC
extern "C" __global__ void window_dev(DEV u64* H, Term root, u32 w, u32 h,
  u32 k, u32* out) {
  u32 x = blockIdx.x * blockDim.x + threadIdx.x;
  u32 y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < w && y < h) {
    out[y * w + x] = window_pix(H, root, k, x, y);
  }
}
#endif

#endif

#if !DEVICE

// Row
// ===

static void row_grow(Env e, DEV Term* stk, u32 base, u32 stride, u32 want) {
  u64* H = e.mem;
  u32 cur = 0;
  for (;;) {
    u32 put0[CUBE_T];
    u32 has = 0;
    for (u32 i = 0; i < CUBE_T; i += 1) {
      put0[i] = *ring_put(H, base + i * stride);
      has += put0[i] != *ring_get(H, base + i * stride);
    }
    if (root_done(H) || has >= want) {
      return;
    }
    u32 grew = 0;
    u32 ran  = 0;
    for (u32 i = 0; i < CUBE_T && ran != 2; i += 1) {
      ran   = monk_step(e, stk, base + i * stride, put0[i], base, stride,
        &cur);
      grew += ran == 1;
    }
    if (grew == 0) {
      return;
    }
  }
}

// Pool
// ====

// cpu_count caps the CPU count by the affinity mask and the cgroup quota.

static void* pool_try(void* at, u64 bytes) {
  return mmap(at, bytes, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
}

static void* pool_mmap(u64 bytes) {
  void* p = pool_try(NULL, bytes);
  if (p == MAP_FAILED) {
    err_fail("reservation failed");
  }
  return p;
}

static Term* pool_stack(void) {
  u64   len = 1ull << 31;
  char* p   = pool_mmap(len + 16384 + SIGSTKSZ);
  if (mprotect(p + len, 16384, PROT_NONE) != 0) {
    err_fail("stack guard failed");
  }
  stack_t ss = { .ss_sp = p + len + 16384, .ss_size = SIGSTKSZ };
  sigaltstack(&ss, NULL);
  struct sigaction sa = { .sa_handler = err_trap, .sa_flags = SA_ONSTACK };
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  return (Term*)p;
}

static void* pool_work(void* arg) {
  Term* stk  = pool_stack();
  u32   seen = 0;
  for (;;) {
    pthread_mutex_lock(&pool_lock);
    while (pool_tick == seen) {
      pthread_cond_wait(&pool_wake, &pool_lock);
    }
    seen = pool_tick;
    pthread_mutex_unlock(&pool_lock);
    Env e = { CORPUS, ALC[1 + (u32)(uintptr_t)arg] };
    for (;;) {
      u32 r = a32_add(&pool_row, 1);
      if (r >= (pool_grow ? CUBE_G : LANES / LINE)) {
        break;
      }
      if (pool_grow) {
        row_grow(e, stk, r * CUBE_T, 1, CUBE_T);
      } else {
        u32  step = CUBE_T / LINE;
        u32 row  = r / step * CUBE_T;
        for (u32 rg = row + r % step; rg < row + CUBE_T; rg += step) {
          u32 put0 = a32_load(ring_put(e.mem, rg));
          while (*ring_get(e.mem, rg) != put0 && !err_seen(e.mem)) {
            monk_step(e, stk, rg, put0, rg, 0, NULL);
          }
        }
      }
    }
    if (a32_sub_rel(&pool_done, 1) == 1) {
      pthread_mutex_lock(&pool_lock);
      pthread_cond_broadcast(&pool_wake);
      pthread_mutex_unlock(&pool_lock);
    }
  }
}

OUTLINE void pool_open(void) {
  static bool up;
  if (up) {
    return;
  }
  up = true;
  for (u32 w = 0; w < pool_size; w += 1) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, pool_work, (void*)(uintptr_t)w)) {
      err_fail("pthread_create");
    }
  }
}

static int cpu_read(const char* path, long* a, long* b) {
  FILE* f = fopen(path, "r");
  int   n = f == NULL ? 0 : fscanf(f, "%ld %ld", a, b);
  if (f != NULL) {
    fclose(f);
  }
  return n;
}

static long cpu_count(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
#ifdef __linux__
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof set, &set) == 0) {
    n = CPU_COUNT(&set);
  }
  long q = 0;
  long p = 0;
  if (cpu_read("/sys/fs/cgroup/cpu.max", &q, &p) != 2) {
    cpu_read("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", &q, &p);
    cpu_read("/sys/fs/cgroup/cpu/cpu.cfs_period_us", &p, &p);
  }
  if (q > 0 && p > 0 && (q + p - 1) / p < n) {
    n = (q + p - 1) / p;
  }
#endif
  return n;
}

OUTLINE void pool_turn(bool grow) {
  pool_grow = grow;
  a32_store(&pool_row, 0);
  a32_store(&pool_done, pool_size);
  pthread_mutex_lock(&pool_lock);
  pool_tick += 1;
  pthread_cond_broadcast(&pool_wake);
  while (a32_load_acq(&pool_done) != 0) {
    pthread_cond_wait(&pool_wake, &pool_lock);
  }
  pthread_mutex_unlock(&pool_lock);
}

// Gpu
// ===

// gpu_make compiles the device program into <binary>.gpu
// (--gpu-build): Metal's binary archive, or CUDA's cubin behind a
// hash of the text. A launch loads it, else notes and compiles. CUDA
// shapes the bag by the device: a group of 128 lanes per 64 KB of
// L2, a power of two in 16..128 (Apple keeps the tuned 128). CUDA
// runs one stream: the default 8 cost about half of the startup.

static const char* gpu_path(void) {
  static char path[4096];
  u32 n = sizeof path - 8;
#ifdef __APPLE__
  _NSGetExecutablePath(path, &n);
#else
  path[readlink("/proc/self/exe", path, n)] = 0;
#endif
  return strcat(path, ".gpu");
}

static void gpu_note(const char* path) {
  fprintf(stderr, "bend: compiling the GPU program (%s is missing or"
    " stale)\n", path);
}

#if !BEND_CUDA
#define gpu_map pool_mmap
#endif

#if BEND_METAL || BEND_CUDA

static void gpu_kernel(u32 pass, u32 groups);

static void gpu_run(u32 f) {
  if (f < CUBE_T) {
    gpu_kernel(0, 1);
  }
  if (f < LANES) {
    gpu_kernel(0, CUBE_G);
  }
  gpu_kernel(1, CUBE_G);
  gpu_kernel(2, 1);
}

#endif

#if BEND_CUDA

static u64 gpu_hash(void) {
  u64 key = 14695981039346656037ull ^ CUBE_LOG;
  for (const char* p = BEND_SRC; *p != 0; p += 1) {
    key = (key ^ (u8)*p) * 1099511628211ull;
  }
  return key;
}

#endif

#if BEND_METAL

static void gpu_fail(NSError* err) {
  err_fail([[err localizedDescription] UTF8String]);
}

static bool gpu_probe(void) {
  return (gpu_dev = MTLCreateSystemDefaultDevice()) != nil;
}

static MTLComputePipelineDescriptor* gpu_desc(void) {
  NSError* err = nil;
  MTLCompileOptions* opts = [MTLCompileOptions new];
  opts.mathMode = MTLMathModeSafe;
  opts.preprocessorMacros = @{ @"CUBE_LOG": @(CUBE_LOG) };
  id<MTLLibrary> lib = [gpu_dev newLibraryWithSource:@(BEND_SRC) options:opts
    error:&err];
  if (!lib) {
    gpu_fail(err);
  }
  MTLComputePipelineDescriptor* d = [MTLComputePipelineDescriptor new];
  d.computeFunction = [lib newFunctionWithName:@"bend_dev"];
  return d;
}

static bool gpu_make(const char* path) {
  NSError* err = nil;
  id<MTLBinaryArchive> ar = [gpu_dev
    newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&err];
  if (![ar addComputePipelineFunctionsWithDescriptor:gpu_desc() error:&err]) {
    gpu_fail(err);
  }
  return [ar serializeToURL:[NSURL fileURLWithPath:@(path)] error:&err];
}

static id<MTLComputePipelineState> gpu_pipe(MTLComputePipelineDescriptor* d,
  id<MTLBinaryArchive> ar) {
  NSError* err = nil;
  d.binaryArchives = ar ? @[ar] : @[];
  id<MTLComputePipelineState> pso = [gpu_dev
    newComputePipelineStateWithDescriptor:d
    options:ar ? MTLPipelineOptionFailOnBinaryArchiveMiss : 0 reflection:nil
    error:&err];
  if (!pso && !ar) {
    gpu_fail(err);
  }
  return pso;
}

static u64 gpu_span(void) {
  u64 span = [gpu_dev recommendedMaxWorkingSetSize];
  u64 most = [gpu_dev maxBufferLength];
  span = span < most ? span : most;
  return span < (2ull << 30) ? span : 2ull << 30;
}

static void gpu_load(u64 bytes) {
  gpu_buf = [gpu_dev newBufferWithBytesNoCopy:CORPUS length:bytes
    options:MTLResourceStorageModeShared
      | MTLResourceHazardTrackingModeUntracked deallocator:nil];
  u64 most = [gpu_dev maxBufferLength];
  if (!gpu_buf && bytes > most) {
    char msg[96];
    snprintf(msg, sizeof msg, "--gpu %lluMB is over the device's %lluMB",
      (unsigned long long)(bytes >> 20), (unsigned long long)(most >> 20));
    err_fail(msg);
  }
  if (!gpu_buf) {
    err_fail("the GPU span is more than the device has");
  }
  @autoreleasepool {
    gpu_que = [gpu_dev newCommandQueue];
    const char* path = gpu_path();
    MTLBinaryArchiveDescriptor* ad = [MTLBinaryArchiveDescriptor new];
    ad.url = [NSURL fileURLWithPath:@(path)];
    MTLComputePipelineDescriptor* d = gpu_desc();
    id<MTLBinaryArchive> ar = [gpu_dev newBinaryArchiveWithDescriptor:ad
      error:nil];
    gpu_pso = ar ? gpu_pipe(d, ar) : nil;
    if (!gpu_pso) {
      gpu_note(path);
      gpu_pso = gpu_pipe(d, nil);
    }
  }
}

static void gpu_kernel(u32 pass, u32 groups) {
  [gpu_enc setComputePipelineState:gpu_pso];
  [gpu_enc setBuffer:gpu_buf offset:0 atIndex:0];
  [gpu_enc setBytes:&pass length:sizeof pass atIndex:1];
  [gpu_enc setThreadgroupMemoryLength:TG_HOLD * 8 atIndex:0];
  [gpu_enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
    threadsPerThreadgroup:MTLSizeMake(CUBE_T, 1, 1)];
  [gpu_enc memoryBarrierWithScope:MTLBarrierScopeBuffers];
}

static void gpu_pass(u32 f) {
  @autoreleasepool {
    id<MTLCommandBuffer> cb = [gpu_que commandBuffer];
    gpu_enc = [cb computeCommandEncoder];
    gpu_run(f);
    [gpu_enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if ([cb error]) {
      gpu_fail([cb error]);
    }
  }
}

#elif BEND_CUDA

static void gpu_shape(int units) {
  CUBE_LOG = 31 - CLZ(units < 16 ? 16 : units > 128 ? 128 : units);
}

static bool gpu_probe(void) {
  int       managed = 0;
  CUcontext ctx;
  setenv("CUDA_DEVICE_MAX_CONNECTIONS", "1", 0);
  if (cuInit(0) == CUDA_SUCCESS && cuDeviceGet(&gpu_dev, 0) == CUDA_SUCCESS) {
    cuDeviceGetAttribute(&managed,
      CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS, gpu_dev);
  }
  int l2 = 1 << 23;
  cuDeviceGetAttribute(&l2, CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE, gpu_dev);
  gpu_shape(l2 >> 16);
  return managed != 0
    && cuDevicePrimaryCtxRetain(&ctx, gpu_dev) == CUDA_SUCCESS
    && cuCtxSetCurrent(ctx) == CUDA_SUCCESS;
}

static u64* gpu_map(u64 bytes) {
  CUdeviceptr p = 0;
  if (cuMemAllocManaged(&p, bytes, CU_MEM_ATTACH_GLOBAL) != CUDA_SUCCESS) {
    err_fail("corpus reservation failed");
  }
#if CUDA_VERSION >= 13000
  cuMemAdvise(p, bytes, CU_MEM_ADVISE_SET_PREFERRED_LOCATION,
    (CUmemLocation){ CU_MEM_LOCATION_TYPE_DEVICE, gpu_dev });
#else
  cuMemAdvise(p, bytes, CU_MEM_ADVISE_SET_PREFERRED_LOCATION, gpu_dev);
#endif
  return (u64*)(uintptr_t)p;
}

static bool gpu_make(const char* path) {
  int cc[2] = {0, 0};
  cuDeviceGetAttribute(cc,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, gpu_dev);
  cuDeviceGetAttribute(cc + 1,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, gpu_dev);
  char arch[40];
  char bag[24];
  snprintf(arch, sizeof arch, "--gpu-architecture=sm_%d%d", cc[0], cc[1]);
  snprintf(bag, sizeof bag, "-DCUBE_LOG=%u", CUBE_LOG);
  const char* opts[] = { arch, bag, "--fmad=false", "-default-device" };
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, BEND_SRC, "bend.cu", 0, NULL, NULL)
    != NVRTC_SUCCESS) {
    err_fail("cannot compile the CUDA library");
  }
  if (nvrtcCompileProgram(prog, 4, opts) != NVRTC_SUCCESS) {
    size_t n = 0;
    nvrtcGetProgramLogSize(prog, &n);
    char* log = calloc(n + 1, 1);
    if (log != NULL && nvrtcGetProgramLog(prog, log) == NVRTC_SUCCESS) {
      fprintf(stderr, "%s\n", log);
    }
    err_fail("cannot compile the CUDA library");
  }
  size_t len = 0;
  nvrtcGetCUBINSize(prog, &len);
  char* bin = malloc(len);
  if (bin == NULL || nvrtcGetCUBIN(prog, bin) != NVRTC_SUCCESS) {
    err_fail("cannot load the CUDA library");
  }
  nvrtcDestroyProgram(&prog);
  u64   key = gpu_hash();
  FILE* out = path == NULL ? NULL : fopen(path, "wb");
  bool  ok  = out != NULL && fwrite(&key, 8, 1, out) == 1
    && fwrite(bin, 1, len, out) == len && fclose(out) == 0;
  if (cuModuleLoadData(&gpu_lib, bin) != CUDA_SUCCESS) {
    err_fail("cannot load the CUDA library");
  }
  free(bin);
  return path == NULL || ok;
}

static u64 gpu_span(void) {
  size_t span = 0;
  cuDeviceTotalMem(&span, gpu_dev);
  return span;
}

static void gpu_load(u64 bytes) {
  const char* path = gpu_path();
  int         fd   = open(path, O_RDONLY);
  struct stat st   = { 0 };
  u64         key  = 0;
  char*       bin  = fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 8 ? NULL
    : mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (bin != NULL && bin != MAP_FAILED) {
    memcpy(&key, bin, 8);
  }
  if (key != gpu_hash()
    || cuModuleLoadData(&gpu_lib, bin + 8) != CUDA_SUCCESS) {
    gpu_note(path);
    gpu_make(path);
  }
  if (cuModuleGetFunction(&gpu_pso, gpu_lib, "bend_dev") != CUDA_SUCCESS) {
    err_fail("cannot load the GPU program");
  }
}

static void gpu_kernel(u32 pass, u32 groups) {
  void* args[] = { &CORPUS, &pass };
  if (cuLaunchKernel(gpu_pso, groups, 1, 1, CUBE_T, 1, 1, TG_HOLD * 8, NULL,
    args, NULL) != CUDA_SUCCESS) {
    err_fail("device launch failed");
  }
}

static void gpu_pass(u32 f) {
  gpu_run(f);
  if (cuCtxSynchronize() != CUDA_SUCCESS) {
    err_fail("device fault");
  }
}

#else

#define gpu_probe() false
#define gpu_make(p) true
#define gpu_span()  0
#define gpu_load(b)
#define gpu_pass(f)

#endif

// Cube
// ====

// Under a unit (CUBE_T / LINE a row) per thread, the host's column grows
// to the rows that give one, no more: each touches a page of every plane.

static void cube_run(u64* H, bool gpu) {
  for (;;) {
    u32 f = a32_exch(a32_at(H, H_CURSOR), 0);
    if (root_done(H)) {
      return;
    }
    if (f == 0) {
      err_fail("frontier drained without a result");
    }
    if (gpu) {
      gpu_pass(f);
    } else {
      if (f * (CUBE_T / LINE) < pool_size) {
        row_grow((Env){ H, ALC[0] }, io_stk, 0, CUBE_G,
          (pool_size + CUBE_T / LINE - 1) / (CUBE_T / LINE));
      }
      if (f < CUBE) {
        pool_turn(true);
      }
      pool_turn(false);
    }
    u32 ec = a32_load(a32_at(H, H_ERROR_CODE));
    if (ec != 0) {
      err_post(H, ec);
    }
  }
}

// Corpus
// ======

// The cores map 8 GiB at a high base and double it in place, so one
// base holds every location; the banks move up past the pages. The GPU maps
// its whole span at once, and never grows it.

static u64 corpus_size;

static void* corpus_map(u64 size) {
  u64   hint = 1ull << 45;
  void* p    = pool_try((void*)hint, size);
  while (p != (void*)hint && hint > size) {
    if (p != MAP_FAILED) {
      munmap(p, size);
    }
    hint /= 2;
    p     = pool_try((void*)hint, size);
  }
  if (p == MAP_FAILED) {
    err_fail("reservation failed");
  }
  return p;
}

static void corpus_lay(u64* H, u64 size) {
  u64 span = size / 8;
  u64 cap  = span > HEAP_OFF ? (span - HEAP_OFF) / (PAGE_LEN + 10) : 0;
  if (cap <= CUBE) {
    err_fail("the GPU span is under the rings, stacks and a page per lane");
  }
  cap = cap < ~0u ? cap : ~0u - 1;
  u64 at = HEAP_OFF + (cap << PAGE_BITS);
  for (u32 c = 0; c < NCLS_ALL; c += 1) {
    Bank* b = bank_at(H, c);
    memcpy(H + at, H + b->off, b->wr * sizeof(u64));
    b->off  = at;
    at     += 2 * (cap >> ((c < NCLS ? NCLS : c) - PAGE_BITS));
  }
  corpus_size = size;
  a32_store_rel(a32_at(H, H_CAP), (u32)cap);
}

static bool corpus_grow(u64* H, u64 need) {
  bool ok = true;
  LOCK(bank_lock);
  while (ok && need > a32_load(a32_at(H, H_CAP))) {
    u64   more = corpus_size;
    char* at   = (char*)H + more;
    void* got  = io_gpu || more >= 1ull << 43 ? MAP_FAILED
      : pool_try(at, more);
    ok = got == at;
    if (ok) {
      corpus_lay(H, more * 2);
    } else if (got != MAP_FAILED) {
      munmap(got, more);
    }
  }
  UNLOCK(bank_lock);
  return ok;
}

static u64* corpus_setup(bool gpu, long threads, u64 bytes) {
  io_gpu     = gpu;
  KEEP_WORDS = gpu ? CHUNK : CAP_WORDS;
  u64 dflt   = gpu ? gpu_span() : 1ull << 33;
  u64 size   = (gpu && bytes != 0 ? bytes : dflt) & ~16383ull;
  CORPUS     = gpu ? gpu_map(size) : corpus_map(size);
  u64* H     = CORPUS;
#if BEND_CUDA
  if (gpu) {
    cuMemsetD8((CUdeviceptr)(uintptr_t)H, 0, STAK_OFF * 8);
    cuCtxSynchronize();
  }
#endif
  corpus_lay(H, size);
  memcpy(H + STAT_OFF, STAT_IMG, STAT_LEN * sizeof(u64));
  a32_store(a32_at(H, H_BUMP), 1);
  if (gpu) {
    gpu_load(size);
  }
  pool_size = threads < 1 ? 1 : threads < CUBE_T ? threads : CUBE_T;
  return H;
}

OUTLINE Term corpus_eval(u64* H, Term t) {
  Env  e = { H, ALC[0] };
  Term rv[WL_RESW];
  for (;;) {
    Term r = work_loop(e, io_stk, t, !BANGS && pool_size == 1);
    if (r == 0) {
      if (root_done(H)) {
        break;
      }
      err_fail("solo delivery lost");
    }
    if ((u32)H[task_tail(r) + 1] == 0) {
      t = r;
      if (io_gpu && fid_bangs((u32)term_aux(t))) {
        u64  tl   = task_tail(t);
        Term cont = H[tl];
        u32  idx  = (u32)(H[tl + 1] >> 32) & 0xFFFF;
        H[tl]     = TERM_HOLE;
        a32_store(a32_at(H, H_CURSOR), 1);
        ring_push(H, 0, t);
        cube_run(H, true);
        Term p = task_deliver(H, cont, idx, rv, root_take(H, rv));
        if (root_done(H)) {
          break;
        }
        if (p == 0) {
          err_fail("seam delivery lost");
        }
        t = p;
      }
      continue;
    }
    task_deal(H, r, 0, 0, NULL);
    pool_open();
    cube_run(H, false);
    break;
  }
  root_take(H, rv);
  return rv[0];
}

// Io
// ==

// Base's opaque, linear handles pack host fds or pointers into aux and loc:
// no forging, copying, reuse or host wrapper. A request's cont applied to
// its item is the next request. A parked request keeps its fd, deadline and
// readiness in word, time and evts; the loop then calls pack: a value
// resumes, IO_PARK parks again. The edge is UTF-8, decoded as WHATWG does: a
// broken sequence yields one U+FFFD and its breaking byte is read again as a
// lead. inet_aton reads a leading zero as octal, so io_sys_addr refuses it.
// macOS poll misses FIFO EOF, so io_wait selects, its sets sized to the
// highest fd (_DARWIN_UNLIMITED_SELECT allows fds past FD_SETSIZE).

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define IO_READ 1
#define IO_TIME 2
#define IO_PARK TERM_HOLE

#define io_hand(v)   term_make(TAG_PAK, (u64)(v) >> 40, (u64)(v) & LOC_MASK)
#define io_hand_v(t) (((u64)term_aux(t) << 40) | term_loc(t))

struct IoWork;
typedef void (*IoCall)(struct IoWork* w);
typedef Term (*IoPack)(Env e, struct IoWork* w);

typedef struct IoWork {
  intptr_t       hand;
  intptr_t       made;
  u32            word;
  u64            size;
  char*          data;
  char*          text;
  u32            code;
  IoCall         call;
  IoPack         pack;
  Term           cont;
  Term           item;
  u64            time;
  short          evts;
  struct IoWork* next;
} IoWork;

typedef Term (*Effect)(Env e, Term* f, IoWork* w);

typedef struct {
  Effect run;
  u32    ask;
} IoEff;

static IoEff io_eff_rows[1 << 16];
static u32   io_live;

static u64 io_tick(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

OUTLINE void* io_mem(void* mem) {
  if (mem == NULL) {
    err_fail("host allocation failed");
  }
  return mem;
}

static int io_sys_addr(const char* host, u32 port, struct sockaddr_in* at) {
  memset(at, 0, sizeof(*at));
  at->sin_family = AF_INET;
  at->sin_port   = htons((uint16_t)port);
  for (const char* p = host; *p != 0; p += 1) {
    if ((p == host || p[-1] == '.') && *p == '0'
      && p[1] >= '0' && p[1] <= '9') {
      return -1;
    }
  }
  return port > 65535 || inet_pton(AF_INET, host, &at->sin_addr) != 1
    ? -1 : 0;
}

static int    io_argc;
static char** io_argv;

static void io_eff(u32 cid, Effect run, u32 need) {
  if (io_eff_rows[cid].run != NULL) {
    err_fail("two effects register one request");
  }
  io_eff_rows[cid] = (IoEff){ run, need };
}

static u64 io_sys_end(IoWork* w, ssize_t n) {
  w->code = n < 0 ? (u32)errno : 0;
  return n < 0 ? 0 : (u64)n;
}

static IoWork* io_runs;
static IoWork* io_park;
static IoWork* io_jobs;

static void io_push(IoWork** q, IoWork* a) {
  IoWork* l = *q != NULL ? *q : a;
  a->next = l->next;
  l->next = a;
  *q      = a;
}

static IoWork* io_pop(IoWork** q) {
  IoWork* a  = (*q)->next;
  (*q)->next = a->next;
  *q         = a != *q ? *q : NULL;
  return a;
}

static void io_spawn(Term m) {
  IoWork* a = io_mem(calloc(1, sizeof(IoWork)));
  a->cont  = m;
  a->item  = term_clo(FID_IO_EMIT, 0);
  io_push(&io_runs, a);
  io_live += 1;
}

// io_park stays in deadline order (time 0, none, sorts last; ties keep
// their park order), so io_wait wakes due timers in the order they expire.
static void io_park_add(IoWork* w) {
  IoWork* p = io_park;
  if (p == NULL || p->time - 1 <= w->time - 1) {
    io_push(&io_park, w);
    return;
  }
  while (p->next->time - 1 <= w->time - 1) {
    p = p->next;
  }
  w->next = p->next;
  p->next = w;
}

static Term io_wait_on(IoWork* w, int fd, short evts, u64 time, IoPack more) {
  w->word = (u32)fd;
  w->pack = more;
  w->time = time;
  w->evts = evts;
  io_park_add(w);
  return IO_PARK;
}

OUTLINE void io_out(FILE* h, const char* data, u64 len) {
  if (fwrite(data, 1, len, h) != len) {
    err_fail("a short write on a standard stream");
  }
}

OUTLINE void io_sync(void) {
  if (fflush(stdout) != 0) {
    err_fail("a short write on a standard stream");
  }
}

static u64 io_utf8(char* buf, u64 c) {
  u64 k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
  for (u64 i = k; i > 1; i -= 1) {
    buf[i - 1] = (char)(0x80 | (c & 0x3F));
    c >>= 6;
  }
  buf[0] = (char)(k == 1 ? c : (0xF00 >> k) | c);
  return k;
}

// io_cbuf writes a String (cons SCon) as UTF-8, or a List (cons Con) as
// its bytes, with no UTF-8: NULL if a value is past 255.
OUTLINE char* io_cbuf(Env e, Term s, u64* len, u64 cons) {
  u64   cap = 64;
  u64   n   = 0;
  u64   bad = 0;
  char* buf = io_mem(malloc(cap));
  while (term_aux(s) == cons) {
    Term fb[2];
    spare_free(e, cls_fit(2), ctr_take(e, s, 2, fb));
    if (n + 5 > cap) {
      cap *= 2;
      buf = io_mem(realloc(buf, cap));
    }
    if (cons == CID_SCON) {
      n += io_utf8(buf + n, fb[0]);
    } else {
      bad |= fb[0] > 255;
      buf[n++] = (char)fb[0];
    }
    s = fb[1];
  }
  buf[n] = 0;
  *len = n;
  if (bad) {
    free(buf);
    return NULL;
  }
  return buf;
}

#define io_cstr(e, s, len) io_cbuf(e, s, len, CID_SCON)

OUTLINE void io_errs(Env e, Term s) {
  u64   n    = 0;
  char* text = io_cstr(e, s, &n);
  io_sync();
  io_out(stderr, text, n);
  io_out(stderr, "\n", 1);
  free(text);
}

#define io_nul(s, n) (strlen(s) != (n))

#define io_seal(e, t, cid) (cid_hot(cid) ? rfc_seal(e, t) : (t))

static Term io_node(Env e, u64 cid, Term a, Term b) {
  u64 l = heap_alloc(e, 1);
  e.mem[l]     = io_seal(e, a, cid);
  e.mem[l + 1] = io_seal(e, b, cid);
  return term_ctr(cid, l);
}

static Term io_str(Env e, const char* p, u64 n) {
  Term s    = term_pak(CID_SNIL, 0);
  u64  hole = 0;
  u64  c = 0, need = 0, lo = 0x80, hi = 0xBF;
  for (u64 i = 0; i < n || need > 0; i += 1) {
    u64 b = i < n ? (uint8_t)p[i] : 0x100;
    if (need > 0 && (b < lo || b > hi)) {
      need = 0;
      c    = 0xFFFD;
      i   -= 1;
    } else if (need > 0) {
      lo = 0x80;
      hi = 0xBF;
      c  = (c << 6) | (b & 0x3F);
      if (--need > 0) {
        continue;
      }
    } else if (b < 0x80) {
      c = b;
    } else if (b < 0xC2 || b > 0xF4) {
      c = 0xFFFD;
    } else {
      need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
      lo   = b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80;
      hi   = b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF;
      c    = b & (0x3F >> need);
      continue;
    }
    u64  l = heap_alloc(e, 1);
    Term t = term_ctr(CID_SCON, l);
    e.mem[l] = c;
    if (hole == 0) {
      s = t;
    } else {
      e.mem[hole] = io_seal(e, t, CID_SCON);
    }
    hole = l + 1;
  }
  if (hole != 0) {
    e.mem[hole] = io_seal(e, term_pak(CID_SNIL, 0), CID_SCON);
  }
  return s;
}

// Bytes cross as they are (0..255), one List cell each, with no UTF-8.
#ifdef CID_CON

static Term io_list(Env e, const char* p, u64 n) {
  Term xs = term_pak(CID_NIL, 0);
  for (u64 i = n; i > 0; i -= 1) {
    xs = io_node(e, CID_CON, (uint8_t)p[i - 1], xs);
  }
  return xs;
}

#endif

#define io_tup(e, a, b) io_node(e, CID_TUPLE, a, b)
#define io_done(e, v)   io_box(e, CID_DONE, v)

static Term io_box(Env e, u64 cid, Term v) {
  u64 l = heap_alloc(e, 0);
  e.mem[l] = io_seal(e, v, cid);
  return term_ctr(cid, l);
}

static Term io_fail(Env e, u32 code, const char* text) {
  const char* s = text != NULL ? text : strerror((int)code);
  Term t = io_tup(e, code, io_str(e, s, strlen(s)));
  return io_box(e, CID_FAIL, t);
}

static pthread_mutex_t io_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  io_bell = PTHREAD_COND_INITIALIZER;
static u32             io_busy;
static u32             io_size;
static int             io_wake_fd[2];

static void io_take(Env e) {
  IoWork* acts[64];
  ssize_t n;
  while ((n = read(io_wake_fd[0], acts, sizeof acts)) > 0) {
    for (u32 i = 0; i < (u32)n / sizeof(IoWork*); i += 1) {
      IoWork* a = acts[i];
      a->item   = a->pack(e, a);
      io_push(&io_runs, a);
      io_busy -= 1;
    }
  }
}

static void* io_help(void* arg) {
  for (;;) {
    pthread_mutex_lock(&io_gate);
    while (io_jobs == NULL) {
      pthread_cond_wait(&io_bell, &io_gate);
    }
    IoWork* a = io_pop(&io_jobs);
    pthread_mutex_unlock(&io_gate);
    a->call(a);
    while (write(io_wake_fd[1], &a, sizeof a) != sizeof a) {
    }
  }
}

static Term io_work(IoWork* w, IoCall call, IoPack pack) {
  w->call  = call;
  w->pack  = pack;
  io_busy += 1;
  if (io_busy > io_size && io_size < IO_HELP) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, io_help, NULL)) {
      err_fail("pthread_create");
    }
    pthread_detach(tid);
    io_size += 1;
  }
  pthread_mutex_lock(&io_gate);
  io_push(&io_jobs, w);
  pthread_cond_signal(&io_bell);
  pthread_mutex_unlock(&io_gate);
  return IO_PARK;
}

static Term io_exec(Env e, IoWork* w) {
  Term fs[256];
  u32  c = (u32)term_aux(w->cont);
  u32  n = cid_arity(c);
  spare_free(e, cls_fit(n), ctr_take(e, w->cont, n, fs));
  w->cont = fs[n - 1];
  return io_eff_rows[c].run(e, fs, w);
}

static bool io_bit(u8* set, int fd, bool put) {
  u8* at = set + fd / 8;
  *at |= put << fd % 8;
  return *at >> fd % 8 & 1;
}

static void io_wait(Env e) {
  int top  = io_wake_fd[0];
  u64 soon = 0;
  for (IoWork* a = io_park; a != NULL;
    a = a->next != io_park ? a->next : NULL) {
    if (a->time != 0 && (soon == 0 || a->time < soon)) {
      soon = a->time;
    }
    if (a->evts != 0 && (int)a->word > top) {
      top = (int)a->word;
    }
  }
  u64 len = (u64)top / 64 * 8 + 8;
  u8* set[2] = { io_mem(calloc(2, len)), NULL };
  set[1] = set[0] + len;
  io_bit(set[0], io_wake_fd[0], true);
  for (IoWork* a = io_park; a != NULL;
    a = a->next != io_park ? a->next : NULL) {
    if (a->evts != 0) {
      io_bit(set[a->evts == POLLOUT], (int)a->word, true);
    }
  }
  u64 tick = io_tick();
  u64 ms = soon > tick ? (soon - tick) / 1000000 + 1 : 0;
  struct timeval tv = { ms / 1000, ms % 1000 * 1000 };
  io_sync();
  if (select(top + 1, (fd_set*)set[0], (fd_set*)set[1], NULL,
    soon == 0 ? NULL : &tv) < 0) {
    if (errno != EINTR) {
      err_fail("the poller failed");
    }
    memset(set[0], 0, 2 * len);
  }
  if (io_bit(set[0], io_wake_fd[0], false)) {
    io_take(e);
  }
  u64     now  = io_tick();
  IoWork* todo = io_park;
  io_park = NULL;
  while (todo != NULL) {
    IoWork* a   = io_pop(&todo);
    bool    due = (a->evts != 0
        && io_bit(set[a->evts == POLLOUT], (int)a->word, false))
      || (a->time != 0 && a->time <= now);
    if (!due) {
      io_park_add(a);
      continue;
    }
    Term x = a->pack(e, a);
    if (x != IO_PARK) {
      a->item = x;
      io_push(&io_runs, a);
    }
  }
  free(set[0]);
}

static int f32_text(char* buf, f32 v) {
  int n = 0;
  int p = 0;
  if (v != v) {
    return sprintf(buf, "nan");
  }
  for (; p < 9; p += 1) {
    n = snprintf(buf, 40, "%.*e", p, (double)v);
    if (strtof(buf, NULL) == v) {
      break;
    }
  }
  char* ep = strchr(buf, 'e');
  if (ep == NULL) {
    return n;
  }
  int ex = atoi(ep + 1);
  if (ex >= 21 || ex <= -7) {
    n = (int)(ep - buf) + sprintf(ep, "e%c%d", ex < 0 ? '-' : '+', abs(ex));
  } else if (ex <= p) {
    n = snprintf(buf, 40, "%.*f", p - ex, (double)v);
  } else {
    int s = *buf == '-';
    memmove(buf + s + 1, buf + s + 2, p);
    memset(buf + s + 1 + p, '0', ex - p);
    n = s + 1 + ex;
  }
  return n;
}

static Term f32_show(Env e, Term x) {
  char buf[40];
  return io_str(e, buf, f32_text(buf, f32_unbox(x)));
}

static Term f32_read(Env e, Term s) {
  u64 n = 0;
  char* text = io_cstr(e, s, &n);
  char* end;
  f32 v = strtof(text, &end);
  Term out = n > 0 && (u64)(end - text) == n && strpbrk(text, "xX(") == NULL
    ? io_box(e, CID_SOME, f32_rewrap(v)) : term_pak(CID_NONE, 0);
  free(text);
  return out;
}


// Show
// ====

// show_val prints a pure main's value as term_show spells it: d
// is a SHOW_DESC node (see show_main), w its words, and chain the
// bracket of the [a, b] or (a, b) the value continues, or 0. Con
// or Nil spell a list, Tuple a tuple, and their tails continue
// it. show_chr escapes as char_show does; show_f32 prints the
// shortest text that reads back, with a point before an e.

#if MAIN_PURE

static void show_val(Env e, u32 d, const Term* w, char chain);

static void show_chr(u64 c, char q) {
  char b[4];
  int  k = c == 10 ? 'n' : c == 9 ? 't' : c == 13 ? 'r' : c == 0 ? '0'
    : c == 92 || c == (u64)q ? (int)c : 0;
  if (k != 0) {
    printf("\\%c", k);
  } else if (c < 32 || c == 127 || (c >= 0xD800 && c <= 0xDFFF)
    || c > 0x10FFFF) {
    printf("\\u{%llx}", (unsigned long long)c);
  } else {
    fwrite(b, 1, io_utf8(b, c), stdout);
  }
}

static void show_f32(u32 x) {
  char  buf[40];
  int   n  = f32_text(buf, f32_unbox(x));
  char* ep = memchr(buf, 'e', n);
  int   m  = ep == NULL ? n : (int)(ep - buf);
  buf[n] = 0;
  if (strpbrk(buf, ".ni") == NULL) {
    printf("%.*s.0%s", m, buf, buf + m);
  } else {
    fputs(buf, stdout);
  }
}

static void show_val(Env e, u32 d, const Term* w, char chain) {
  const u32* D = SHOW_DESC;
  Term one;
  char zs[4];
  u32  zn = 0;
  for (bool tail = true; tail;) switch (tail = false, D[d]) {
    case 0:
      printf("%u", (u32)w[0]);
      break;
    case 1:
      show_f32((u32)w[0]);
      break;
    case 2:
      printf("%llun", (unsigned long long)w[0]);
      break;
    case 3:
      putchar('\'');
      show_chr(D[d + 1] != 0 ? term_loc(w[0]) : w[0], '\'');
      putchar('\'');
      break;
    case 4:
      putchar('"');
      for (Term s = w[0]; term_aux(s) == CID_SCON;) {
        u64 l = term_peek(e.mem, s);
        show_chr(e.mem[l], '"');
        s = e.mem[l + 1];
      }
      putchar('"');
      break;
    case 5:
      fputs("{==}", stdout);
      break;
    case 6:
      putchar('[');
      for (u32 i = 0, g = D[d + 2]; i < 1u << (blk_cls(w[0]) - g); i += 1) {
        Term v[1u << g];
        for (u32 j = 0; j < 1u << g; j += 1) {
          v[j] = blk_read(e.mem, term_tag(w[0]) == TAG_ARR,
            term_peek(e.mem, w[0]), (i << g) + j);
        }
        fputs(i > 0 ? ", " : "", stdout);
        show_val(e, D[d + 1], v, 0);
      }
      putchar(']');
      break;
    default: {
      Term t   = w[0];
      bool box = D[d + 1] != 0;
      u32  key = box ? (u32)term_aux(t) : D[d + 2] > 1 ? (u32)t : 0;
      u32  a   = d + 3;
      for (u32 i = 0; box ? D[a + 1] != key : i != key; i += 1) {
        a += 4 + 2 * D[a + 2];
      }
      if (box) {
        one = term_loc(t);
        w   = term_tag(t) == TAG_PAK ? &one : e.mem + term_peek(e.mem, t);
      }
      char o = "{[("[D[a + 3]];
      if (o == '{') {
        printf("%s{", SHOW_NAMES[D[a]]);
      } else if (chain != o) {
        putchar(o);
      }
      if (o == '{' || chain != o) {
        zs[zn++] = "}])"[D[a + 3]];
      }
      for (u32 j = 0; j < D[a + 2]; j += 1) {
        if (o == '[' ? j == 0 && chain == o : j > 0) {
          fputs(", ", stdout);
        }
        if (j == 1 && o != '{') {
          tail  = true;
          chain = o;
          d     = D[a + 5 + 2 * j];
          w     = w + D[a + 4 + 2 * j];
        } else {
          show_val(e, D[a + 5 + 2 * j], w + D[a + 4 + 2 * j], 0);
        }
      }
    }
  }
  while (zn > 0) {
    putchar(zs[--zn]);
  }
}

#endif

// Run
// ===

static void io_step(Env e, IoWork* a) {
  for (;;) {
    u64  ap  = task_node(e, FID_CLO_APPLY, TERM_HOLE, 0, 0);
    e.mem[ap]     = a->cont;
    e.mem[ap + 1] = a->item;
    Term req = corpus_eval(e.mem, term_tsk(FID_CLO_APPLY, ap));
    u32  c   = (u32)term_aux(req);
    u64  at  = term_peek(e.mem, req);
    if (c == CID_EMIT) {
      term_drop(e, req);
      free(a);
      io_live -= 1;
      return;
    }
    if (c == CID_HALT) {
      io_errs(e, e.mem[at + 1]);
      exit((int)(u32)e.mem[at]);
    }
    if (io_eff_rows[c].run == NULL) {
      err_fail("an alien request");
    }
    u32 need = io_eff_rows[c].ask;
    u32 word = (u32)(need & IO_READ ? io_hand_v(e.mem[at]) : e.mem[at]);
    a->cont  = req;
    if (need != 0) {
      io_wait_on(a, (int)word, need & IO_READ ? POLLIN : 0,
        need & IO_TIME ? io_tick() + (u64)word * 1000000ull : 0, io_exec);
      return;
    }
    Term x = io_exec(e, a);
    if (x == IO_PARK) {
      return;
    }
    a->item = x;
  }
}

OUTLINE void io_loop(u64* H) {
  Env e = { H, ALC[0] };
  io_stk = pool_stack();
  signal(SIGPIPE, SIG_IGN);
  if (pipe(io_wake_fd) | fcntl(io_wake_fd[0], F_SETFL, O_NONBLOCK)) {
    err_fail("the event loop failed to open");
  }
  Term m = corpus_eval(H, term_tsk(MAIN_FID, task_node(e, MAIN_FID,
    TERM_HOLE, 0, 0)));
#if MAIN_PURE
  show_val(e, 0, H + H_ROOT_WORD, 0);
  putchar('\n');
  return;
#endif
  io_spawn(m);
  for (u32 n = 0;; n += 1) {
    if (io_runs == NULL) {
      if (io_live == 0) {
        return;
      }
      if (io_park == NULL && io_busy == 0) {
        io_sync();
        err_fail("deadlock: every computation waits on a channel");
      }
      io_wait(e);
      continue;
    }
    if ((n & 63) == 0 && io_busy != 0) {
      io_take(e);
    }
    io_step(e, io_pop(&io_runs));
  }
}

// Requests
// ========

// Stdin.read: reads all of standard input into one packed Array<U32> (byte
// j in slot j / 4 at bits 8 * (j % 4), little-endian; the pad bytes are 0)
// and answers it beside its byte count. It only copies bytes.
Term stdin_read_run(Env e, Term* f, IoWork* w) {
  u64 cap = 1ull << 16, n = 0;
  u8* buf = io_mem(malloc(cap));
  for (;;) {
    if (n == cap) {
      if (cap >= (1ull << 32)) {
        free(buf);
        return io_fail(e, EFBIG, "heph-core: request over 4 GiB");
      }
      cap *= 2;
      buf = io_mem(realloc(buf, cap));
    }
    ssize_t r = read(0, buf + n, cap - n);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r < 0) {
      u32 code = (u32)errno;
      free(buf);
      return io_fail(e, code, "heph-core: cannot read stdin");
    }
    if (r == 0) {
      break;
    }
    n += (u64)r;
  }
  // at least one pad byte, so the slot count n / 4 + 1 is never 0
  u32 c = cls_fit((u32)(n / 4 + 1));
  u64 l = heap_alloc(e, buf_wcls(c));
  if (err_seen(e.mem)) {
    free(buf);
    return io_fail(e, ENOMEM, "heph-core: out of memory for the request");
  }
  u8* dst = (u8*)(e.mem + l);
  memcpy(dst, buf, n);
  memset(dst + n, 0, (4ull << c) - n);
  free(buf);
  return io_done(e, io_tup(e, term_blk(0, c, l), (Term)n));
}

static void __attribute__((constructor)) stdin_read_use(void) {
  io_eff(CID_STDIN_READ, stdin_read_run, 0);
}
// Stdout.write: writes the first n bytes of a packed Array<U32> (byte j in
// slot j / 4 at bits 8 * (j % 4), little-endian) to standard output and
// frees the array. It only copies bytes.
Term stdout_write_run(Env e, Term* f, IoWork* w) {
  Term a = f[0];
  u64  n = (u64)f[1];
  if (term_tag(a) != TAG_BUF || n > (4ull << blk_cls(a))) {
    term_sink(e, a);
    return io_fail(e, EINVAL, "heph-core: output past its buffer");
  }
  const u8* p = (const u8*)(e.mem + blk_loc(e.mem, a));
  size_t put = fwrite(p, 1, (size_t)n, stdout);
  term_sink(e, a);
  if (put != (size_t)n) {
    return io_fail(e, EIO, "heph-core: cannot write stdout");
  }
  return io_done(e, term_pak(CID_UNIT, 0));
}

static void __attribute__((constructor)) stdout_write_use(void) {
  io_eff(CID_STDOUT_WRITE, stdout_write_run, 0);
}


// Main
// ====

int main(int argc, char** argv) {
  long thr = 0;
  int  gpu = -1;
  u64  mem = 0;
  io_argv = argv;
  io_argc = 1;
  for (int i = 1; i < argc; i += 1) {
    const char* a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : NULL;
    if (strcmp(a, "--") == 0) {
      while (i + 1 < argc) {
        io_argv[io_argc++] = argv[++i];
      }
    } else if (strcmp(a, "--bend-help") == 0) {
      printf(CLI_HELP, argv[0]);
      return 0;
    } else if (strcmp(a, "--gpu-build") == 0) {
      if (gpu_probe() && !gpu_make(gpu_path())) {
        fprintf(stderr, "bend: cannot write %s\n", gpu_path());
        return 1;
      }
      return 0;
    } else if (strcmp(a, "--threads") == 0) {
      char* end = NULL;
      thr = v != NULL ? strtol(v, &end, 10) : 0;
      if (thr < 1 || end == NULL || *end != '\0') {
        err_fail("expected a thread count of 1 or more after --threads");
      }
      i += 1;
    } else if (strcmp(a, "--gpu") == 0) {
      char*  end = NULL;
      double n   = v != NULL ? strtod(v, &end) : 0;
      u64    mul = end == NULL ? 0 : strcmp(end, "GB") == 0 ? 1ull << 30
        : strcmp(end, "MB") == 0 ? 1ull << 20 : 0;
      if (v != NULL && strcmp(v, "off") == 0) {
        gpu = 0;
      } else if (v != NULL && (strcmp(v, "on") == 0 || (mul != 0 && n > 0))) {
        gpu = 1;
        mem = (u64)(n * (double)mul);
      } else {
        err_fail("expected on, off or a size like 4GB after --gpu");
      }
      i += 1;
    } else {
      io_argv[io_argc++] = argv[i];
    }
  }
  bool dev = gpu != 0 && BANGS != 0 && gpu_probe();
  if (gpu == 1 && BANGS != 0 && !dev) {
    err_fail("--gpu on, but this binary found no usable GPU (a CUDA GPU needs"
      " concurrent managed access, which WSL2's lack)");
  }
  io_loop(corpus_setup(dev, thr > 0 ? thr : cpu_count(), mem));
  io_sync();
  return 0;
}

#endif
