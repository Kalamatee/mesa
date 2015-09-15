/*
    Copyright © 2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#ifndef INTELGMA_BITMAP_H_
#define INTELGMA_BITMAP_H_

#ifndef INTELGMA_STATE_H_
#include "intelgma_state.h"
#endif

#define CLID_Hidd_BitMap_IntelGMA       "hidd.bitmap.intelgma"
#define IID_Hidd_BitMap_IntelGMA        "hidd.bitmap.intelgma"

extern OOP_AttrBase HiddBitMapIntelGMAAttrBase;

typedef struct HiddBitMapIntelGMAData {
    struct SignalSemaphore      bmLock;

    OOP_Object                  *display;        // cached display object
    OOP_Object                  *dmenum;

    OOP_Object                  *bitmap;        // BitMap Object
    intptr_t	                framebuffer;    // Points to pixel data
    uint16_t	                width;          // Bitmap width
    uint16_t	                height;         // Bitmap height
    uint16_t	                pitch;          // BytesPerRow aligned
    uint8_t		        depth;          // Bitmap depth
    uint8_t		        bpp;            // BytesPerPixel
    uint8_t		        onbm;           // is onbitmap?
    uint8_t		        fbgfx;          // is framebuffer in gfx memory
    uint64_t	                usecount;       // counts BitMap accesses

    GMAState_t                  *state;

    BOOL                        displayable;    /* Can bitmap be displayed on screen */

    /* Information connected with display */
#if defined(INTELGMA_COMPOSIT)
    OOP_Object                  *compositor;   /* Compositor object used by bitmap */
    LONG                        xoffset;        /* Offset to bitmap point that is displayed as (0,0) on screen */
    LONG                        yoffset;        /* Offset to bitmap point that is displayed as (0,0) on screen */
#endif
    ULONG                       fbid;           /* Contains ID under which bitmap is registered as framebuffer or 
                                                   0 otherwise */
} GMABitMap_t;

enum {
    aoHidd_BitMap_IntelGMA_Drawable,
#if defined(INTELGMA_COMPOSIT)
    aoHidd_BitMap_IntelGMA_CompositorHidd,
#endif
    num_Hidd_BitMap_IntelGMA_Attrs
};

#define aHidd_BitMap_IntelGMA_Drawable (HiddBitMapIntelGMAAttrBase + aoHidd_BitMap_IntelGMA_Drawable)
#if defined(INTELGMA_COMPOSIT)
#define aHidd_BitMap_IntelGMA_CompositorHidd    (HiddBitMapIntelGMAAttrBase + aoHidd_BitMap_IntelGMA_CompositorHidd)
#endif

#define IS_BM_ATTR(attr, idx) (((idx)=(attr)-HiddBitMapAttrBase) < num_Hidd_BitMap_Attrs)
#define IS_BitMapIntelGMA_ATTR(attr, idx) (((idx)=(attr)-HiddBitMapIntelGMAAttrBase) < num_Hidd_BitMap_IntelGMA_Attrs)

#define LOCK_BITMAP      { ObtainSemaphore(&bm->bmLock); }
#define UNLOCK_BITMAP        { ReleaseSemaphore(&bm->bmLock); }

#define LOCK_BITMAP_BM(bm)   { ObtainSemaphore(&(bm)->bmLock); }
#define UNLOCK_BITMAP_BM(bm) { ReleaseSemaphore(&(bm)->bmLock); }

#define LOCK_MULTI_BITMAP    { ObtainSemaphore(&sd->MultiBMLock); }
#define UNLOCK_MULTI_BITMAP  { ReleaseSemaphore(&sd->MultiBMLock); }

#endif /* INTELGMA_BITMAP_H_ */
