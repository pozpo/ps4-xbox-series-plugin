#ifndef STUB_DETOUR_H
#define STUB_DETOUR_H
/* Host-test model of the GoldHEN hook macros: HOOK_CONTINUE calls a function
 * pointer named real_<name> that the test sets to a fake "PS4 library". */
#define HOOK_INIT(name) void* real_##name
#define HOOK32(name) ((void)0)
#define UNHOOK(name) ((void)0)
#define HOOK_CONTINUE(name, type, ...) (((type)real_##name)(__VA_ARGS__))
#endif
