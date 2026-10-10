#ifndef STUB_LIBKERNEL_H
#define STUB_LIBKERNEL_H
#include <stdint.h>
#include <stddef.h>
#include "_types/pthread.h"
int scePthreadMutexInit(OrbisPthreadMutex* m, const void* attr, const char* name);
int scePthreadMutexLock(OrbisPthreadMutex* m);
int scePthreadMutexUnlock(OrbisPthreadMutex* m);
int scePthreadCreate(OrbisPthread* t, const void* attr, void* fn, void* arg, const char* name);
int scePthreadJoin(OrbisPthread t, void** ret);
int sceKernelUsleep(unsigned int us);
uint64_t sceKernelGetProcessTime(void);
int sceKernelOpen(const char* path, int flags, int mode);
long sceKernelRead(int fd, void* buf, size_t n);
long sceKernelWrite(int fd, const void* buf, size_t n);
int sceKernelClose(int fd);
typedef struct { int type; int targetId; char message[1024]; } OrbisNotificationRequest;
#define NotificationRequest 0
int sceKernelSendNotificationRequest(int dev, OrbisNotificationRequest* r, size_t sz, int blocking);
#endif
