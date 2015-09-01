#ifndef _COMPOSITOR_INTERN_H
#define _COMPOSITOR_INTERN_H
/*
    Copyright © 2010-2015, The AROS Development Team. All rights reserved.
    $Id: compositing_intern.h 38464 2011-05-01 13:12:36Z sonic $
*/

#include <exec/lists.h>

struct _Rectangle
{
    WORD MinX;
    WORD MinY;
    WORD MaxX;
    WORD MaxY;
};

struct StackBitMapNode
{
    struct Node         n;
    OOP_Object *        bm;
    struct _Rectangle   screenvisiblerect;
    BOOL                isscreenvisible;
    LONG                displayedwidth;
    LONG                displayedheight;
};

struct HIDDCompositorData
{
    OOP_Object              *screenbitmap;
	OOP_Object              *directbitmap; 
    HIDDT_ModeID            screenmodeid;
    struct _Rectangle       screenrect;

    struct List             bitmapstack;
    
    struct SignalSemaphore  semaphore;
    
    OOP_Object              *gfx;           /* GFX driver object */
    OOP_Object              *gc;            /* GC object used for drawing operations */
};

extern const struct OOP_InterfaceDescr Compositor_ifdescr[];

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)
  
//#define SD(cl)                      (&BASE(cl->UserData)->sd)

#define LOCK_COMPOSITING_READ       { ObtainSemaphoreShared(&compdata->semaphore); }
#define LOCK_COMPOSITING_WRITE      { ObtainSemaphore(&compdata->semaphore); }
#define UNLOCK_COMPOSITING          { ReleaseSemaphore(&compdata->semaphore); }

#endif /* _COMPOSITOR_INTERN_H */
