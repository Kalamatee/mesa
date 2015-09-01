/*
    Copyright © 2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#define DEBUG 1

#include <aros/debug.h>
#include <hidd/graphics.h>

#include <stdio.h>
#include <stdint.h>

#include "display.h"

#define PUSH_TAG(ptr, tag, data) do { (*(ptr))->ti_Tag = (tag); (*(ptr))->ti_Data= (IPTR)(data); (*ptr)++; } while(0)

/* Definitions used in CVT formula */
#define M 600
#define C 40
#define K 128
#define J 20
#define DUTY_CYCLE(period) \
    (((C - J) / 2 + J) * 1000 - (M / 2 * (period) / 1000))
#define MIN_DUTY_CYCLE 20 /* % */
#define MIN_V_PORCH 3 /* lines */
#define MIN_V_PORCH_TIME 550 /* us */
#define CLOCK_STEP 250000 /* Hz */

typedef struct {
    uint16_t width;
    uint16_t height;

    uint16_t hstart;
    uint16_t hend;
    uint16_t htotal;
    uint16_t vstart;
    uint16_t vend;
    uint16_t vtotal;

    uint32_t pixel;
} sync_t;

/* Partial implementation of CVT formula */
void calcTimings(int x, int y, int vfreq, sync_t *sync)
{
    ULONG h_period, h_freq, h_total, h_blank, h_front, h_sync, h_back,
          v_total, v_front, v_sync, v_back, duty_cycle, pixel_freq;

    sync->width = x;
    sync->height = y;

    /* Get horizontal period in microseconds */
    h_period = (1000000000 / vfreq - MIN_V_PORCH_TIME * 1000)
        / (y + MIN_V_PORCH);

    /* Vertical front porch is fixed */
    v_front = MIN_V_PORCH;

    /* Use aspect ratio to determine V-sync lines */
    if (x == y * 4 / 3)
        v_sync = 4;
    else if (x == y * 16 / 9)
        v_sync = 5;
    else if (x == y * 16 / 10)
        v_sync = 6;
    else if (x == y * 5 / 4)
        v_sync = 7;
    else if (x == y * 15 / 9)
        v_sync = 7;
    else
        v_sync = 10;

    /* Get vertical back porch */
    v_back = MIN_V_PORCH_TIME * 1000 / h_period + 1;
    if (v_back < MIN_V_PORCH)
        v_back = MIN_V_PORCH;
    v_back -= v_sync;

    /* Get total lines per frame */
    v_total = y + v_front + v_sync + v_back;

    /* Get horizontal blanking pixels */
    duty_cycle = DUTY_CYCLE(h_period);
    if (duty_cycle < MIN_DUTY_CYCLE)
        duty_cycle = MIN_DUTY_CYCLE;

    h_blank = 10 * x * duty_cycle / (100000 - duty_cycle);
    h_blank /= 2 * 8 * 10;
    h_blank = h_blank * (2 * 8);

    /* Get total pixels in a line */
    h_total = x + h_blank;

    /* Calculate frequencies for each pixel, line and field */
    h_freq = 1000000000 / h_period;
    pixel_freq = h_freq * h_total / CLOCK_STEP * CLOCK_STEP;
    h_freq = pixel_freq / h_total;

    /* Back porch is half of H-blank */
    h_back = h_blank / 2;

    /* H-sync is a fixed percentage of H-total */
    h_sync = h_total / 100 * 8;

    /* Front porch is whatever's left */
    h_front = h_blank - h_sync - h_back;

    /* Fill in VBE timings structure */
    sync->htotal = h_total;
    sync->hstart = x + h_front;
    sync->hend = h_total - h_back;
    sync->vtotal = v_total;
    sync->vstart = y + v_front;
    sync->vend = v_total - v_back;
    sync->pixel = pixel_freq;
}

static void createSync(APTR edidpool, int x, int y, int refresh, struct TagItem **tagsptr, struct TagItem **poolptr)
{
    sync_t sync;
    char *description = AllocVecPooled(edidpool, MAX_MODE_NAME_LEN + 1);
    snprintf(description, MAX_MODE_NAME_LEN, "GMA: %dx%d@%d", x, y, refresh);
    calcTimings(x, y, refresh, &sync);

    D(bug("[EDID]  %s: %s %d  %d %d %d %d  %d %d %d %d  -HSync +VSync\n", __PRETTY_FUNCTION__, description+5,
                    sync.pixel / 1000, sync.width, sync.hstart, sync.hend, sync.htotal,
                    sync.height, sync.vstart, sync.vend, sync.vtotal));

    PUSH_TAG(tagsptr, aHidd_Gfx_SyncTags, *poolptr);

    PUSH_TAG(poolptr, aHidd_Sync_Description, description);
    PUSH_TAG(poolptr, aHidd_Sync_PixelClock, sync.pixel);

    PUSH_TAG(poolptr, aHidd_Sync_HDisp, sync.width);
    PUSH_TAG(poolptr, aHidd_Sync_HSyncStart, sync.hstart);
    PUSH_TAG(poolptr, aHidd_Sync_HSyncEnd, sync.hend);
    PUSH_TAG(poolptr, aHidd_Sync_HTotal, sync.htotal);

    PUSH_TAG(poolptr, aHidd_Sync_VDisp, sync.height);
    PUSH_TAG(poolptr, aHidd_Sync_VSyncStart, sync.vstart);
    PUSH_TAG(poolptr, aHidd_Sync_VSyncEnd, sync.vend);
    PUSH_TAG(poolptr, aHidd_Sync_VTotal, sync.vtotal);

    PUSH_TAG(poolptr, aHidd_Sync_VMin, sync.height);
    PUSH_TAG(poolptr, aHidd_Sync_VMax, 4096);
    PUSH_TAG(poolptr, aHidd_Sync_HMin, sync.width);
    PUSH_TAG(poolptr, aHidd_Sync_HMax,  4096);
            
    PUSH_TAG(poolptr, aHidd_Sync_Flags, vHidd_Sync_VSyncPlus);
    PUSH_TAG(poolptr, TAG_DONE, 0);
}

void EDIDParse(APTR edidpool, UBYTE *edid_data, struct TagItem **tagsptr, struct TagItem *poolptr)
{
    uint8_t chksum = 0;
    char *description;
    int i;

    D(bug("[EDID] %s(0x%p)\n", __PRETTY_FUNCTION__, edid_data));

    for (i=0; i < 128; i++)
        chksum += edid_data[i];

    if (chksum == 0 &&
                    edid_data[0] == 0 && edid_data[1] == 0xff && edid_data[2] == 0xff && edid_data[3] == 0xff &&
                    edid_data[4] == 0xff && edid_data[5] == 0xff && edid_data[6] == 0xff && edid_data[7] == 0)
    {
        D(bug("[EDID] %s: Valid EDID%d.%d header\n", __PRETTY_FUNCTION__, edid_data[18], edid_data[19]));

        D(bug("[EDID] %s: Established timing: %02x %02x %02x\n", __PRETTY_FUNCTION__, edid_data[35], edid_data[36], edid_data[37]));

        if (edid_data[35] & 0x80)
            createSync(edidpool, 720, 400, 70, tagsptr, &poolptr);
        if (edid_data[35] & 0x40)
            createSync(edidpool, 720, 400, 88, tagsptr, &poolptr);
        if (edid_data[35] & 0x20)
            createSync(edidpool, 640, 480, 60, tagsptr, &poolptr);
        if (edid_data[35] & 0x10)
            createSync(edidpool, 640, 480, 67, tagsptr, &poolptr);
        if (edid_data[35] & 0x08)
            createSync(edidpool, 640, 480, 72, tagsptr, &poolptr);
        if (edid_data[35] & 0x04)
            createSync(edidpool, 640, 480, 75, tagsptr, &poolptr);
        if (edid_data[35] & 0x02)
            createSync(edidpool, 800, 600, 56, tagsptr, &poolptr);
        if (edid_data[35] & 0x01)
            createSync(edidpool, 800, 600, 60, tagsptr, &poolptr);

        if (edid_data[36] & 0x80)
            createSync(edidpool, 800, 600, 72, tagsptr, &poolptr);
        if (edid_data[36] & 0x40)
            createSync(edidpool, 800, 600, 75, tagsptr, &poolptr);
        if (edid_data[36] & 0x20)
            createSync(edidpool, 832, 624, 75, tagsptr, &poolptr);
#if (0)
        if (edid_data[36] & 0x10) /* interlaced */
            createSync(edidpool, 1024, 768, 87, tagsptr, &poolptr);
#endif
        if (edid_data[36] & 0x08)
            createSync(edidpool, 1024, 768, 60, tagsptr, &poolptr);
        if (edid_data[36] & 0x04)
            createSync(edidpool, 1024, 768, 70, tagsptr, &poolptr);
        if (edid_data[36] & 0x02)
            createSync(edidpool, 1024, 768, 75, tagsptr, &poolptr);
        if (edid_data[36] & 0x01)
            createSync(edidpool, 1280, 1024, 75, tagsptr, &poolptr);

        if (edid_data[37] & 0x01) /* Apple Mac II */
            createSync(edidpool, 1152, 870, 75, tagsptr, &poolptr);

        D(bug("[EDID] %s: Standard timing identification:\n", __PRETTY_FUNCTION__));

        for (i=38; i < 54; i+=2)
        {
            int w, h = 0, freq;
            w = edid_data[i] * 8 + 31;
            if (w > 256)
            {
                freq = (edid_data[i+1] & 0x3f) + 60;
                switch (edid_data[i+1] >> 6)
                {
                case 0: /* 16:10 */
                    h = (w * 10) / 16;
                    break;
                case 1: /* 4:3 */
                    h = (w * 3) / 4;
                    break;
                case 2: /* 5:4 */
                    h = (w * 4) / 5;
                    break;
                case 3: /* 16:9 */
                    h = (w * 9) / 16;
                    break;
                }
                createSync(edidpool, w, h, freq, tagsptr, &poolptr);
            }
        }

        for (i=54; i < 126; i+= 18)
        {
            if (edid_data[i] || edid_data[i+1])
            {
                int ha, hb, va, vb, hsync_o, hsync_w, vsync_o, vsync_w, pixel;
                ha = edid_data[i+2];
                hb = edid_data[i+3];
                ha |= (edid_data[i+4] >> 4) << 8;
                hb |= (edid_data[i+4] & 0x0f) << 8;
                va = edid_data[i+5];
                vb = edid_data[i+6];
                va |= (edid_data[i+7] >> 4) << 8;
                vb |= (edid_data[i+7] & 0x0f) << 8;
                hsync_o = edid_data[i+8];
                hsync_w = edid_data[i+9];
                vsync_o = edid_data[i+10] >> 4;
                vsync_w = edid_data[i+10] & 0x0f;
                hsync_o |= 0x300 & ((edid_data[i+11] >> 6) << 8);
                hsync_w |= 0x300 & ((edid_data[i+11] >> 4) << 8);
                vsync_o |= 0x30 & ((edid_data[i+11] >> 2) << 4);
                vsync_w |= 0x30 & ((edid_data[i+11]) << 4);

                pixel = (edid_data[i] | (edid_data[i+1] << 8));

                D(bug("[EDID] %s: Modeline: ", __PRETTY_FUNCTION__));
                D(bug("%dx%d Pixel: %d0 kHz %d %d %d %d   %d %d %d %d\n", ha, va, pixel,
                                ha, hb, hsync_o, hsync_w,
                                va, vb, vsync_o, vsync_w));

                description = AllocVecPooled(edidpool, MAX_MODE_NAME_LEN + 1);
                snprintf(description, MAX_MODE_NAME_LEN, "GMA: %dx%d@%d N",
                        ha, va, (int)(((pixel * 10 / (uint32_t)(ha + hb)) * 1000)
                        / ((uint32_t)(va + vb))));

                PUSH_TAG(tagsptr, aHidd_Gfx_SyncTags, poolptr);

                PUSH_TAG(&poolptr, aHidd_Sync_Description, description);
                PUSH_TAG(&poolptr, aHidd_Sync_PixelClock, pixel*10000);

                PUSH_TAG(&poolptr, aHidd_Sync_HDisp, ha);
                PUSH_TAG(&poolptr, aHidd_Sync_HSyncStart, ha+hsync_o);
                PUSH_TAG(&poolptr, aHidd_Sync_HSyncEnd, ha+hsync_o+hsync_w);
                PUSH_TAG(&poolptr, aHidd_Sync_HTotal, ha+hb);

                PUSH_TAG(&poolptr, aHidd_Sync_VDisp, va);
                PUSH_TAG(&poolptr, aHidd_Sync_VSyncStart, va+vsync_o);
                PUSH_TAG(&poolptr, aHidd_Sync_VSyncEnd, va+vsync_o+vsync_w);
                PUSH_TAG(&poolptr, aHidd_Sync_VTotal, va+vb);

                PUSH_TAG(&poolptr, aHidd_Sync_VMin, va);
                PUSH_TAG(&poolptr, aHidd_Sync_VMax, 4096);
                PUSH_TAG(&poolptr, aHidd_Sync_HMin, ha);
                PUSH_TAG(&poolptr, aHidd_Sync_HMax,  4096);

                PUSH_TAG(&poolptr, aHidd_Sync_Flags, vHidd_Sync_VSyncPlus);
                PUSH_TAG(&poolptr, TAG_DONE, 0);
            }
            else
            {
                switch (edid_data[i+3])
                {
                case 0xFF:
                    D(bug("[EDID] %s: Monitor Serial: %s\n", __PRETTY_FUNCTION__, &edid_data[i+5]));
                    break;

                case 0xFE:
                    D(bug("[EDID] %s: ASCII String: %s\n", __PRETTY_FUNCTION__, &edid_data[i+5]));
                    break;

                case 0xFD:
                    if (edid_data[i+10] == 0 && edid_data[i+11] == 0x0a)
                    {
                            D(bug("[EDID] %s: Monitor limits: H: %d - %d kHz, V: %d - %d Hz, PixelClock: %dMHz\n", __PRETTY_FUNCTION__,
                                            edid_data[i+7], edid_data[i+8], edid_data[i+5], edid_data[i+6], edid_data[i+9]*10));
                    }
                    break;

                case 0xFC:
                    D(bug("[EDID] %s: Monitor Name: %s\n", __PRETTY_FUNCTION__, &edid_data[i+5]));
                    break;

                case 0xFB:
                    D(bug("[EDID] %s: White Point: \n", __PRETTY_FUNCTION__));
                    break;

                case 0xFA:
                    D(bug("[EDID] %s: Standard Timing ID: %04x:%04x:%04x\n", __PRETTY_FUNCTION__,
                        (edid_data[i+5] << 8) | edid_data[i+6], (edid_data[i+8] << 8) | edid_data[i+9], (edid_data[i+11] << 8) | edid_data[i+12]));
                    break;

                default:
                    D(bug("[EDID] %s: Descriptor ID 0x%02x\n", __PRETTY_FUNCTION__, edid_data[i+3]));
                }
            }
        }

        D(bug("[EDID] %s: %d additional extensions available\n", __PRETTY_FUNCTION__, edid_data[126]));
    }
    else
        D(bug("[EDID] %s: Not a valid EDID block\n", __PRETTY_FUNCTION__));
}
