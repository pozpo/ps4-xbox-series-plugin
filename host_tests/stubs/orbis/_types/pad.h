#ifndef STUB_PAD_TYPES_H
#define STUB_PAD_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#define ORBIS_PAD_BUTTON_L3        0x00000002
#define ORBIS_PAD_BUTTON_R3        0x00000004
#define ORBIS_PAD_BUTTON_OPTIONS   0x00000008
#define ORBIS_PAD_BUTTON_UP        0x00000010
#define ORBIS_PAD_BUTTON_RIGHT     0x00000020
#define ORBIS_PAD_BUTTON_DOWN      0x00000040
#define ORBIS_PAD_BUTTON_LEFT      0x00000080
#define ORBIS_PAD_BUTTON_L2        0x00000100
#define ORBIS_PAD_BUTTON_R2        0x00000200
#define ORBIS_PAD_BUTTON_L1        0x00000400
#define ORBIS_PAD_BUTTON_R1        0x00000800
#define ORBIS_PAD_BUTTON_TRIANGLE  0x00001000
#define ORBIS_PAD_BUTTON_CIRCLE    0x00002000
#define ORBIS_PAD_BUTTON_CROSS     0x00004000
#define ORBIS_PAD_BUTTON_SQUARE    0x00008000
#define ORBIS_PAD_BUTTON_TOUCH_PAD 0x00100000
#define ORBIS_PAD_CONNECTION_TYPE_STANDARD 0
#define ORBIS_PAD_DEVICE_CLASS_PAD 0
typedef struct { uint8_t x, y; } StubStick;
typedef struct OrbisPadData {
    uint32_t buttons;
    StubStick leftStick;
    StubStick rightStick;
    struct { uint8_t l2, r2; } analogButtons;
    struct { float x, y, z, w; } quat;
    struct { float x, y, z; } vel;
    struct { float x, y, z; } acell;
    struct { uint8_t fingers; } touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t count;
} OrbisPadData;
typedef struct OrbisPadInformation {
    float touchpadDensity;
    uint16_t touchResolutionX, touchResolutionY;
    uint8_t stickDeadzoneL, stickDeadzoneR;
    uint8_t connectionType, count, connected;
    uint32_t deviceClass;
} OrbisPadInformation;
typedef struct { uint8_t r, g, b, a; } OrbisPadColor;
typedef struct { uint8_t lgMotor, smMotor; } OrbisPadVibeParam;
#endif
