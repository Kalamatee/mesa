/*
    Copyright © 2010-2015, The AROS Development Team. All rights reserved.
    $Id: createpipescreen.c 48224 2013-10-07 14:30:43Z neil $
*/

#define DEBUG 1
#include <aros/debug.h>

#include <proto/utility.h>
#include <proto/intuition.h>

#include <hidd/graphics.h>

#include <stdio.h>

#include "gallium_intern.h"

#undef HiddGalliumAttrBase
#define HiddGalliumAttrBase GB(GalliumBase)->galliumAttrBase

/*****************************************************************************

    NAME */

      AROS_LH1(void, DestroyPipe,

/*  SYNOPSIS */ 
      AROS_LHA(PipeHandle_t, pipe, A0),

/*  LOCATION */
      struct Library *, GalliumBase, 6, Gallium)

/*  FUNCTION
        Destroys a peviously created pipe.

    INPUTS
        pipe - a pipe handle created using CreatePipe().

    RESULT

    BUGS

    INTERNALS

*****************************************************************************/
{
    AROS_LIBFUNC_INIT

    OOP_DisposeObject((OOP_Object *)pipe);

    AROS_LIBFUNC_EXIT
}

