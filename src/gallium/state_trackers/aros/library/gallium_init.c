/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: gallium_init.c 41762 2011-10-05 20:14:27Z deadwood $
*/

#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <proto/oop.h>

#include <hidd/gfx.h>

#include LC_LIBDEFS_FILE
#include "gallium_intern.h"

CONST_STRPTR softpipe_str = "softpipe";

static int Init(LIBBASETYPEPTR LIBBASE)
{
    LIBBASE->displayAttrBase = OOP_ObtainAttrBase((STRPTR)IID_Hidd_Display);
    if (!LIBBASE->displayAttrBase)
        return FALSE;

    LIBBASE->bmAttrBase = OOP_ObtainAttrBase((STRPTR)IID_Hidd_BitMap);
    if (!LIBBASE->bmAttrBase)
        return FALSE;

    LIBBASE->galliumAttrBase = OOP_ObtainAttrBase((STRPTR)IID_Hidd_Gallium);
    if (!LIBBASE->galliumAttrBase)
        return FALSE;

    LIBBASE->basegallium = OOP_FindClass(CLID_Hidd_Gallium);
    LIBBASE->fallback = (char *)softpipe_str;
    LIBBASE->fallbackmodule = NULL;

    /* cache method id's that we use ..  */
    LIBBASE->galliumMId_UpdateRect = OOP_GetMethodID(IID_Hidd_BitMap, moHidd_BitMap_UpdateRect);
    LIBBASE->galliumMId_DisplayResource = OOP_GetMethodID(IID_Hidd_Gallium, moHidd_Gallium_DisplayResource);

    return TRUE;
}

static int Expunge(LIBBASETYPEPTR LIBBASE)
{
    
    if (LIBBASE->galliumAttrBase)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_Gallium);

    if (LIBBASE->bmAttrBase)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_BitMap);

    if (LIBBASE->displayAttrBase)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_Display);

    if (LIBBASE->fallbackmodule)
        CloseLibrary(LIBBASE->fallbackmodule);

    return TRUE;
}

ADD2INITLIB(Init, 0);
ADD2EXPUNGELIB(Expunge, 0);

ADD2LIBS((STRPTR)"gallium.hidd", 0, static struct Library *, GalliumHiddBase);
