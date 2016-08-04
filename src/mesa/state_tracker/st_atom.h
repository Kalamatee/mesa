/**************************************************************************
 * 
 * Copyright 2003 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  */
    

#ifndef ST_ATOM_H
#define ST_ATOM_H

#include "main/glheader.h"

struct st_context;

/**
 * Enumeration of state tracker pipelines.
 */
enum st_pipeline {
   ST_PIPELINE_RENDER,
   ST_PIPELINE_COMPUTE,
};

struct st_tracked_state {
   void (*update)( struct st_context *st );
};


void st_init_atoms( struct st_context *st );
void st_destroy_atoms( struct st_context *st );
void st_validate_state( struct st_context *st, enum st_pipeline pipeline );
GLuint st_compare_func_to_pipe(GLenum func);

enum pipe_format
st_pipe_vertex_format(GLenum type, GLuint size, GLenum format,
                      GLboolean normalized, GLboolean integer);


/* Define ST_NEW_xxx_INDEX */
enum {
#define ST_STATE(FLAG, st_update) FLAG##_INDEX,
#include "st_atom_list.h"
#undef ST_STATE
};

/* Define ST_NEW_xxx */
enum {
#define ST_STATE(FLAG, st_update) FLAG = 1llu << FLAG##_INDEX,
#include "st_atom_list.h"
#undef ST_STATE
};

/* Add extern struct declarations. */
#define ST_STATE(FLAG, st_update) extern const struct st_tracked_state st_update;
#include "st_atom_list.h"
#undef ST_STATE

/* Combined state flags. */
#define ST_NEW_SAMPLERS         (ST_NEW_RENDER_SAMPLERS | \
                                 ST_NEW_CS_SAMPLERS)

#define ST_NEW_FRAMEBUFFER      (ST_NEW_FB_STATE | \
                                 ST_NEW_SAMPLE_MASK | \
                                 ST_NEW_SAMPLE_SHADING)

#define ST_NEW_VERTEX_PROGRAM   (ST_NEW_VS_STATE | \
                                 ST_NEW_VS_SAMPLER_VIEWS | \
                                 ST_NEW_VS_IMAGES | \
                                 ST_NEW_VS_CONSTANTS | \
                                 ST_NEW_VS_UBOS | \
                                 ST_NEW_VS_ATOMICS | \
                                 ST_NEW_VS_SSBOS | \
                                 ST_NEW_VERTEX_ARRAYS | \
                                 ST_NEW_CLIP_STATE | \
                                 ST_NEW_RASTERIZER | \
                                 ST_NEW_RENDER_SAMPLERS)

#define ST_NEW_TCS_RESOURCES    (ST_NEW_TCS_SAMPLER_VIEWS | \
                                 ST_NEW_TCS_IMAGES | \
                                 ST_NEW_TCS_CONSTANTS | \
                                 ST_NEW_TCS_UBOS | \
                                 ST_NEW_TCS_ATOMICS | \
                                 ST_NEW_TCS_SSBOS)

#define ST_NEW_TESSCTRL_PROGRAM (ST_NEW_TCS_STATE | \
                                 ST_NEW_TCS_RESOURCES | \
                                 ST_NEW_RENDER_SAMPLERS)

#define ST_NEW_TES_RESOURCES    (ST_NEW_TES_SAMPLER_VIEWS | \
                                 ST_NEW_TES_IMAGES | \
                                 ST_NEW_TES_CONSTANTS | \
                                 ST_NEW_TES_UBOS | \
                                 ST_NEW_TES_ATOMICS | \
                                 ST_NEW_TES_SSBOS)

#define ST_NEW_TESSEVAL_PROGRAM (ST_NEW_TES_STATE | \
                                 ST_NEW_TES_RESOURCES | \
                                 ST_NEW_RASTERIZER | \
                                 ST_NEW_RENDER_SAMPLERS)

#define ST_NEW_GS_RESOURCES     (ST_NEW_GS_SAMPLER_VIEWS | \
                                 ST_NEW_GS_IMAGES | \
                                 ST_NEW_GS_CONSTANTS | \
                                 ST_NEW_GS_UBOS | \
                                 ST_NEW_GS_ATOMICS | \
                                 ST_NEW_GS_SSBOS)

#define ST_NEW_GEOMETRY_PROGRAM (ST_NEW_GS_STATE | \
                                 ST_NEW_GS_RESOURCES | \
                                 ST_NEW_RASTERIZER | \
                                 ST_NEW_RENDER_SAMPLERS)

#define ST_NEW_FRAGMENT_PROGRAM (ST_NEW_FS_STATE | \
                                 ST_NEW_FS_SAMPLER_VIEWS | \
                                 ST_NEW_FS_IMAGES | \
                                 ST_NEW_FS_CONSTANTS | \
                                 ST_NEW_FS_UBOS | \
                                 ST_NEW_FS_ATOMICS | \
                                 ST_NEW_FS_SSBOS | \
                                 ST_NEW_SAMPLE_SHADING | \
                                 ST_NEW_RENDER_SAMPLERS)

#define ST_NEW_COMPUTE_PROGRAM  (ST_NEW_CS_STATE | \
                                 ST_NEW_CS_SAMPLER_VIEWS | \
                                 ST_NEW_CS_IMAGES | \
                                 ST_NEW_CS_CONSTANTS | \
                                 ST_NEW_CS_UBOS | \
                                 ST_NEW_CS_ATOMICS | \
                                 ST_NEW_CS_SSBOS | \
                                 ST_NEW_CS_SAMPLERS)

#define ST_NEW_CONSTANTS        (ST_NEW_VS_CONSTANTS | \
                                 ST_NEW_TCS_CONSTANTS | \
                                 ST_NEW_TES_CONSTANTS | \
                                 ST_NEW_FS_CONSTANTS | \
                                 ST_NEW_GS_CONSTANTS | \
                                 ST_NEW_CS_CONSTANTS)

#define ST_NEW_UNIFORM_BUFFER   (ST_NEW_VS_UBOS | \
                                 ST_NEW_TCS_UBOS | \
                                 ST_NEW_TES_UBOS | \
                                 ST_NEW_FS_UBOS | \
                                 ST_NEW_GS_UBOS | \
                                 ST_NEW_CS_UBOS)

#define ST_NEW_SAMPLER_VIEWS    (ST_NEW_VS_SAMPLER_VIEWS | \
                                 ST_NEW_FS_SAMPLER_VIEWS | \
                                 ST_NEW_GS_SAMPLER_VIEWS | \
                                 ST_NEW_TCS_SAMPLER_VIEWS | \
                                 ST_NEW_TES_SAMPLER_VIEWS | \
                                 ST_NEW_CS_SAMPLER_VIEWS)

#define ST_NEW_ATOMIC_BUFFER    (ST_NEW_VS_ATOMICS | \
                                 ST_NEW_TCS_ATOMICS | \
                                 ST_NEW_TES_ATOMICS | \
                                 ST_NEW_FS_ATOMICS | \
                                 ST_NEW_GS_ATOMICS | \
                                 ST_NEW_CS_ATOMICS)

#define ST_NEW_STORAGE_BUFFER   (ST_NEW_VS_SSBOS | \
                                 ST_NEW_TCS_SSBOS | \
                                 ST_NEW_TES_SSBOS | \
                                 ST_NEW_FS_SSBOS | \
                                 ST_NEW_GS_SSBOS | \
                                 ST_NEW_CS_SSBOS)

#define ST_NEW_IMAGE_UNITS      (ST_NEW_VS_IMAGES | \
                                 ST_NEW_TCS_IMAGES | \
                                 ST_NEW_TES_IMAGES | \
                                 ST_NEW_GS_IMAGES | \
                                 ST_NEW_FS_IMAGES | \
                                 ST_NEW_CS_IMAGES)

/* All state flags within each group: */
#define ST_PIPELINE_RENDER_STATE_MASK  (ST_NEW_CS_STATE - 1)
#define ST_PIPELINE_COMPUTE_STATE_MASK (ST_NEW_COMPUTE_PROGRAM | \
                                        ST_NEW_CS_SAMPLERS)

#define ST_ALL_STATES_MASK (ST_PIPELINE_RENDER_STATE_MASK | \
                            ST_PIPELINE_COMPUTE_STATE_MASK)

#endif
