#ifndef STUB_USERSERVICE_H
#define STUB_USERSERVICE_H
#include <stdint.h>
#define ORBIS_USER_SERVICE_MAX_LOGIN_USERS 4
#define ORBIS_USER_SERVICE_USER_ID_INVALID (-1)
typedef struct { int32_t userId[ORBIS_USER_SERVICE_MAX_LOGIN_USERS]; } OrbisUserServiceLoginUserIdList;
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList*);
int32_t sceUserServiceGetForegroundUser(int32_t*);
#endif
