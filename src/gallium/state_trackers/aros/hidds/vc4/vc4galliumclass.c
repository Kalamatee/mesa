/*
    Copyright 2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#include <hidd/gallium.h>
#include <proto/oop.h>
#include <proto/utility.h>
#include <aros/debug.h>

#include <gallium/gallium.h>

#include "pipe/p_screen.h"
#include "vc4/vc4_screen.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "state_tracker/sw_winsys.h"

#include "vc4_intern.h"

#include <proto/cybergraphics.h>
#include <proto/graphics.h>
#include <cybergraphx/cybergraphics.h>

#if (AROS_BIG_ENDIAN == 1)
#define AROS_PIXFMT RECTFMT_RAW   /* Big Endian Archs. */
#else
#define AROS_PIXFMT RECTFMT_BGRA32   /* Little Endian Archs. */
#endif

#define CyberGfxBase    (&BASE(cl->UserData)->sd)->VC4CyberGfxBase

#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase   (SD(cl)->hiddGalliumAB)

struct HIDDVC4Data
{
};

/*  Displaytarget support code */
struct HiddVC4Displaytarget
{
    APTR data;
};

struct HiddVC4Displaytarget * HiddVC4Displaytarget(struct sw_displaytarget * dt)
{
    return (struct HiddVC4Displaytarget *)dt;
}

static struct sw_displaytarget *
HiddVC4CreateDisplaytarget(struct sw_winsys *winsys, unsigned tex_usage,
    enum pipe_format format, unsigned width, unsigned height,
    unsigned alignment, unsigned *stride)
{
    struct HiddVC4Displaytarget * spdt = 
        AllocVec(sizeof(struct HiddVC4Displaytarget), MEMF_PUBLIC | MEMF_CLEAR);
    
    *stride = align(util_format_get_stride(format, width), alignment);
    spdt->data = AllocVec(*stride * height, MEMF_PUBLIC | MEMF_CLEAR);
    
    return (struct sw_displaytarget *)spdt;
}

static void
HiddVC4DestroyDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
    struct HiddVC4Displaytarget * spdt = HiddVC4Displaytarget(dt);
    
    if (spdt)
    {
        FreeVec(spdt->data);
        FreeVec(spdt);
    }
}

static void *
HiddVC4MapDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt,
    unsigned flags)
{
    struct HiddVC4Displaytarget * spdt = HiddVC4Displaytarget(dt);
    return spdt->data;
}

static void
HiddVC4UnMapDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
    /* No op */
}

/*  Displaytarget support code ends */

OOP_Object *METHOD(VC4Gallium, Root, New)
{
    IPTR interfaceVers;

    D(bug("[SoftPipe] %s()\n", __PRETTY_FUNCTION__));

    interfaceVers = GetTagData(aHidd_Gallium_InterfaceVersion, -1, msg->attrList);
    if (interfaceVers != GALLIUM_INTERFACE_VERSION)
        return NULL;

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    if (o)
    {
        struct HiddGalliumVC4Data * HiddVC4_DATA = OOP_INST_DATA(cl, o);

        HiddSoftpipe_DATA->vc4_obj = o;

#if (0)
        HiddVC4_DATA->vc4_ws.destroy                            = NULL;
        HiddVC4_DATA->vc4_ws.is_displaytarget_format_supported  = NULL;
        HiddVC4_DATA->vc4_ws.displaytarget_create               = HiddVC4CreateDisplaytarget;
        HiddVC4_DATA->vc4_ws.displaytarget_from_handle          = NULL;
        HiddVC4_DATA->vc4_ws.displaytarget_get_handle           = NULL;
        HiddVC4_DATA->vc4_ws.displaytarget_map                  = HiddVC4MapDisplaytarget;
        HiddVC4_DATA->vc4_ws.displaytarget_unmap                = HiddVC4UnMapDisplaytarget;
        HiddVC4_DATA->vc4_ws.displaytarget_display              = NULL;
        HiddVC4_DATA->vc4_ws.displaytarget_destroy              = HiddVC4DestroyDisplaytarget;
#endif
    }

    return o;
}

VOID METHOD(VC4Gallium, Root, Get)
{
    struct HIDDVC4Data * HIDDVC4_Data = OOP_INST_DATA(cl, o);
    ULONG idx;

    if (IS_GALLIUM_ATTR(msg->attrID, idx))
    {
        switch (idx)
        {
            /* Overload the property */
            case aoHidd_Gallium_InterfaceVersion:
                *msg->storage = GALLIUM_INTERFACE_VERSION;
                return;
        }
    }

    /* Use parent class for all other properties */
    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

APTR METHOD(VC4Gallium, Hidd_Gallium, CreatePipeScreen)
{
    struct HIDDVC4Data * HIDDVC4_Data = OOP_INST_DATA(cl, o);
    struct pipe_screen *screen = NULL;
    struct sw_winsys *vc4_ws = NULL;

#if (0)
    struct HiddVC4WinSys * vc4ws;

    vc4ws = HiddVC4CreateVC4WinSys();
    if (vc4ws == NULL)
        return NULL;
#endif

    OOP_GetAttr(o, aHidd_Gallium_WinSys, &vc4_ws);

    if (vc4_ws)
    {
        screen = vc4_create_screen(vc4_ws);
        if (screen)
        {
#if (0)
        /* Force a pipe_winsys pointer (Mesa 7.9 or never) */
        screen->winsys = (struct pipe_winsys *)vc4ws;

        /* Preserve pointer to HIDD driver */
        vc4ws->base.driver = o;
#endif
        }
    }

    return screen;

}

VOID METHOD(VC4Gallium, Hidd_Gallium, DisplayResource)
{
    struct vc4_resource * spr = vc4_resource(msg->resource);
    struct RastPort * rp;
    struct sw_winsys * vc4_ws = NULL;
    APTR * data = spr->data;

    OOP_GetAttr(o, aHidd_Gallium_WinSys, &vc4_ws);

    if (vc4_ws)
    {
        if ((data == NULL) && (spr->dt != NULL))
            data = vc4_ws->displaytarget_map(vc4_ws, spr->dt, 0);

        if (data)
        {
            rp = CreateRastPort();

            rp->BitMap = msg->bitmap;

            WritePixelArray(
                data, 
                msg->srcx,
                msg->srcy,
                spr->stride[0],
                rp, 
                msg->dstx, 
                msg->dsty, 
                msg->width, 
                msg->height, 
                AROS_PIXFMT);

            FreeRastPort(rp);
        }

        if ((spr->data == NULL) && (data != NULL))
            vc4_ws->displaytarget_unmap(vc4_ws, spr->dt);
    }
}
