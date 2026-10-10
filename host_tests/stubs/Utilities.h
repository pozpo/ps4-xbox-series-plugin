#ifndef STUB_UTILITIES_H
#define STUB_UTILITIES_H
#include <stdint.h>
#include "Detour.h"
#define klog(...) ((void)0)
#define HOOK32(name) \
    klog("%s:%d HOOK32() Create " #name "\n", __FUNCTION__, __LINE__); \
    Detour_DetourFunction((&(Detour_##name)), (uint64_t)name, (void *)(&(name##_hook)));
#endif
