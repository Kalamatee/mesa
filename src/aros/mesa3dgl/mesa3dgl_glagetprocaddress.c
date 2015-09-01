/*
    Copyright 2014-2015, The AROS Development Team. All rights reserved.
    $Id: mesa3dgl_glagetprocaddress.c 50856 2015-06-23 22:01:54Z NicJA $
*/

#define DEBUG 0
#include <aros/debug.h>
#undef DEBUG

#include <proto/exec.h>

#include "mesa3dgl_types.h"

/*****************************************************************************

    NAME */

      GLAProc glAGetProcAddress(

/*  SYNOPSIS */
      const GLubyte * procname)

/*  FUNCTION

    INPUTS

    RESULT
      Pointer to procname function or NULL if function is not supported


    BUGS

    INTERNALS

    HISTORY

*****************************************************************************/
{
    D(bug("[MESA3DGL] %s()\n", __PRETTY_FUNCTION__));

    return glstapi->get_proc_address(glstapi, procname);
}
