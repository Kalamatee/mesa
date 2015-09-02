/*
    Copyright 2015, The AROS Development Team. All rights reserved.
    $Id: vmwaresvgagalliumclass.c 50414 2015-04-18 11:21:09Z wawa $
*/

#define DEBUG 0
#include <aros/debug.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/oop.h>

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <utility/tagitem.h>

#include <hidd/gallium.h>
#include <gallium/gallium.h>

#include "vmwaresvga_intern.h"

#if (AROS_BIG_ENDIAN == 1)
#define AROS_PIXFMT RECTFMT_RAW   /* Big Endian Archs. */
#else
#define AROS_PIXFMT RECTFMT_BGRA32   /* Little Endian Archs. */
#endif

#define CyberGfxBase    (&BASE(cl->UserData)->sd)->VMWareSVGACyberGfxBase

#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase   (SD(cl)->hiddGalliumAB)


/*  Displaytarget support code ends */

OOP_Object *METHOD(GalliumVMWareSVGA, Root, New)
{
    IPTR interfaceVers;

    D(bug("[VMWareSVGA:Gallium] %s()\n", __PRETTY_FUNCTION__));

    interfaceVers = GetTagData(aHidd_Gallium_InterfaceVersion, -1, msg->attrList);
    if (interfaceVers != GALLIUM_INTERFACE_VERSION)
        return NULL;

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg) msg);
    if (0)
    {
        struct HIDDGalliumVMWareSVGAData * data = OOP_INST_DATA(cl, o);

        data->wsi.destroy                            = NULL;
    }

    return o;
}

VOID METHOD(GalliumVMWareSVGA, Root, Dispose)
{
    D(bug("[VMWareSVGA:Gallium] %s()\n", __PRETTY_FUNCTION__));

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(GalliumVMWareSVGA, Root, Get)
{
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

APTR METHOD(GalliumVMWareSVGA, Hidd_Gallium, CreatePipeScreen)
{
    struct HIDDGalliumVMWareSVGAData * data = OOP_INST_DATA(cl, o);
    struct pipe_screen *screen = NULL;

    D(bug("[VMWareSVGA:Gallium] %s()\n", __PRETTY_FUNCTION__));

    screen = svga_screen_create(&data->wsi);

    D(bug("[VMWareSVGA:Gallium] %s: screen @ 0x%p\n", __PRETTY_FUNCTION__, screen));

    return screen;

}

VOID METHOD(GalliumVMWareSVGA, Hidd_Gallium, DisplayResource)
{
    D(bug("[VMWareSVGA:Gallium] %s()\n", __PRETTY_FUNCTION__));
}
