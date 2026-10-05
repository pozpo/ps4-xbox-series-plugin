#ifndef STUB_PAD_H
#define STUB_PAD_H
#include "_types/pad.h"
/* public symbols (HOOK32 takes their address in the real SDK) */
int32_t scePadRead(int32_t, OrbisPadData*, int32_t);
int32_t scePadReadState(int32_t, OrbisPadData*);
int32_t scePadOpen(int32_t, int32_t, int32_t, void*);
int32_t scePadClose(int32_t);
int32_t scePadGetControllerInformation(int32_t, OrbisPadInformation*);
int32_t scePadGetHandle(int32_t, uint32_t, uint32_t);
int32_t scePadSetLightBar(int32_t, OrbisPadColor*);
int32_t scePadResetLightBar(int32_t);
int32_t scePadSetVibration(int32_t, const OrbisPadVibeParam*);
int32_t scePadResetOrientation(int32_t);
void scePadSetMotionSensorState();
void scePadSetTiltCorrectionState();
void scePadSetAngularVelocityDeadbandState();
void scePadDeviceClassParseData();
void scePadDeviceClassGetExtendedInformation();
#endif
