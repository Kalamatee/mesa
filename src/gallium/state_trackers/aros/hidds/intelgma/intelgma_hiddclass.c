/*
    Copyright © 1995-2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#define DEBUG 1
#include <aros/debug.h>

#include <proto/oop.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <utility/tagitem.h>
#include <hidd/gfx.h>
#if defined(INTELGMA_COMPOSIT)
#include <hidd/compositor.h>
#endif
#include <hidd/gallium.h>
#include <hidd/i2c.h>
#include <graphics/displayinfo.h>

#include <stdio.h>
#include <stdint.h>

#include "intelgma_hidd.h"
#include "intelgma_gallium.h"
#include "intelG45_regs.h"

#define sd ((struct g45staticdata*)SD(cl))
extern struct UtilityBase *UtilityBase;

OOP_Object *METHOD(IntelGMA, Root, New)
{
    struct TagItem new_tags[] =
    {
        {aHidd_Name,    (IPTR)"intelgma.hidd"  },
        {TAG_DONE,      0               }
    };
    struct pRoot_New new_msg =
    {
        .mID      = msg->mID,
        .attrList = new_tags
    };
    D(bug("[IntelGMA:HW] %s()\n", __PRETTY_FUNCTION__));

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)&new_msg);
    {
        struct HiddGfxIntelGMAData *data = OOP_INST_DATA(cl, o);
        struct TagItem displaytags[] =
        {
            {aHidd_Display_GfxHidd,      (IPTR)o },
            {TAG_DONE,                  0       }
        };

        data->gma_display = OOP_NewObject(sd->displayclass, NULL, displaytags);
        if (data->gma_display)
        {
#if defined(INTELGMA_COMPOSIT)
        /* Create compositor object */
        {
            struct TagItem comptags [] =
            {
                { aHidd_Compositor_GfxHidd, (IPTR)o },
                { TAG_DONE, TAG_DONE }
            };
            sd->compositor = OOP_NewObject(sd->compositorclass, NULL, comptags);
            /* TODO: Check if object was created, how to handle ? */
        }
#endif
        }
        else
        {
            //OOP_MethodID dispose_mid = XSD(cl)->mid_Dispose;
            //OOP_CoerceMethod(cl, o, &dispose_mid);
            o = NULL;
        }
    }

    D(bug("[IntelGMA:HW] %s: object @ 0x%p\n", __PRETTY_FUNCTION__, o));

    return o;
}

void METHOD(IntelGMA, Root, Get)
{
    struct HiddGfxIntelGMAData *data = OOP_INST_DATA(cl, o);
    ULONG idx;

    Hidd_Gfx_Switch (msg->attrID, idx)
    {
    case aoHidd_Gfx_SupportsHWCursor:
    case aoHidd_Gfx_NoFrameBuffer:
        *msg->storage = (IPTR)TRUE;
        return;

     case aoHidd_Gfx_DisplayDefault:
        *msg->storage = (IPTR)data->gma_display;
        return;
     
    case aoHidd_Gfx_MemoryAttribs:
        {
            struct TagItem *memTags = (struct TagItem *)msg->storage, *memTag;
            while ((memTag = NextTagItem(&memTags)))
            {
                switch (memTag->ti_Tag)
                {
                case vHidd_Gfx_MemTotal:
                    memTag->ti_Tag = sd->Card.Framebuffer_size;
                case vHidd_Gfx_MemAddressableTotal:
                    memTag->ti_Tag = sd->Card.GATT_size;
                    break;
                case vHidd_Gfx_MemAddressableFree:
                case vHidd_Gfx_MemFree:
                case vHidd_Gfx_MemClock:
                    memTag->ti_Tag = 0;
                    break;
                }
            }
        }
        return;
    }
    Hidd_Switch (msg->attrID, idx)
    {
    case aoHidd_HardwareName:
        *msg->storage = (IPTR)"IntelGMA Display Adaptor";
        return;
    }
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

BOOL HIDD_IntelGMA_SwitchToVideoMode(OOP_Object * bm)
{
    OOP_Class * cl = OOP_OCLASS(bm);
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, bm);
    OOP_Object *display, *dmenum, *sync, *pf;
    HIDDT_ModeID modeid;
    IPTR pixel;
    IPTR hdisp, vdisp, hstart, hend, htotal, vstart, vend, vtotal;

    D(bug("[IntelGMA:HW] %s()\n", __PRETTY_FUNCTION__));

    D(bug("[IntelGMA:HW] HIDD_IntelGMA_SwitchToVideoMode bitmap:%x\n",bmdata));

    /* We should be able to get modeID from the bitmap */
    OOP_GetAttr(bm, aHidd_BitMap_ModeID, &modeid);

    if (modeid == vHidd_ModeID_Invalid)
    {
        D(bug("[IntelGMA:HW] Invalid ModeID\n"));
        return FALSE;
    }

    OOP_GetAttr(bm, aHidd_BitMap_Display, (IPTR *)&display);
    OOP_GetAttr(display, aHidd_Display_DMEnumerator, &dmenum);

    /* Get Sync and PixelFormat properties */
    struct pHidd_DMEnum_GetMode __getmodemsg = 
    {
        modeID:	modeid,
        syncPtr:	&sync,
        pixFmtPtr:	&pf,
    }, *getmodemsg = &__getmodemsg;

    getmodemsg->mID = sd->mid_GetMode;
    OOP_DoMethod(dmenum, (OOP_Msg)getmodemsg);

    OOP_GetAttr(sync, aHidd_Sync_PixelClock,    &pixel);
    OOP_GetAttr(sync, aHidd_Sync_HDisp,         &hdisp);
    OOP_GetAttr(sync, aHidd_Sync_VDisp,         &vdisp);
    OOP_GetAttr(sync, aHidd_Sync_HSyncStart,    &hstart);
    OOP_GetAttr(sync, aHidd_Sync_VSyncStart,    &vstart);
    OOP_GetAttr(sync, aHidd_Sync_HSyncEnd,      &hend);
    OOP_GetAttr(sync, aHidd_Sync_VSyncEnd,      &vend);
    OOP_GetAttr(sync, aHidd_Sync_HTotal,        &htotal);
    OOP_GetAttr(sync, aHidd_Sync_VTotal,        &vtotal);    

    D(bug("[IntelGMA:HW] Sync: %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
    pixel, hdisp, hstart, hend, htotal, vdisp, vstart, vend, vtotal));

    if (bmdata->state && sd->VisibleBitmap != bmdata)
    {
        /* Suppose bm has properly allocated state structure */
        if (bmdata->fbgfx)
        {
            bmdata->usecount++;
            SetCursorPosition(sd,0,0);
            LOCK_HW
            {
                sd->VisibleBitmap = bmdata;
                G45_LoadState(sd, bmdata->state);
            }
            UNLOCK_HW
            SetCursorPosition(sd,sd->pointerx,sd->pointery);
            return TRUE;
        }
    }
    
    return FALSE;     
}

BOOL HIDD_IntelGMA_SetFramebuffer(OOP_Object * bm)
{
    OOP_Class * cl = OOP_OCLASS(bm);
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, bm);

    D(bug("[IntelGMA:HW] %s()\n", __PRETTY_FUNCTION__));

    //bug("[IntelGMA:HW] HIDD_IntelGMA_SetFramebuffer %x %d,%d\n",bmdata,bmdata->xoffset,bmdata->yoffset);
    if (bmdata->fbgfx)
    {
        char *linoff_reg = sd->Card.MMIO + ((sd->pipe == PIPE_A) ? G45_DSPALINOFF : G45_DSPBLINOFF);
        char *stride_reg = sd->Card.MMIO + ((sd->pipe == PIPE_A) ? G45_DSPASTRIDE : G45_DSPBSTRIDE);
        IPTR xoffset, yoffset;

        OOP_GetAttr(bm, aHidd_BitMap_LeftEdge, &xoffset);
        OOP_GetAttr(bm, aHidd_BitMap_TopEdge, &yoffset);

        // bitmap width in bytes
        writel( bmdata->state->dspstride , stride_reg );

        // framebuffer address + possible xy offset  
        writel(	bmdata->framebuffer - ( yoffset * bmdata->pitch +
                                                                        xoffset * bmdata->bpp ) ,linoff_reg );
        readl( linoff_reg );	
        return TRUE;
    }
    //bug("[IntelGMA:HW] HIDD_IntelGMA_SetFramebuffer: not Framebuffer Bitmap!\n");
    return FALSE;
}

static const struct OOP_MethodDescr IntelGMA_Root_descr[] =
{
    {(OOP_MethodFunc)IntelGMA__Root__New,                       moRoot_New                      },
    {(OOP_MethodFunc)IntelGMA__Root__Get,                       moRoot_Get                      },
    {NULL,                                                      0                               }
};
#define NUM_IntelGMA_Root_METHODS 2

const struct OOP_InterfaceDescr IntelGMA_ifdescr[] =
{
    {IntelGMA_Root_descr,               IID_Root,               NUM_IntelGMA_Root_METHODS       },
    {NULL,                              NULL,                   0				}
};
