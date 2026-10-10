/*
 * Xbox Series X|S controller - USB protocol parser. See xbox_series.h.
 */

#include "xbox_series.h"
#include <string.h>

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

void xbs_parse_packet(const uint8_t* d, int len, XbsPacket* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->kind = XBS_PKT_IGNORED;

    if (!d || len < 4) {
        out->kind = XBS_PKT_INVALID;
        return;
    }

    if (d[0] == XBS_GIP_CMD_INPUT) {
        /* Header: [0]=0x20 command, [1]=flags, [2]=sequence, [3]=payload length.
         * A length with bit 7 set would use a 2-byte length field. Input
         * packets are < 64 bytes so that never happens; treat it as broken.   */
        if (d[3] & 0x80) { out->kind = XBS_PKT_INVALID; return; }
        int payload = d[3];
        if (payload < 14 || len < 4 + payload) { out->kind = XBS_PKT_INVALID; return; }

        XbsInput* in = &out->input;
        in->buttons = (uint16_t)(d[4] | ((uint16_t)d[5] << 8));
        in->lt = le16(&d[6]);
        in->rt = le16(&d[8]);
        if (in->lt > XBS_TRIGGER_MAX) in->lt = XBS_TRIGGER_MAX;
        if (in->rt > XBS_TRIGGER_MAX) in->rt = XBS_TRIGGER_MAX;
        in->lx = (int16_t)le16(&d[10]);
        in->ly = (int16_t)le16(&d[12]);
        in->rx = (int16_t)le16(&d[14]);
        in->ry = (int16_t)le16(&d[16]);

        /* A D-pad cannot press opposite directions at once. If the bits say
         * so, the report is glitched: cancel that axis.                       */
        if ((in->buttons & XBS_BTN_UP)   && (in->buttons & XBS_BTN_DOWN))
            in->buttons &= (uint16_t)~(XBS_BTN_UP | XBS_BTN_DOWN);
        if ((in->buttons & XBS_BTN_LEFT) && (in->buttons & XBS_BTN_RIGHT))
            in->buttons &= (uint16_t)~(XBS_BTN_LEFT | XBS_BTN_RIGHT);

        out->kind = XBS_PKT_INPUT;
        return;
    }

    if (d[0] == XBS_GIP_CMD_VIRTUAL_KEY) {
        /* Xbox button: [1]=flags (0x30 = "please acknowledge"), [2]=sequence,
         * [3]=payload length, [4]=state (bit 0 = pressed).                    */
        if (len < 5 || d[3] < 1) { out->kind = XBS_PKT_INVALID; return; }
        out->guide_pressed = (d[4] & 0x01) ? 1 : 0;
        out->needs_ack = (d[1] == 0x30) ? 1 : 0;
        out->seq = d[2];
        out->kind = XBS_PKT_GUIDE;
        return;
    }

    /* Anything else (announce, status, ...) is harmless and ignored. */
}

int xbs_build_power_on(uint8_t* out, int cap) {
    static const uint8_t cmd[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
    if (!out || cap < (int)sizeof(cmd)) return 0;
    memcpy(out, cmd, sizeof(cmd));
    return (int)sizeof(cmd);
}

int xbs_build_guide_ack(uint8_t seq, uint8_t* out, int cap) {
    /* Bytes taken from the Linux xpad driver commit "fix stuck mode button on
     * Xbox One S pad": 01 20 <seq> 09 00 07 20 02 00 00 00 00 00             */
    static const uint8_t ack[13] = {
        0x01, 0x20, 0x00, 0x09, 0x00, 0x07, 0x20, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00
    };
    if (!out || cap < (int)sizeof(ack)) return 0;
    memcpy(out, ack, sizeof(ack));
    out[2] = seq;
    return (int)sizeof(ack);
}
