#ifndef _SOFTPIPE_INTERN_H
#define _SOFTPIPE_INTERN_H

/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: vmwaresvga_intern.h 33556 2010-06-12 12:57:12Z deadwood $
*/


#include LC_LIBDEFS_FILE

#define CLID_Hidd_Gallium_VMWareSVGA  "hidd.gallium.vmwaresvga"

struct HIDDGalliumVMWareSVGAData
{
};

struct vmwaresvgastaticdata 
{
    OOP_Class       *galliumclass;
    OOP_AttrBase    hiddGalliumAB;
    struct Library  *VMWareSVGACyberGfxBase;
};

LIBBASETYPE 
{
    struct Library              LibNode;
    struct vmwaresvgastaticdata   sd;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define BASE(lib) ((LIBBASETYPEPTR)(lib))

#define SD(cl) (&BASE(cl->UserData)->sd)

#endif
