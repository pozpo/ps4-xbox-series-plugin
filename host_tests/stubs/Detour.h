#ifndef STUB_DETOUR_H
#define STUB_DETOUR_H
#include <stdint.h>
/* Host-test model of the GoldHEN hook macros.
 * The shape of HOOK32 below is copied from the real SDK's Utilities.h as it
 * appeared in a compiler error message on a real Windows build:
 *     Detour_DetourFunction((&(Detour_##name)), (uint64_t)name, (void *)(&(name##_hook)));
 * so 'name' must be a declared function and 'name_hook' must be declared
 * BEFORE HOOK32 is used. HOOK_CONTINUE calls a function pointer named
 * real_<name> that the test sets to a fake "PS4 library". */
extern int host_detour_calls;   /* counted by fake_platform.c */
typedef struct { int dummy; } StubDetour;
static inline void Detour_DetourFunction(StubDetour* d, uint64_t target, void* hook) {
    (void)d; (void)target; (void)hook;
    __atomic_fetch_add(&host_detour_calls, 1, __ATOMIC_SEQ_CST);
}
#define HOOK_INIT(name) StubDetour Detour_##name; void* real_##name
#define UNHOOK(name) ((void)0)
#define HOOK_CONTINUE(name, type, ...) (((type)real_##name)(__VA_ARGS__))
#endif
