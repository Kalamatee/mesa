/*
    Copyright 2010, The AROS Development Team. All rights reserved.
    $Id: gallium_intern.h 33687 2010-06-23 21:28:31Z deadwood $
*/

#ifndef GALLIUM_INTERN_H
#define GALLIUM_INTERN_H

#ifndef EXEC_LIBRARIES_H
#   include <exec/libraries.h>
#endif

#ifndef GALLIUM_GALLIUM_H
#   include <gallium/gallium.h>
#endif

#ifndef PROTO_EXEC_H
#   include <proto/exec.h>
#endif

#ifndef EXEC_SEMAPHORES_H
#   include <exec/semaphores.h>
#endif

#ifndef OOP_OOP_H
#   include <oop/oop.h>
#endif

#ifndef PROTO_OOP_H
#   include <proto/oop.h>
#endif

#ifndef HIDD_GALLIUM_H
#   include <hidd/gallium.h>
#endif

#ifndef P_SCREEN_H
#   include "pipe/p_screen.h"
#endif

struct GalliumBase
{
    struct Library              galb_Lib;

    char                        *fallback;
    struct Library              *fallbackmodule;

    OOP_Class                   *basegallium;
    OOP_AttrBase                galliumAttrBase;
    OOP_AttrBase                bmAttrBase;

    /* methods we use .. */
    OOP_MethodID                galliumMId_UpdateRect;
    OOP_MethodID                galliumMId_DisplayResource;
};

#define GB(lb)  ((struct GalliumBase *)lb)

#undef HiddBitMapAttrBase
#define HiddBitMapAttrBase      (GB(GalliumBase)->bmAttrBase)
#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase      (GB(GalliumBase)->galliumAttrBase)

#endif
