/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: vmwaresvgagalliumclass.c 50414 2015-04-18 11:21:09Z wawa $
*/

#include <hidd/gallium.h>
#include <proto/oop.h>
#include <aros/debug.h>

#include <gallium/gallium.h>

#include "pipe/p_screen.h"
#include "vmwaresvga/sp_texture.h"
#include "vmwaresvga/sp_public.h"
#include "vmwaresvga/sp_screen.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "state_tracker/sw_winsys.h"

#include "vmwaresvga_intern.h"

#include <proto/cybergraphics.h>
#include <proto/graphics.h>
#include <cybergraphx/cybergraphics.h>

#if (AROS_BIG_ENDIAN == 1)
#define AROS_PIXFMT RECTFMT_RAW   /* Big Endian Archs. */
#else
#define AROS_PIXFMT RECTFMT_BGRA32   /* Little Endian Archs. */
#endif

#define CyberGfxBase    (&BASE(cl->UserData)->sd)->VMWareSVGACyberGfxBase

#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase   (SD(cl)->hiddGalliumAB)

struct HIDDVMWareSVGAData
{
};

/*  Displaytarget support code */
struct HiddVMWareSVGADisplaytarget
{
    APTR data;
};

struct HiddVMWareSVGADisplaytarget * HiddVMWareSVGADisplaytarget(struct sw_displaytarget * dt)
{
    return (struct HiddVMWareSVGADisplaytarget *)dt;
}

static struct sw_displaytarget *
HiddVMWareSVGACreateDisplaytarget(struct sw_winsys *winsys, unsigned tex_usage,
    enum pipe_format format, unsigned width, unsigned height,
    unsigned alignment, unsigned *stride)
{
    struct HiddVMWareSVGADisplaytarget * spdt = 
        AllocVec(sizeof(struct HiddVMWareSVGADisplaytarget), MEMF_PUBLIC | MEMF_CLEAR);
    
    *stride = align(util_format_get_stride(format, width), alignment);
    spdt->data = AllocVec(*stride * height, MEMF_PUBLIC | MEMF_CLEAR);
    
    return (struct sw_displaytarget *)spdt;
}

static void
HiddVMWareSVGADestroyDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
    struct HiddVMWareSVGADisplaytarget * spdt = HiddVMWareSVGADisplaytarget(dt);
    
    if (spdt)
    {
        FreeVec(spdt->data);
        FreeVec(spdt);
    }
}

static void *
HiddVMWareSVGAMapDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt,
    unsigned flags)
{
    struct HiddVMWareSVGADisplaytarget * spdt = HiddVMWareSVGADisplaytarget(dt);
    return spdt->data;
}

static void
HiddVMWareSVGAUnMapDisplaytarget(struct sw_winsys *ws, struct sw_displaytarget *dt)
{
    /* No op */
}

/*  Displaytarget support code ends */

OOP_Object *METHOD(VMWareSVGAGallium, Root, New)
{
    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg) msg);

    if (0)
    {
        struct HIDDVMWareSVGAData * HIDDVMWareSVGA_Data = OOP_INST_DATA(cl, o);
        struct sw_winsys *vmwaresvga_ws = NULL;

        OOP_GetAttr(o, aHidd_Gallium_WinSys, &vmwaresvga_ws);

        if (vmwaresvga_ws)
        {
            /* Fill in with functions is displaytarget is ever used*/
            vmwaresvga_ws->destroy                            = NULL;
            vmwaresvga_ws->is_displaytarget_format_supported  = NULL;
            vmwaresvga_ws->displaytarget_create               = HiddVMWareSVGACreateDisplaytarget;
            vmwaresvga_ws->displaytarget_from_handle          = NULL;
            vmwaresvga_ws->displaytarget_get_handle           = NULL;
            vmwaresvga_ws->displaytarget_map                  = HiddVMWareSVGAMapDisplaytarget;
            vmwaresvga_ws->displaytarget_unmap                = HiddVMWareSVGAUnMapDisplaytarget;
            vmwaresvga_ws->displaytarget_display              = NULL;
            vmwaresvga_ws->displaytarget_destroy              = HiddVMWareSVGADestroyDisplaytarget;
        }
    }

    return o;
}

VOID METHOD(VMWareSVGAGallium, Root, Get)
{
    struct HIDDVMWareSVGAData * HIDDVMWareSVGA_Data = OOP_INST_DATA(cl, o);
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

APTR METHOD(VMWareSVGAGallium, Hidd_Gallium, CreatePipeScreen)
{
    struct HIDDVMWareSVGAData * HIDDVMWareSVGA_Data = OOP_INST_DATA(cl, o);
    struct pipe_screen *screen = NULL;
    struct sw_winsys *vmwaresvga_ws = NULL;

#if (0)
    struct HiddVMWareSVGAWinSys * vmwaresvgaws;

    vmwaresvgaws = HiddVMWareSVGACreateVMWareSVGAWinSys();
    if (vmwaresvgaws == NULL)
        return NULL;
#endif

    OOP_GetAttr(o, aHidd_Gallium_WinSys, &vmwaresvga_ws);

    if (vmwaresvga_ws)
    {
        screen = vmwaresvga_create_screen(vmwaresvga_ws);
        if (screen)
        {
#if (0)
        /* Force a pipe_winsys pointer (Mesa 7.9 or never) */
        screen->winsys = (struct pipe_winsys *)vmwaresvgaws;

        /* Preserve pointer to HIDD driver */
        vmwaresvgaws->base.driver = o;
#endif
        }
    }

    return screen;

}

VOID METHOD(VMWareSVGAGallium, Hidd_Gallium, DisplayResource)
{
    struct vmwaresvga_resource * spr = vmwaresvga_resource(msg->resource);
    struct RastPort * rp;
    struct sw_winsys * vmwaresvga_ws = NULL;
    APTR * data = spr->data;

    OOP_GetAttr(o, aHidd_Gallium_WinSys, &vmwaresvga_ws);

    if (vmwaresvga_ws)
    {
        if ((data == NULL) && (spr->dt != NULL))
            data = vmwaresvga_ws->displaytarget_map(vmwaresvga_ws, spr->dt, 0);

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
            vmwaresvga_ws->displaytarget_unmap(vmwaresvga_ws, spr->dt);
    }
}
