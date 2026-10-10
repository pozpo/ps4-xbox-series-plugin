/*
 * Xbox Series X|S -> PS4 pad data translator.
 *
 * Pure conversion code: no USB, no threads, no hooks. Give it an XbsInput and
 * it fills an OrbisPadData (the structure PS4 games read).
 */

#ifndef TRANSLATOR_H
#define TRANSLATOR_H

#include <stdint.h>
#include "xbox_series.h"
#include "ds4.h"

typedef struct {
    int     stick_deadzone_raw;   /* dead zone in raw stick units (0..13107) */
    uint8_t trigger_threshold8;   /* 0..255: L2/R2 digital press point       */
    int     view_to_touchpad;     /* 0: View->Share, 1: View->Touchpad click */
} XbsTranslateConfig;

/* Builds a translator configuration from percentages (values are clamped). */
void xbs_translate_config_make(XbsTranslateConfig* cfg, int stick_deadzone_pct,
                               int trigger_threshold_pct, int view_to_touchpad);

/* Fills *d completely.
 *   in == NULL      -> neutral pad (sticks centred, nothing pressed)
 *   connected       -> value for the 'connected' field
 *   timestamp       -> value for the 'timestamp' field (microseconds)        */
void xbs_fill_pad_data(OrbisPadData* d, const XbsInput* in,
                       const XbsTranslateConfig* cfg, int connected,
                       uint64_t timestamp);

/* Fills *info with the controller-information a DualShock 4 would report. */
void xbs_fill_pad_info(OrbisPadInformation* info, int connected);

/* Helpers exposed for the unit tests. */
int     xbs_axis_deadzone(int value, int deadzone);   /* -32768..32767 in/out */
uint8_t xbs_axis_to_u8(int value);                    /* -32767..32767 -> 0..255 */

#endif /* TRANSLATOR_H */
