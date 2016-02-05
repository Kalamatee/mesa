#ifndef _VC4_INTERN_H
#define _VC4_INTERN_H

/*
    Copyright 2015, The AROS Development Team. All rights reserved.
    $Id: vc4_intern.h 33556 2010-06-12 12:57:12Z deadwood $
*/


#include LC_LIBDEFS_FILE

#define CLID_Hidd_Gallium_VC4  "hidd.gallium.vc4"

// The object instance data is used as our winsys wrapper
struct HiddGalliumVC4Data
{
#if (0)
    struct sw_winsys vc4_ws;
#endif
    OOP_Object *vc4_obj;
};

struct vc4staticdata 
{
    OOP_Class       *galliumclass;
    OOP_AttrBase    hiddGalliumAB;
    struct Library  *VC4CyberGfxBase;
};

LIBBASETYPE 
{
    struct Library              LibNode;
    struct vc4staticdata   sd;
};

#define METHOD(base, id, name) \
  base ## __ ## id ## __ ## name (OOP_Class *cl, OOP_Object *o, struct p ## id ## _ ## name *msg)

#define BASE(lib) ((LIBBASETYPEPTR)(lib))

#define SD(cl) (&BASE(cl->UserData)->sd)

#endif
