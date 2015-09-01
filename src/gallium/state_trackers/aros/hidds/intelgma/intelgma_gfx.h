/*
    Copyright © 2010-2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#ifndef INTELGMA_GFX_H_
#define INTELGMA_GFX_H_

#include <exec/nodes.h>
#include <exec/semaphores.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <oop/oop.h>
#include <hidd/graphics.h>
#include <hidd/pci.h>
#include <hidd/i2c.h>

#include <stdint.h>

#ifndef INTELGMA_BITMAP_H_
#include "intelgma_bitmap.h"
#endif

#ifndef INTELGMA_I2C_H_
#include "intelgma_i2c.h"
#endif

#define CLID_Hidd_Gfx_IntelGMA		"hidd.gfx.intelgma"
#define IID_Hidd_Gfx_IntelGMA		"hidd.gfx.intelgma"

extern OOP_AttrBase HiddGfxIntelGMAAttrBase;

#define KBYTES                          1024
#define MBYTES                          (1024 * 1024)

struct Sync{
    uint16_t                            width;
    uint16_t                            height;
    uint8_t                             depth;
    uint32_t                            pixelclock;
    intptr_t                            framebuffer;
    uint16_t                            hdisp;
    uint16_t                            vdisp;
    uint16_t                            hstart;
    uint16_t                            hend;
    uint16_t                            htotal;
    uint16_t                            vstart;
    uint16_t                            vend;
    uint16_t                            vtotal;
    uint32_t                            flags;
};

typedef struct {
    uint8_t		                h_min;		/* Minimal horizontal frequency in kHz */
    uint8_t		                h_max;		/* Maximal horizontal frequency in kHz */
    uint8_t		                v_min;		/* Minimal vertical frequency in Hz */
    uint8_t		                v_max;		/* Maximal vertical frequency in Hz */
    uint8_t		                pixel_max;	/* Maximal pixelclock/10 in MHz (multiply by 10 to get MHz value) */
} GMA_MonitorSpec_t;

struct g45chip {
    char *			        Framebuffer;
    uint32_t		                Framebuffer_size;
    char *			        MMIO;
    uint32_t *		                GATT;
    uint32_t		                GATT_size;
    uint32_t		                Stolen_size;
};

struct HiddGfxIntelGMAData
{
    APTR gma_GalliumData;

    /* TODO: Move driver instance data here from staticdata */
};

struct g45staticdata
{
    void *MemPool;

    IPTR flags;
#define SDFLAG_INITIALISED (1 << 0)
    BOOL forced;
    BOOL force_gallium;

    /* The rest should be moved to object data */
    struct SignalSemaphore	HWLock;
    struct SignalSemaphore	MultiBMLock;
    struct MsgPort		MsgPort;
    struct timerequest		*tr;

    struct MinList CardMem;

    struct g45chip			Card;
    uint16_t				ProductID;

    HIDDT_DPMSLevel			dpms;

    GMABitMap_t *			Engine2DOwner;

    GMABitMap_t *			VisibleBitmap;

    GMAState_t *			initialState;
    intptr_t				initialBitMap;

    intptr_t				RingBuffer;
    uint32_t				RingBufferSize;
    uint32_t				RingBufferTail;
    char *					RingBufferPhys;
    char					RingActive;

    uint32_t				DDCPort;

    OOP_Class *				IntelGMAClass;
    OOP_Class *				IntelI2C;
    OOP_Class *				BMClass;
#if defined(INTELGMA_COMPOSIT)
    OOP_Class *				compositorclass;
#endif
    OOP_Class *				galliumclass;

#if defined(INTELGMA_COMPOSIT)
    OOP_Object          *compositor;
#endif
    OOP_Object *			PCIObject;
    OOP_Object *			PCIDevice;

    intptr_t				ScratchArea;
    intptr_t				AttachedMemory;
    intptr_t				AttachedSize;

    volatile uint32_t *	HardwareStatusPage;

    intptr_t				CursorImage; /* offset into sd->card.framebuffer for hardware pointer */
    intptr_t				CursorBase;
    BOOL					CursorVisible;

    /* baseclass for CreateObject */
    OOP_Class *basegc;
    OOP_Class *basebm;
    OOP_Class *basegallium;
    
    /* cached method ID's */
    OOP_MethodID    mid_Dispose;
    OOP_MethodID    mid_GetMode;

    OOP_MethodID    mid_ReadLong;
    OOP_MethodID    mid_CopyMemBox8;
    OOP_MethodID    mid_CopyMemBox16;
    OOP_MethodID    mid_CopyMemBox32;
    OOP_MethodID    mid_PutMem32Image8;
    OOP_MethodID    mid_PutMem32Image16;
    OOP_MethodID    mid_GetMem32Image8;
    OOP_MethodID    mid_GetMem32Image16;
    OOP_MethodID    mid_GetImage;
    OOP_MethodID    mid_Clear;
    OOP_MethodID    mid_PutMemTemplate8;
    OOP_MethodID    mid_PutMemTemplate16;
    OOP_MethodID    mid_PutMemTemplate32;
    OOP_MethodID    mid_PutMemPattern8;
    OOP_MethodID    mid_PutMemPattern16;
    OOP_MethodID    mid_PutMemPattern32;
    OOP_MethodID    mid_CopyLUTMemBox16;
    OOP_MethodID    mid_CopyLUTMemBox32;

#if defined(INTELGMA_COMPOSIT)
    OOP_MethodID    mid_BitMapPositionChanged;
    OOP_MethodID    mid_BitMapRectChanged;
    OOP_MethodID    mid_ValidateBitMapPositionChange;
#endif

    ULONG pipe;
    struct Sync lvds_fixed;
    LONG pointerx;
    LONG pointery;
};

enum
{
    aoHidd_Gfx_IntelGMA_GalliumData = 0,

    num_Hidd_Gfx_IntelGMA_Attrs
};

#define aHidd_Gfx_IntelGMA_GalliumData (HiddBitMapIntelGMAAttrBase + aoHidd_Gfx_IntelGMA_GalliumData)

#define IS_INTELGMA_ATTR(attr, idx) \
    (((idx) = (attr) - HiddGfxIntelGMAAttrBase) < num_Hidd_Gfx_IntelGMA_Attrs)

#define SD(cl) ((struct g45staticdata *)cl->UserData)

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define METHOD_NAME(base, id, name) \
  base ## __ ## id ## __ ## name

#define METHOD_NAME_S(base, id, name) \
  # base "__" # id "__" # name

#define LOCK_HW          { ObtainSemaphore(&sd->HWLock); }
#define UNLOCK_HW        { ReleaseSemaphore(&sd->HWLock); }

extern const struct OOP_InterfaceDescr IntelGMA_ifdescr[];
extern const struct OOP_InterfaceDescr BitMapIntelGMA_ifdescr[];
extern const struct OOP_InterfaceDescr INTELI2C_ifdescr[];

int G45_Init(struct g45staticdata *sd);

APTR AllocGfxMem(struct g45staticdata *sd, ULONG size);
VOID FreeGfxMem(struct g45staticdata *sd, APTR ptr, ULONG size);
intptr_t G45_VirtualToPhysical(struct g45staticdata *sd, intptr_t virtual);
struct MemHeader *ObtainGfxMemory(struct g45staticdata *sd, intptr_t virtual,
    intptr_t length, BOOL stolen);
void ReleaseGfxMemory(struct g45staticdata *sd, struct MemHeader *header);
void G45_AttachCacheableMemory(struct g45staticdata *sd, intptr_t physical, intptr_t virtual, intptr_t length);

void G45_InitMode(struct g45staticdata *sd, GMAState_t *state,
		uint16_t width, uint16_t height, uint8_t depth, uint32_t pixelclock, intptr_t framebuffer,
        uint16_t hdisp, uint16_t vdisp, uint16_t hstart, uint16_t hend, uint16_t htotal,
        uint16_t vstart, uint16_t vend, uint16_t vtotal, uint32_t flags);
void G45_LoadState(struct g45staticdata *sd, GMAState_t *state);
IPTR AllocBitmapArea(struct g45staticdata *sd, ULONG width, ULONG height, ULONG bpp, BOOL clear);
VOID FreeBitmapArea(struct g45staticdata *sd, IPTR bmp, ULONG width, ULONG height, ULONG bpp);
VOID delay_ms(struct g45staticdata *sd, uint32_t msec);
VOID delay_us(struct g45staticdata *sd, uint32_t usec);

BOOL adpa_Enabled(struct g45staticdata *sd);
BOOL lvds_Enabled(struct g45staticdata *sd);
void GetSync(struct g45staticdata *sd,struct Sync *sync,ULONG pipe);
void UpdateCursor(struct g45staticdata *sd);
void SetCursorPosition(struct g45staticdata *sd,LONG x,LONG y);

BOOL HIDD_IntelGMA_SetFramebuffer(OOP_Object * bm);
BOOL HIDD_IntelGMA_SwitchToVideoMode(OOP_Object * bm);
BOOL copybox3d_supported();
BOOL copybox3d( GMABitMap_t *bm_dst, GMABitMap_t *bm_src,
               ULONG dst_x,ULONG dst_y,ULONG dst_width, ULONG dst_height,
               ULONG src_x,ULONG src_y,ULONG src_width, ULONG src_height );

//#define GALLIUM_SIMULATION             
#endif /* INTELGMA_GFX_H_ */
