/*
 * Xbox Series X|S controller - USB protocol ("GIP") definitions and parser.
 *
 * This file contains NO PS4 code. It only knows how to turn the raw bytes
 * that the controller sends over USB into a clean XbsInput structure.
 *
 * Evidence for the layout (see GUIDE.md, "Protocol notes"):
 *  - The same GIP "0x20 input" packet is used by Xbox One and Series X|S
 *    controllers (the Linux xpad driver and SDL both treat VID 045E PID 0B12
 *    as Xbox-One-family GIP).
 *  - The existing working plugin read packets of 18+ bytes with byte0 == 0x20.
 *  - Series firmware makes the packet LONGER (SDL documents 36, 44 or 48
 *    bytes). The first 18 bytes are unchanged, so we read the length from the
 *    packet header instead of assuming 18.
 */

#ifndef XBOX_SERIES_H
#define XBOX_SERIES_H

#include <stdint.h>

/* GIP command numbers we care about */
#define XBS_GIP_CMD_ACK          0x01
#define XBS_GIP_CMD_POWER        0x05
#define XBS_GIP_CMD_VIRTUAL_KEY  0x07   /* Xbox/Guide button               */
#define XBS_GIP_CMD_INPUT        0x20   /* normal buttons/sticks/triggers  */

/* Bits in XbsInput.buttons (= packet byte 4 | byte 5 << 8) */
#define XBS_BTN_MENU    0x0004   /* three-lines button ("Start"-like)       */
#define XBS_BTN_VIEW    0x0008   /* two-squares button ("Back"-like)        */
#define XBS_BTN_A       0x0010
#define XBS_BTN_B       0x0020
#define XBS_BTN_X       0x0040
#define XBS_BTN_Y       0x0080
#define XBS_BTN_UP      0x0100
#define XBS_BTN_DOWN    0x0200
#define XBS_BTN_LEFT    0x0400
#define XBS_BTN_RIGHT   0x0800
#define XBS_BTN_LB      0x1000
#define XBS_BTN_RB      0x2000
#define XBS_BTN_L3      0x4000
#define XBS_BTN_R3      0x8000

/* Buttons that are "latched" so a very short tap cannot be missed.
 * (D-pad and triggers are deliberately excluded.)                          */
#define XBS_LATCH_MASK  (XBS_BTN_MENU | XBS_BTN_VIEW | XBS_BTN_A | XBS_BTN_B | \
                         XBS_BTN_X | XBS_BTN_Y | XBS_BTN_LB | XBS_BTN_RB |     \
                         XBS_BTN_L3 | XBS_BTN_R3)

#define XBS_TRIGGER_MAX 1023     /* triggers are 10 bit: 0..1023            */

typedef struct {
    uint16_t buttons;            /* XBS_BTN_* bits                          */
    uint16_t lt;                 /* left trigger  0..1023                   */
    uint16_t rt;                 /* right trigger 0..1023                   */
    int16_t  lx, ly, rx, ry;     /* sticks -32768..32767, +Y = UP           */
} XbsInput;

typedef enum {
    XBS_PKT_INVALID = -1,        /* looked like our packet but is broken    */
    XBS_PKT_IGNORED = 0,         /* some other (harmless) packet type       */
    XBS_PKT_INPUT   = 1,         /* valid 0x20 input packet                 */
    XBS_PKT_GUIDE   = 2          /* Xbox button report (0x07)               */
} XbsPacketKind;

typedef struct {
    int      kind;               /* XbsPacketKind                           */
    XbsInput input;              /* valid when kind == XBS_PKT_INPUT        */
    int      guide_pressed;      /* valid when kind == XBS_PKT_GUIDE        */
    int      needs_ack;          /* controller asked for an acknowledgement */
    uint8_t  seq;                /* sequence number to echo in the ack      */
} XbsPacket;

/* Parses one raw USB packet. Never reads outside data[0..len-1]. */
void xbs_parse_packet(const uint8_t* data, int len, XbsPacket* out);

/* Builds the "power on" command. Returns its length (5), or 0 if cap is too small. */
int xbs_build_power_on(uint8_t* out, int cap);

/* Builds the Xbox-button acknowledgement. Returns its length (13) or 0. */
int xbs_build_guide_ack(uint8_t seq, uint8_t* out, int cap);

#endif /* XBOX_SERIES_H */
