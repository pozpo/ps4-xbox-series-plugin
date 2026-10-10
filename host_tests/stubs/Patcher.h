#ifndef STUB_PATCHER_H
#define STUB_PATCHER_H
#include <stdint.h>
extern int host_patch_calls;   /* counted by fake_platform.c */
typedef struct Patcher { int dummy; } Patcher;
static inline void Patcher_Construct(Patcher* p) { p->dummy = 1; }
static inline void Patcher_Install_Patch(Patcher* p, uint64_t addr, const uint8_t* d, int n) { (void)p; (void)addr; (void)d; (void)n; __atomic_fetch_add(&host_patch_calls, 1, __ATOMIC_SEQ_CST); }
static inline void Patcher_Destroy(Patcher* p) { p->dummy = 0; }
#endif
