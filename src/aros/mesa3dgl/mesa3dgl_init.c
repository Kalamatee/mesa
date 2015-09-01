/*
    Copyright 2010-2015, The AROS Development Team. All rights reserved.
    $Id: mesa3dgl_init.c 50856 2015-06-23 22:01:54Z NicJA $
*/

#define DEBUG 0
#include <aros/debug.h>
#undef DEBUG

#include <aros/symbolsets.h>

#include "state_tracker/st_gl_api.h"
#include "state_tracker/st_api.h"

/* This is a global GL API object */
/* TODO: Should be moved to LIBBASE */
struct st_api * glstapi;

LONG MESA3DGLInit()
{
    D(bug("[MESA3DGL] %s()\n", __PRETTY_FUNCTION__));

    glstapi = st_gl_api_create();

    D(bug("[MESA3DGL] %s: st_api @ 0x%p\n", __PRETTY_FUNCTION__, glstapi));

    return (glstapi != 0);
}

VOID MESA3DGLExit()
{
    D(bug("[MESA3DGL] %s()\n", __PRETTY_FUNCTION__));

    if (glstapi)
        glstapi->destroy(glstapi);
}

ADD2INIT(MESA3DGLInit, 5);
ADD2EXIT(MESA3DGLExit, 5);

