/*
 * Unit tests for the pure-logic parts: GIP parser, translator, settings.
 * Build/run: see host_tests/run_tests.sh
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "config.h"
#include "xbox_series.h"
#include "translator.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (uint8_t)(v >> 8); }

static int make_input(uint8_t* p, int total, uint16_t btn, uint16_t lt, uint16_t rt,
                      int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    memset(p, 0, (size_t)total);
    p[0] = 0x20; p[1] = 0x00; p[2] = 7; p[3] = (uint8_t)(total - 4);
    put16(p + 4, btn);
    put16(p + 6, lt); put16(p + 8, rt);
    put16(p + 10, (uint16_t)lx); put16(p + 12, (uint16_t)ly);
    put16(p + 14, (uint16_t)rx); put16(p + 16, (uint16_t)ry);
    return total;
}

static void test_parser(void) {
    printf("[parser]\n");
    uint8_t p[64];
    XbsPacket k;

    int n = make_input(p, 18, XBS_BTN_A | XBS_BTN_RB, 512, 1023, 1000, -2000, 32767, -32768);
    xbs_parse_packet(p, n, &k);
    CHECK(k.kind == XBS_PKT_INPUT, "18-byte packet is input");
    CHECK(k.input.buttons == (XBS_BTN_A | XBS_BTN_RB), "buttons 0x%x", k.input.buttons);
    CHECK(k.input.lt == 512 && k.input.rt == 1023, "triggers %d %d", k.input.lt, k.input.rt);
    CHECK(k.input.lx == 1000 && k.input.ly == -2000, "left stick");
    CHECK(k.input.rx == 32767 && k.input.ry == -32768, "right stick extremes");

    /* Series firmware packet lengths documented by SDL: 36, 44, 48 */
    int lens[] = {18, 20, 36, 44, 48, 64};
    for (unsigned i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        n = make_input(p, lens[i], XBS_BTN_Y, 3, 4, 5, 6, 7, 8);
        /* make the extra bytes noisy: they must not matter */
        for (int b = 18; b < lens[i]; b++) p[b] = 0xAA;
        xbs_parse_packet(p, n, &k);
        CHECK(k.kind == XBS_PKT_INPUT && k.input.buttons == XBS_BTN_Y && k.input.ry == 8,
              "length %d parsed", lens[i]);
    }

    /* every button bit individually */
    uint16_t bits[] = {XBS_BTN_MENU, XBS_BTN_VIEW, XBS_BTN_A, XBS_BTN_B, XBS_BTN_X, XBS_BTN_Y,
                       XBS_BTN_UP, XBS_BTN_DOWN, XBS_BTN_LEFT, XBS_BTN_RIGHT, XBS_BTN_LB,
                       XBS_BTN_RB, XBS_BTN_L3, XBS_BTN_R3};
    for (unsigned i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
        n = make_input(p, 18, bits[i], 0, 0, 0, 0, 0, 0);
        xbs_parse_packet(p, n, &k);
        CHECK(k.kind == XBS_PKT_INPUT && k.input.buttons == bits[i], "bit 0x%x", bits[i]);
    }
    /* byte positions: A is bit 4 of byte 4; D-pad up is bit 0 of byte 5 */
    memset(p, 0, 18); p[0] = 0x20; p[3] = 14; p[4] = 0x10; p[5] = 0x01;
    xbs_parse_packet(p, 18, &k);
    CHECK(k.input.buttons == (XBS_BTN_A | XBS_BTN_UP), "raw byte layout A+Up");

    /* trigger clamp */
    n = make_input(p, 18, 0, 0xFFFF, 0x8000, 0, 0, 0, 0);
    xbs_parse_packet(p, n, &k);
    CHECK(k.input.lt == 1023 && k.input.rt == 1023, "trigger clamp");

    /* opposite d-pad directions cancel */
    n = make_input(p, 18, XBS_BTN_UP | XBS_BTN_DOWN | XBS_BTN_LEFT | XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    xbs_parse_packet(p, n, &k);
    CHECK(k.input.buttons == (XBS_BTN_LEFT | XBS_BTN_A), "opposites cancel, got 0x%x", k.input.buttons);

    /* broken packets never become input */
    n = make_input(p, 18, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    xbs_parse_packet(p, 17, &k);  CHECK(k.kind == XBS_PKT_INVALID, "truncated 17");
    xbs_parse_packet(p, 10, &k);  CHECK(k.kind == XBS_PKT_INVALID, "truncated 10");
    xbs_parse_packet(p, 3, &k);   CHECK(k.kind == XBS_PKT_INVALID, "truncated 3");
    xbs_parse_packet(p, 0, &k);   CHECK(k.kind == XBS_PKT_INVALID, "empty");
    xbs_parse_packet(NULL, 18, &k); CHECK(k.kind == XBS_PKT_INVALID, "NULL data");
    xbs_parse_packet(p, 18, NULL);  CHECK(1, "NULL out does not crash");
    p[3] = 10; xbs_parse_packet(p, 18, &k); CHECK(k.kind == XBS_PKT_INVALID, "payload length 10 too small");
    p[3] = 14; p[3] |= 0x80; xbs_parse_packet(p, 18, &k); CHECK(k.kind == XBS_PKT_INVALID, "2-byte length rejected");
    p[3] = 40; xbs_parse_packet(p, 18, &k); CHECK(k.kind == XBS_PKT_INVALID, "claims more than received");

    /* other packet types are ignored and must not look like input */
    uint8_t other[24] = {0x02, 0x20, 1, 20};
    xbs_parse_packet(other, 24, &k); CHECK(k.kind == XBS_PKT_IGNORED, "announce ignored");
    uint8_t status[8] = {0x03, 0x20, 1, 4};
    xbs_parse_packet(status, 8, &k); CHECK(k.kind == XBS_PKT_IGNORED, "status ignored");

    /* guide report */
    uint8_t guide[6] = {0x07, 0x30, 0x42, 0x02, 0x01, 0x5B};
    xbs_parse_packet(guide, 6, &k);
    CHECK(k.kind == XBS_PKT_GUIDE && k.guide_pressed == 1 && k.needs_ack == 1 && k.seq == 0x42, "guide pressed+ack");
    guide[1] = 0x20; guide[4] = 0x00;
    xbs_parse_packet(guide, 6, &k);
    CHECK(k.kind == XBS_PKT_GUIDE && k.guide_pressed == 0 && k.needs_ack == 0, "guide released, no ack");
    xbs_parse_packet(guide, 4, &k); CHECK(k.kind == XBS_PKT_INVALID, "short guide invalid");

    /* commands */
    uint8_t out[16];
    CHECK(xbs_build_power_on(out, 16) == 5 && memcmp(out, "\x05\x20\x00\x01\x00", 5) == 0, "power on bytes");
    CHECK(xbs_build_power_on(out, 4) == 0, "power on small buffer");
    CHECK(xbs_build_guide_ack(0x42, out, 16) == 13, "ack length");
    CHECK(memcmp(out, "\x01\x20\x42\x09\x00\x07\x20\x02\x00\x00\x00\x00\x00", 13) == 0, "ack bytes with sequence");
    CHECK(xbs_build_guide_ack(1, out, 12) == 0, "ack small buffer");
}

static OrbisPadData tr(uint16_t btn, uint16_t lt, uint16_t rt, int16_t lx, int16_t ly,
                       int16_t rx, int16_t ry, int dz, int thr, int viewtp) {
    XbsInput in = {btn, lt, rt, lx, ly, rx, ry};
    XbsTranslateConfig cfg;
    xbs_translate_config_make(&cfg, dz, thr, viewtp);
    OrbisPadData d;
    xbs_fill_pad_data(&d, &in, &cfg, 1, 12345);
    return d;
}

static void test_translator(void) {
    printf("[translator]\n");
    OrbisPadData d;

    /* neutral */
    d = tr(0, 0, 0, 0, 0, 0, 0, 12, 12, 0);
    CHECK(d.buttons == 0, "neutral buttons");
    CHECK(d.leftStick.x == 128 && d.leftStick.y == 128 && d.rightStick.x == 128 && d.rightStick.y == 128,
          "neutral sticks centred (%d,%d)", d.leftStick.x, d.leftStick.y);
    CHECK(d.connected == 1 && d.timestamp == 12345 && d.count == 1, "connected/timestamp/count");
    CHECK(d.quat.w == 1.0f && d.acell.z == 1.0f && d.touch.fingers == 0, "motion defaults");

    /* stick extremes and direction */
    d = tr(0, 0, 0, 32767, 32767, -32768, -32768, 12, 12, 0);
    CHECK(d.leftStick.x == 255, "left X full right = 255, got %d", d.leftStick.x);
    CHECK(d.leftStick.y == 0,   "left Y full UP = 0, got %d", d.leftStick.y);
    CHECK(d.rightStick.x == 0,  "right X full left = 0, got %d", d.rightStick.x);
    CHECK(d.rightStick.y == 255,"right Y full DOWN = 255, got %d", d.rightStick.y);

    /* dead zone: 12% of 32767 = 3932 */
    d = tr(0, 0, 0, 3900, -3900, 0, 0, 12, 12, 0);
    CHECK(d.leftStick.x == 128 && d.leftStick.y == 128, "inside dead zone stays centred");
    d = tr(0, 0, 0, 4500, 0, 0, 0, 12, 12, 0);
    CHECK(d.leftStick.x > 128 && d.leftStick.x < 140, "just outside dead zone is small, got %d", d.leftStick.x);
    d = tr(0, 0, 0, 0, 0, 0, 0, 0, 12, 0);
    CHECK(d.leftStick.x == 128, "zero dead zone centre");
    d = tr(0, 0, 0, 1000, 0, 0, 0, 0, 12, 0);
    CHECK(d.leftStick.x > 128, "zero dead zone passes small motion");

    /* monotonic and symmetric */
    int prev = -1, ok = 1;
    for (int v = -32768; v <= 32767; v += 97) {
        d = tr(0, 0, 0, (int16_t)v, 0, 0, 0, 12, 12, 0);
        if ((int)d.leftStick.x < prev) ok = 0;
        prev = d.leftStick.x;
    }
    CHECK(ok, "stick X output is monotonic");

    /* triggers */
    d = tr(0, 0, 0, 0, 0, 0, 0, 12, 12, 0);
    CHECK(d.analogButtons.l2 == 0 && !(d.buttons & DS4_BUTTON_L2), "trigger 0");
    d = tr(0, 1023, 1023, 0, 0, 0, 0, 12, 12, 0);
    CHECK(d.analogButtons.l2 == 255 && d.analogButtons.r2 == 255, "trigger full = 255");
    CHECK((d.buttons & DS4_BUTTON_L2) && (d.buttons & DS4_BUTTON_R2), "trigger full -> digital L2/R2");
    d = tr(0, 512, 100, 0, 0, 0, 0, 12, 12, 0);
    CHECK(d.analogButtons.l2 == 128 && d.analogButtons.r2 == 25, "trigger scaling %d %d", d.analogButtons.l2, d.analogButtons.r2);
    CHECK((d.buttons & DS4_BUTTON_L2) && !(d.buttons & DS4_BUTTON_R2),
          "L2 at 128/255 is digital; R2 at 25/255 is below the 30/255 (12%%) threshold");
    d = tr(0, 40, 40, 0, 0, 0, 0, 12, 12, 0);   /* 10/255 < 30/255 */
    CHECK(!(d.buttons & DS4_BUTTON_L2), "light trigger below threshold: no digital");

    /* every button maps to exactly the right PS4 bit */
    struct { uint16_t x; uint32_t ps; const char* n; } map[] = {
        {XBS_BTN_A, DS4_BUTTON_CROSS, "A->Cross"}, {XBS_BTN_B, DS4_BUTTON_CIRCLE, "B->Circle"},
        {XBS_BTN_X, DS4_BUTTON_SQUARE, "X->Square"}, {XBS_BTN_Y, DS4_BUTTON_TRIANGLE, "Y->Triangle"},
        {XBS_BTN_LB, DS4_BUTTON_L1, "LB->L1"}, {XBS_BTN_RB, DS4_BUTTON_R1, "RB->R1"},
        {XBS_BTN_L3, DS4_BUTTON_L3, "L3"}, {XBS_BTN_R3, DS4_BUTTON_R3, "R3"},
        {XBS_BTN_MENU, DS4_BUTTON_OPTIONS, "Menu->Options"}, {XBS_BTN_VIEW, DS4_BUTTON_SHARE, "View->Share"},
        {XBS_BTN_UP, DS4_BUTTON_DPAD_UP, "Up"}, {XBS_BTN_DOWN, DS4_BUTTON_DPAD_DOWN, "Down"},
        {XBS_BTN_LEFT, DS4_BUTTON_DPAD_LEFT, "Left"}, {XBS_BTN_RIGHT, DS4_BUTTON_DPAD_RIGHT, "Right"},
    };
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        d = tr(map[i].x, 0, 0, 0, 0, 0, 0, 12, 12, 0);
        CHECK(d.buttons == map[i].ps, "%s: got 0x%x want 0x%x", map[i].n, d.buttons, map[i].ps);
    }
    d = tr(XBS_BTN_VIEW, 0, 0, 0, 0, 0, 0, 12, 12, 1);
    CHECK(d.buttons == DS4_BUTTON_TOUCHPAD, "View->Touchpad option");

    /* all buttons at once: bits do not collide */
    d = tr(0xFFFF, 0, 0, 0, 0, 0, 0, 12, 12, 0);
    uint32_t want = 0;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) want |= map[i].ps;
    CHECK(d.buttons == want, "all buttons: 0x%x vs 0x%x", d.buttons, want);

    /* neutral pad from NULL input */
    XbsTranslateConfig cfg; xbs_translate_config_make(&cfg, 12, 12, 0);
    xbs_fill_pad_data(&d, NULL, &cfg, 0, 77);
    CHECK(d.connected == 0 && d.timestamp == 77 && d.leftStick.x == 128 && d.buttons == 0, "neutral/disconnected pad");

    /* config clamping */
    xbs_translate_config_make(&cfg, 999, 0, 5);
    CHECK(cfg.stick_deadzone_raw == (40 * 32767) / 100 && cfg.trigger_threshold8 >= 1 && cfg.view_to_touchpad == 1, "config clamp");
    xbs_translate_config_make(&cfg, -5, 500, 0);
    CHECK(cfg.stick_deadzone_raw == 0 && cfg.trigger_threshold8 == 255, "config clamp low/high");

    /* controller information */
    OrbisPadInformation info;
    xbs_fill_pad_info(&info, 1);
    CHECK(info.connected == 1 && info.connectionType == ORBIS_PAD_CONNECTION_TYPE_STANDARD &&
          info.deviceClass == ORBIS_PAD_DEVICE_CLASS_PAD && info.count == 1, "pad info connected");
    CHECK(info.touchResolutionX == 1920 && info.touchResolutionY == 942 && info.touchpadDensity > 44.0f, "touchpad info");
    xbs_fill_pad_info(&info, 0);
    CHECK(info.connected == 0, "pad info disconnected");
    CHECK(xbs_axis_deadzone(-32768, 0) == -32767, "deadzone of -32768 does not overflow");
    CHECK(xbs_axis_to_u8(-32767) == 0 && xbs_axis_to_u8(32767) == 255 && xbs_axis_to_u8(0) == 128, "axis to u8 ends");
}

static void test_settings(void) {
    printf("[settings]\n");
    XbsSettings s;
    settings_set_defaults(&s);
    CHECK(s.enabled == 1 && s.stick_deadzone_pct == 12 && s.trigger_threshold_pct == 12 &&
          s.view_to_touchpad == 0 && s.allow_foreground_user == 0 && s.player_mask == 0 &&
          s.guide_ack == 1 && s.notifications == 1, "defaults");

    settings_parse_text(&s,
        "# comment\n"
        "; another\n"
        "[section]\n"
        "stick_deadzone = 20   # trailing comment\n"
        "TRIGGER_THRESHOLD=50\n"
        "view_button=touchpad\n"
        "allow_player1=1\n"
        "xbox_players=2, 3\n"
        "guide_ack=0\n"
        "notifications=0\n"
        "log_to_file=0\n"
        "log_verbose=0\n"
        "enabled=1\n"
        "unknown_key=5\n"
        "garbage line without equals\n");
    CHECK(s.stick_deadzone_pct == 20, "deadzone %d", s.stick_deadzone_pct);
    CHECK(s.trigger_threshold_pct == 50, "threshold (case-insensitive key)");
    CHECK(s.view_to_touchpad == 1 && s.allow_foreground_user == 1, "view/allow");
    CHECK(s.player_mask == ((1u << 2) | (1u << 3)), "player mask 0x%x", s.player_mask);
    CHECK(s.guide_ack == 0 && s.notifications == 0 && s.log_to_file == 0 && s.log_verbose == 0, "flags");

    settings_parse_text(&s, "stick_deadzone=999\ntrigger_threshold=0\nxbox_players=\nstick_deadzone=abc\n");
    CHECK(s.stick_deadzone_pct == 40, "deadzone clamp (and 'abc' ignored), got %d", s.stick_deadzone_pct);
    CHECK(s.trigger_threshold_pct == 1, "threshold clamp");
    CHECK(s.player_mask == 0, "empty xbox_players = automatic");

    settings_parse_text(&s, "xbox_players=9,0,2\n");
    CHECK(s.player_mask == (1u << 2), "only 1..4 accepted, mask 0x%x", s.player_mask);

    settings_parse_text(&s, NULL);     /* must not crash */
    settings_parse_text(NULL, "a=b"); /* must not crash */
    CHECK(1, "NULL inputs safe");

    /* very long line must not overflow */
    char big[600];
    memset(big, 'a', sizeof(big) - 1); big[sizeof(big) - 1] = 0;
    settings_parse_text(&s, big);
    CHECK(1, "long line safe");
}

int main(void) {
    test_parser();
    test_translator();
    test_settings();
    printf("\ntest_core: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
