/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: init.c 41759 2011-10-05 20:14:17Z deadwood $
*/

#include <aros/symbolsets.h>
#include <hidd/gallium.h>
#include <proto/oop.h>
#include <proto/exec.h>

#include "vmwaresvga_intern.h"

static int VMWareSVGAHidd_ExpungeLib(LIBBASETYPEPTR LIBBASE)
{
    if (LIBBASE->sd.VMWareSVGACyberGfxBase)
        CloseLibrary(LIBBASE->sd.VMWareSVGACyberGfxBase);

    if (LIBBASE->sd.hiddGalliumAB)
        OOP_ReleaseAttrBase((STRPTR)IID_Hidd_Gallium);

    return TRUE;
}

static int VMWareSVGAHidd_InitLib(LIBBASETYPEPTR LIBBASE)
{
    LIBBASE->sd.VMWareSVGACyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library",0);

    LIBBASE->sd.hiddGalliumAB = OOP_ObtainAttrBase((STRPTR)IID_Hidd_Gallium);

    if (LIBBASE->sd.VMWareSVGACyberGfxBase && LIBBASE->sd.hiddGalliumAB)
        return TRUE;

    return FALSE;
}

ADD2INITLIB(VMWareSVGAHidd_InitLib, 0)
ADD2EXPUNGELIB(VMWareSVGAHidd_ExpungeLib, 0)

ADD2LIBS((STRPTR)"gallium.hidd", 7, static struct Library *, GalliumHiddBase);
