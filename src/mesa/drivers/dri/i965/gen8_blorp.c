/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>

#include "intel_batchbuffer.h"
#include "intel_fbo.h"
#include "intel_mipmap_tree.h"

#include "brw_context.h"
#include "brw_defines.h"
#include "brw_state.h"

#include "brw_blorp.h"

static uint32_t
gen8_blorp_emit_blend_state(struct brw_context *brw,
                            const struct brw_blorp_params *params)
{
   uint32_t blend_state_offset;

   assume(params->num_draw_buffers);

   const unsigned size = 4 + 8 * params->num_draw_buffers;
   uint32_t *blend = (uint32_t *)brw_state_batch(brw, AUB_TRACE_BLEND_STATE,
                                                 size, 64,
                                                 &blend_state_offset);
   memset(blend, 0, size);

   for (unsigned i = 0; i < params->num_draw_buffers; ++i) {
      if (params->color_write_disable[0])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_RED;
      if (params->color_write_disable[1])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_GREEN;
      if (params->color_write_disable[2])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_BLUE;
      if (params->color_write_disable[3])
         blend[1 + 2 * i] |= GEN8_BLEND_WRITE_DISABLE_ALPHA;

      blend[1 + 2 * i + 1] = GEN8_BLEND_PRE_BLEND_COLOR_CLAMP_ENABLE |
                             GEN8_BLEND_POST_BLEND_COLOR_CLAMP_ENABLE |
                             GEN8_BLEND_COLOR_CLAMP_RANGE_RTFORMAT;
   }

   return blend_state_offset;
}

/* Hardware seems to try to fetch the constants even though the corresponding
 * stage gets disabled. Therefore make sure the settings for the constant
 * buffer are valid.
 */
static void
gen8_blorp_disable_constant_state(struct brw_context *brw,
                                       unsigned opcode)
{
   BEGIN_BATCH(11);
   OUT_BATCH(opcode << 16 | (11 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_VS
 *
 * Disable vertex shader.
 */
static void
gen8_blorp_emit_vs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_VS << 16 | (9 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_HS
 *
 * Disable the hull shader.
 */
static void
gen8_blorp_emit_hs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(9);
   OUT_BATCH(_3DSTATE_HS << 16 | (9 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_DS
 *
 * Disable the domain shader.
 */
static void
gen8_blorp_emit_ds_disable(struct brw_context *brw)
{
   const int ds_pkt_len = brw->gen >= 9 ? 11 : 9;
   BEGIN_BATCH(ds_pkt_len);
   OUT_BATCH(_3DSTATE_DS << 16 | (ds_pkt_len - 2));
   for (int i = 0; i < ds_pkt_len - 1; i++)
      OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_GS
 *
 * Disable the geometry shader.
 */
static void
gen8_blorp_emit_gs_disable(struct brw_context *brw)
{
   BEGIN_BATCH(10);
   OUT_BATCH(_3DSTATE_GS << 16 | (10 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

/* 3DSTATE_STREAMOUT
 *
 * Disable streamout.
 */
static void
gen8_blorp_emit_streamout_disable(struct brw_context *brw)
{
   BEGIN_BATCH(5);
   OUT_BATCH(_3DSTATE_STREAMOUT << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_raster_state(struct brw_context *brw)
{
   BEGIN_BATCH(5);
   OUT_BATCH(_3DSTATE_RASTER << 16 | (5 - 2));
   OUT_BATCH(GEN8_RASTER_CULL_NONE);
   OUT_BATCH_F(0);
   OUT_BATCH_F(0);
   OUT_BATCH_F(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_sbe_state(struct brw_context *brw,
                          const struct brw_blorp_params *params)
{
   const unsigned num_varyings = params->wm_prog_data->num_varying_inputs;
   const unsigned urb_read_length =
      brw_blorp_get_urb_length(params->wm_prog_data);

   /* 3DSTATE_SBE */
   {
      const unsigned sbe_cmd_length = brw->gen == 8 ? 4 : 6;
      BEGIN_BATCH(sbe_cmd_length);
      OUT_BATCH(_3DSTATE_SBE << 16 | (sbe_cmd_length - 2));

      /* There is no need for swizzling (GEN7_SBE_SWIZZLE_ENABLE). All the
       * vertex data coming from vertex fetcher is taken as unmodified
       * (i.e., passed through). Vertex shader state is disabled and vertex
       * fetcher builds complete vertex entries including VUE header.
       * This is for unknown reason really needed to be disabled when more
       * than one vec4 worth of vertex attributes are needed.
       */
      OUT_BATCH(num_varyings << GEN7_SBE_NUM_OUTPUTS_SHIFT |
                urb_read_length << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
                BRW_SF_URB_ENTRY_READ_OFFSET <<
                   GEN8_SBE_URB_ENTRY_READ_OFFSET_SHIFT |
                GEN8_SBE_FORCE_URB_ENTRY_READ_LENGTH |
                GEN8_SBE_FORCE_URB_ENTRY_READ_OFFSET);
      OUT_BATCH(0);
      OUT_BATCH(params->wm_prog_data->flat_inputs);
      if (sbe_cmd_length >= 6) {
         /* Fragment coordinates are always enabled. */
         uint32_t dw4 = (GEN9_SBE_ACTIVE_COMPONENT_XYZW << (0 << 1));

         for (unsigned i = 0; i < num_varyings; ++i) {
            dw4 |= (GEN9_SBE_ACTIVE_COMPONENT_XYZW << ((i + 1) << 1));
         }

         OUT_BATCH(dw4);
         OUT_BATCH(0);
      }
      ADVANCE_BATCH();
   }

   {
      BEGIN_BATCH(11);
      OUT_BATCH(_3DSTATE_SBE_SWIZ << 16 | (11 - 2));

      /* Output DWords 1 through 8: */
      for (int i = 0; i < 8; i++) {
         OUT_BATCH(0);
      }

      OUT_BATCH(0); /* wrapshortest enables 0-7 */
      OUT_BATCH(0); /* wrapshortest enables 8-15 */
      ADVANCE_BATCH();
   }
}

static void
gen8_blorp_emit_sf_config(struct brw_context *brw)
{
   /* See gen6_blorp_emit_sf_config() */
   BEGIN_BATCH(4);
   OUT_BATCH(_3DSTATE_SF << 16 | (4 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(GEN6_SF_LINE_AA_MODE_TRUE);
   ADVANCE_BATCH();
}

/**
 * Disable thread dispatch (dw5.19) and enable the HiZ op.
 */
static void
gen8_blorp_emit_wm_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_WM << 16 | (2 - 2));
   OUT_BATCH(GEN7_WM_LINE_AA_WIDTH_1_0 |
             GEN7_WM_LINE_END_CAP_AA_WIDTH_0_5 |
             GEN7_WM_POINT_RASTRULE_UPPER_RIGHT);
   ADVANCE_BATCH();
}

/**
 * 3DSTATE_PS
 *
 * Pixel shader dispatch is disabled above in 3DSTATE_WM, dw1.29. Despite
 * that, thread dispatch info must still be specified.
 *     - Maximum Number of Threads (dw4.24:31) must be nonzero, as the
 *       valid range for this field is [0x3, 0x2f].
 *     - A dispatch mode must be given; that is, at least one of the
 *       "N Pixel Dispatch Enable" (N=8,16,32) fields must be set. This was
 *       discovered through simulator error messages.
 */
static void
gen8_blorp_emit_ps_config(struct brw_context *brw,
                          const struct brw_blorp_params *params)
{
   const struct brw_blorp_prog_data *prog_data = params->wm_prog_data;
   uint32_t dw3, dw5, dw6, dw7, ksp0, ksp2;

   dw3 = dw5 = dw6 = dw7 = ksp0 = ksp2 = 0;
   dw3 |= GEN7_PS_VECTOR_MASK_ENABLE;

   if (params->src.mt) {
      dw3 |= 1 << GEN7_PS_SAMPLER_COUNT_SHIFT; /* Up to 4 samplers */
      dw3 |= 2 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT; /* Two surfaces */
   } else {
      dw3 |= 1 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT; /* One surface */
   }

   dw7 |= prog_data->first_curbe_grf_0 << GEN7_PS_DISPATCH_START_GRF_SHIFT_0;
   dw7 |= prog_data->first_curbe_grf_2 << GEN7_PS_DISPATCH_START_GRF_SHIFT_2;

   if (params->wm_prog_data->dispatch_8)
      dw6 |= GEN7_PS_8_DISPATCH_ENABLE;
   if (params->wm_prog_data->dispatch_16)
      dw6 |= GEN7_PS_16_DISPATCH_ENABLE;

   ksp0 = params->wm_prog_kernel;
   ksp2 = params->wm_prog_kernel + params->wm_prog_data->ksp_offset_2;

   /* 3DSTATE_PS expects the number of threads per PSD, which is always 64;
    * it implicitly scales for different GT levels (which have some # of PSDs).
    *
    * In Gen8 the format is U8-2 whereas in Gen9 it is U8-1.
    */
   if (brw->gen >= 9)
      dw6 |= (64 - 1) << HSW_PS_MAX_THREADS_SHIFT;
   else
      dw6 |= (64 - 2) << HSW_PS_MAX_THREADS_SHIFT;

   dw6 |= GEN7_PS_POSOFFSET_NONE;
   dw6 |= params->fast_clear_op;

   BEGIN_BATCH(12);
   OUT_BATCH(_3DSTATE_PS << 16 | (12 - 2));
   OUT_BATCH(ksp0);
   OUT_BATCH(0);
   OUT_BATCH(dw3);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(dw6);
   OUT_BATCH(dw7);
   OUT_BATCH(0); /* kernel 1 pointer */
   OUT_BATCH(0);
   OUT_BATCH(ksp2);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_ps_blend(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_PS_BLEND << 16 | (2 - 2));
   OUT_BATCH(GEN8_PS_BLEND_HAS_WRITEABLE_RT);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_ps_extra(struct brw_context *brw,
                         const struct brw_blorp_params *params)
{
   const struct brw_blorp_prog_data *prog_data = params->wm_prog_data;
   uint32_t dw1 = 0;

   dw1 |= GEN8_PSX_PIXEL_SHADER_VALID;

   if (params->src.mt)
      dw1 |= GEN8_PSX_KILL_ENABLE;

   if (params->wm_prog_data->num_varying_inputs)
      dw1 |= GEN8_PSX_ATTRIBUTE_ENABLE;

   if (params->dst.num_samples > 1 && prog_data &&
       prog_data->persample_msaa_dispatch)
      dw1 |= GEN8_PSX_SHADER_IS_PER_SAMPLE;

   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_PS_EXTRA << 16 | (2 - 2));
   OUT_BATCH(dw1);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_depth_disable(struct brw_context *brw)
{
   /* Skip repeated NULL depth/stencil emits (think 2D rendering). */
   if (brw->no_depth_or_stencil)
      return;

   brw_emit_depth_stall_flushes(brw);

   BEGIN_BATCH(8);
   OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER << 16 | (8 - 2));
   OUT_BATCH((BRW_DEPTHFORMAT_D32_FLOAT << 18) | (BRW_SURFACE_NULL << 29));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   BEGIN_BATCH(5);
   OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();

   BEGIN_BATCH(5);
   OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER << 16 | (5 - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_topology(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF_TOPOLOGY << 16 | (2 - 2));
   OUT_BATCH(_3DPRIM_RECTLIST);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_sys_gen_vals_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF_SGVS << 16 | (2 - 2));
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_vf_instancing_state(struct brw_context *brw,
                                    const struct brw_blorp_params *params)
{
   const unsigned num_varyings =
      params->wm_prog_data ? params->wm_prog_data->num_varying_inputs : 0;
   const unsigned num_elems = 2 + num_varyings;

   for (unsigned i = 0; i < num_elems; ++i) {
      BEGIN_BATCH(3);
      OUT_BATCH(_3DSTATE_VF_INSTANCING << 16 | (3 - 2));
      OUT_BATCH(i);
      OUT_BATCH(0);
      ADVANCE_BATCH();
   }
}

static void
gen8_blorp_emit_vf_state(struct brw_context *brw)
{
   BEGIN_BATCH(2);
   OUT_BATCH(_3DSTATE_VF << 16 | (2 - 2));
   OUT_BATCH(0);
   ADVANCE_BATCH();
}

static void
gen8_blorp_emit_depth_stencil_state(struct brw_context *brw,
                                    const struct brw_blorp_params *params)
{
   const unsigned pkt_len = brw->gen >= 9 ? 4 : 3;

   BEGIN_BATCH(pkt_len);
   OUT_BATCH(_3DSTATE_WM_DEPTH_STENCIL << 16 | (pkt_len - 2));
   OUT_BATCH(0);
   OUT_BATCH(0);
   if (pkt_len > 3) {
      OUT_BATCH(0);
   }
   ADVANCE_BATCH();
}

/**
 * Convert an swizzle enumeration (i.e. SWIZZLE_X) to one of the Gen7.5+
 * "Shader Channel Select" enumerations (i.e. HSW_SCS_RED).  The mappings are
 *
 * SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W, SWIZZLE_ZERO, SWIZZLE_ONE
 *         0          1          2          3             4            5
 *         4          5          6          7             0            1
 *   SCS_RED, SCS_GREEN,  SCS_BLUE, SCS_ALPHA,     SCS_ZERO,     SCS_ONE
 *
 * which is simply adding 4 then modding by 8 (or anding with 7).
 *
 * We then may need to apply workarounds for textureGather hardware bugs.
 */
static unsigned
swizzle_to_scs(GLenum swizzle)
{
   return (swizzle + 4) & 7;
}

static uint32_t
gen8_blorp_emit_surface_states(struct brw_context *brw,
                               const struct brw_blorp_params *params)
{
   uint32_t wm_surf_offset_renderbuffer;
   uint32_t wm_surf_offset_texture = 0;

   intel_miptree_used_for_rendering(params->dst.mt);

   wm_surf_offset_renderbuffer =
      brw_blorp_emit_surface_state(brw, &params->dst,
                                   I915_GEM_DOMAIN_RENDER,
                                   I915_GEM_DOMAIN_RENDER,
                                   true /* is_render_target */);
   if (params->src.mt) {
      const struct brw_blorp_surface_info *surface = &params->src;
      struct intel_mipmap_tree *mt = surface->mt;

      /* If src is a 2D multisample array texture on Gen7+ using
       * INTEL_MSAA_LAYOUT_UMS or INTEL_MSAA_LAYOUT_CMS, src layer is the
       * physical layer holding sample 0.  So, for example, if mt->num_samples
       * == 4, then logical layer n corresponds to layer == 4*n.
       *
       * Multisampled depth and stencil surfaces have the samples interleaved
       * (INTEL_MSAA_LAYOUT_IMS) and therefore the layer doesn't need
       * adjustment.
       */
      const unsigned layer_divider =
         (mt->msaa_layout == INTEL_MSAA_LAYOUT_UMS ||
          mt->msaa_layout == INTEL_MSAA_LAYOUT_CMS) ?
         MAX2(mt->num_samples, 1) : 1;

      const unsigned layer = mt->target != GL_TEXTURE_3D ?
                                surface->layer / layer_divider : 0;

      struct isl_view view = {
         .format = surface->brw_surfaceformat,
         .base_level = surface->level,
         .levels = mt->last_level - surface->level + 1,
         .base_array_layer = layer,
         .array_len = mt->logical_depth0 - layer,
         .channel_select = {
            swizzle_to_scs(GET_SWZ(surface->swizzle, 0)),
            swizzle_to_scs(GET_SWZ(surface->swizzle, 1)),
            swizzle_to_scs(GET_SWZ(surface->swizzle, 2)),
            swizzle_to_scs(GET_SWZ(surface->swizzle, 3)),
         },
         .usage = ISL_SURF_USAGE_TEXTURE_BIT,
      };

      brw_emit_surface_state(brw, mt, &view,
                             brw->gen >= 9 ? SKL_MOCS_WB : BDW_MOCS_WB,
                             false, &wm_surf_offset_texture, -1,
                             I915_GEM_DOMAIN_SAMPLER, 0);
   }

   return gen6_blorp_emit_binding_table(brw,
                                        wm_surf_offset_renderbuffer,
                                        wm_surf_offset_texture);
}

/**
 * \copydoc gen6_blorp_exec()
 */
void
gen8_blorp_exec(struct brw_context *brw, const struct brw_blorp_params *params)
{
   uint32_t wm_bind_bo_offset = 0;

   brw_upload_state_base_address(brw);

   gen7_blorp_emit_cc_viewport(brw);
   gen7_l3_state.emit(brw);

   gen7_blorp_emit_urb_config(brw, params);

   const uint32_t cc_blend_state_offset =
      gen8_blorp_emit_blend_state(brw, params);
   gen7_blorp_emit_blend_state_pointer(brw, cc_blend_state_offset);

   const uint32_t cc_state_offset = gen6_blorp_emit_cc_state(brw);
   gen7_blorp_emit_cc_state_pointer(brw, cc_state_offset);

   gen8_blorp_disable_constant_state(brw, _3DSTATE_CONSTANT_VS);
   gen8_blorp_disable_constant_state(brw, _3DSTATE_CONSTANT_HS);
   gen8_blorp_disable_constant_state(brw, _3DSTATE_CONSTANT_DS);
   gen8_blorp_disable_constant_state(brw, _3DSTATE_CONSTANT_GS);
   gen8_blorp_disable_constant_state(brw, _3DSTATE_CONSTANT_PS);

   wm_bind_bo_offset = gen8_blorp_emit_surface_states(brw, params);

   gen7_blorp_emit_binding_table_pointers_ps(brw, wm_bind_bo_offset);

   if (params->src.mt) {
      const uint32_t sampler_offset =
         gen6_blorp_emit_sampler_state(brw, BRW_MAPFILTER_LINEAR, 0, true);
      gen7_blorp_emit_sampler_state_pointers_ps(brw, sampler_offset);
   }

   gen8_emit_3dstate_multisample(brw, params->dst.num_samples);
   gen6_emit_3dstate_sample_mask(brw,
                                 params->dst.num_samples > 1 ?
                                    (1 << params->dst.num_samples) - 1 : 1);

   gen8_disable_stages.emit(brw);
   gen8_blorp_emit_vs_disable(brw);
   gen8_blorp_emit_hs_disable(brw);
   gen7_blorp_emit_te_disable(brw);
   gen8_blorp_emit_ds_disable(brw);
   gen8_blorp_emit_gs_disable(brw);

   gen8_blorp_emit_streamout_disable(brw);
   gen6_blorp_emit_clip_disable(brw);
   gen8_blorp_emit_raster_state(brw);
   gen8_blorp_emit_sbe_state(brw, params);
   gen8_blorp_emit_sf_config(brw);

   gen8_blorp_emit_ps_blend(brw);
   gen8_blorp_emit_ps_extra(brw, params);

   gen8_blorp_emit_ps_config(brw, params);

   gen8_blorp_emit_depth_stencil_state(brw, params);
   gen8_blorp_emit_wm_state(brw);

   gen8_blorp_emit_depth_disable(brw);
   gen7_blorp_emit_clear_params(brw, params);
   gen6_blorp_emit_drawing_rectangle(brw, params);
   gen8_blorp_emit_vf_topology(brw);
   gen8_blorp_emit_vf_sys_gen_vals_state(brw);
   gen6_blorp_emit_vertices(brw, params);
   gen8_blorp_emit_vf_instancing_state(brw, params);
   gen8_blorp_emit_vf_state(brw);
   gen7_blorp_emit_primitive(brw, params);

   if (brw->gen < 9)
      gen8_write_pma_stall_bits(brw, 0);
}
