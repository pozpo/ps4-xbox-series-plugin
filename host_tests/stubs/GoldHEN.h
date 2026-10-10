#ifndef STUB_GOLDHEN_H
#define STUB_GOLDHEN_H
/* NOTE: like the real SDK header, this does NOT include <stdint.h>: the
 * including .c file must do it first (a real build failed on exactly this). */
/* Copied from the user's real SDK: C:\GoldHEN_Plugins_SDK\include\GoldHEN.h
 * (struct proc_info = lines 35-43, sys_sdk_proc_info = line 83). */
struct proc_info {
    int pid;
    char name[40];
    char path[64];
    char titleid[16];
    char contentid[64];
    char version[6];
    uint64_t base_address;
} __attribute__((packed));
int sys_sdk_proc_info(struct proc_info* info);
#endif
