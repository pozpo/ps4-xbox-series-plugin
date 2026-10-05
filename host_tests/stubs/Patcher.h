#ifndef STUB_PATCHER_H
#define STUB_PATCHER_H
#include <stdint.h>
typedef struct Patcher { int dummy; } Patcher;
static inline void Patcher_Construct(Patcher* p) { p->dummy = 1; }
static inline void Patcher_Install_Patch(Patcher* p, uint64_t addr, const uint8_t* d, int n) { (void)p; (void)addr; (void)d; (void)n; }
static inline void Patcher_Destroy(Patcher* p) { p->dummy = 0; }
#endif
