/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: init.c 41759 2011-10-05 20:14:17Z deadwood $
*/

#include <aros/symbolsets.h>
#include <hidd/gallium.h>
#include <proto/oop.h>
#include <proto/exec.h>

#include "vc4_intern.h"

static int VC4Hidd_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    if (LIBBASE->sd.VC4CyberGfxBase)
        CloseLibrary(LIBBASE->sd.VC4CyberGfxBase);

    if (LIBBASE->sd.hiddGalliumAB)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_Gallium);

    return TRUE;
}

static int VC4Hidd_InitLib(LIBBASETYPEPTR LIBBASE)
{
    LIBBASE->sd.VC4CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library",0);

    LIBBASE->sd.hiddGalliumAB = OOP_ObtainAttrBase((STRPTR)IID_Hidd_Gallium);

    if (LIBBASE->sd.VC4CyberGfxBase && LIBBASE->sd.hiddGalliumAB)
        return TRUE;

    return FALSE;
}

ADD2INITLIB(VC4Hidd_InitLib, 0)
ADD2EXPUNGELIB(VC4Hidd_ExpungeLib, 0)

ADD2LIBS((STRPTR)"gallium.hidd", 7, static struct Library *, GalliumHiddBase);
