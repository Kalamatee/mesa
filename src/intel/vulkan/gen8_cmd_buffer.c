/*
 * Copyright © 2015 Intel Corporation
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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "anv_private.h"

#include "genxml/gen_macros.h"
#include "genxml/genX_pack.h"

#if GEN_GEN == 8
void
gen8_cmd_buffer_emit_viewport(struct anv_cmd_buffer *cmd_buffer)
{
   uint32_t count = cmd_buffer->state.dynamic.viewport.count;
   const VkViewport *viewports = cmd_buffer->state.dynamic.viewport.viewports;
   struct anv_state sf_clip_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, count * 64, 64);
   struct anv_state cc_state =
      anv_cmd_buffer_alloc_dynamic_state(cmd_buffer, count * 8, 32);

   for (uint32_t i = 0; i < count; i++) {
      const VkViewport *vp = &viewports[i];

      /* The gen7 state struct has just the matrix and guardband fields, the
       * gen8 struct adds the min/max viewport fields. */
      struct GENX(SF_CLIP_VIEWPORT) sf_clip_viewport = {
         .ViewportMatrixElementm00 = vp->width / 2,
         .ViewportMatrixElementm11 = vp->height / 2,
         .ViewportMatrixElementm22 = 1.0,
         .ViewportMatrixElementm30 = vp->x + vp->width / 2,
         .ViewportMatrixElementm31 = vp->y + vp->height / 2,
         .ViewportMatrixElementm32 = 0.0,
         .XMinClipGuardband = -1.0f,
         .XMaxClipGuardband = 1.0f,
         .YMinClipGuardband = -1.0f,
         .YMaxClipGuardband = 1.0f,
         .XMinViewPort = vp->x,
         .XMaxViewPort = vp->x + vp->width - 1,
         .YMinViewPort = vp->y,
         .YMaxViewPort = vp->y + vp->height - 1,
      };

      struct GENX(CC_VIEWPORT) cc_viewport = {
         .MinimumDepth = vp->minDepth,
         .MaximumDepth = vp->maxDepth
      };

      GENX(SF_CLIP_VIEWPORT_pack)(NULL, sf_clip_state.map + i * 64,
                                 &sf_clip_viewport);
      GENX(CC_VIEWPORT_pack)(NULL, cc_state.map + i * 8, &cc_viewport);
   }

   if (!cmd_buffer->device->info.has_llc) {
      anv_state_clflush(sf_clip_state);
      anv_state_clflush(cc_state);
   }

   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_VIEWPORT_STATE_POINTERS_CC),
                  .CCViewportPointer = cc_state.offset);
   anv_batch_emit(&cmd_buffer->batch,
                  GENX(3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP),
                  .SFClipViewportPointer = sf_clip_state.offset);
}
#endif

#define emit_lri(batch, reg, imm)                       \
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_IMM),    \
                  .RegisterOffset = __anv_reg_num(reg), \
                  .DataDWord = imm)

void
genX(cmd_buffer_config_l3)(struct anv_cmd_buffer *cmd_buffer, bool enable_slm)
{
   /* References for GL state:
    *
    * - commits e307cfa..228d5a3
    * - src/mesa/drivers/dri/i965/gen7_l3_state.c
    */

   uint32_t l3cr_slm, l3cr_noslm;
   anv_pack_struct(&l3cr_noslm, GENX(L3CNTLREG),
                   .URBAllocation = 48,
                   .AllAllocation = 48);
   anv_pack_struct(&l3cr_slm, GENX(L3CNTLREG),
                   .SLMEnable = 1,
                   .URBAllocation = 16,
                   .AllAllocation = 48);
   const uint32_t l3cr_val = enable_slm ? l3cr_slm : l3cr_noslm;
   bool changed = cmd_buffer->state.current_l3_config != l3cr_val;

   if (changed) {
      /* According to the hardware docs, the L3 partitioning can only be
       * changed while the pipeline is completely drained and the caches are
       * flushed, which involves a first PIPE_CONTROL flush which stalls the
       * pipeline...
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL),
                     .DCFlushEnable = true,
                     .PostSyncOperation = NoWrite,
                     .CommandStreamerStallEnable = true);

      /* ...followed by a second pipelined PIPE_CONTROL that initiates
       * invalidation of the relevant caches. Note that because RO
       * invalidation happens at the top of the pipeline (i.e. right away as
       * the PIPE_CONTROL command is processed by the CS) we cannot combine it
       * with the previous stalling flush as the hardware documentation
       * suggests, because that would cause the CS to stall on previous
       * rendering *after* RO invalidation and wouldn't prevent the RO caches
       * from being polluted by concurrent rendering before the stall
       * completes. This intentionally doesn't implement the SKL+ hardware
       * workaround suggesting to enable CS stall on PIPE_CONTROLs with the
       * texture cache invalidation bit set for GPGPU workloads because the
       * previous and subsequent PIPE_CONTROLs already guarantee that there is
       * no concurrent GPGPU kernel execution (see SKL HSD 2132585).
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL),
                     .TextureCacheInvalidationEnable = true,
                     .ConstantCacheInvalidationEnable = true,
                     .InstructionCacheInvalidateEnable = true,
                     .StateCacheInvalidationEnable = true,
                     .PostSyncOperation = NoWrite);

      /* Now send a third stalling flush to make sure that invalidation is
       * complete when the L3 configuration registers are modified.
       */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL),
                     .DCFlushEnable = true,
                     .PostSyncOperation = NoWrite,
                     .CommandStreamerStallEnable = true);

      emit_lri(&cmd_buffer->batch, GENX(L3CNTLREG), l3cr_val);
      cmd_buffer->state.current_l3_config = l3cr_val;
   }
}

static void
__emit_genx_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GENX(3DSTATE_SF) sf = {
         GENX(3DSTATE_SF_header),
         .LineWidth = cmd_buffer->state.dynamic.line_width,
      };
      GENX(3DSTATE_SF_pack)(NULL, sf_dw, &sf);
      /* FIXME: gen9.fs */
      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw,
                           cmd_buffer->state.pipeline->gen8.sf);
}

#include "genxml/gen9_pack.h"
static void
__emit_gen9_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
      uint32_t sf_dw[GENX(3DSTATE_SF_length)];
      struct GEN9_3DSTATE_SF sf = {
         GEN9_3DSTATE_SF_header,
         .LineWidth = cmd_buffer->state.dynamic.line_width,
      };
      GEN9_3DSTATE_SF_pack(NULL, sf_dw, &sf);
      /* FIXME: gen9.fs */
      anv_batch_emit_merge(&cmd_buffer->batch, sf_dw,
                           cmd_buffer->state.pipeline->gen8.sf);
}

static void
__emit_sf_state(struct anv_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->device->info.is_cherryview)
      __emit_gen9_sf_state(cmd_buffer);
   else
      __emit_genx_sf_state(cmd_buffer);
}

void
genX(cmd_buffer_flush_dynamic_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.pipeline;

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_LINE_WIDTH)) {
      __emit_sf_state(cmd_buffer);
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_DEPTH_BIAS)){
      uint32_t raster_dw[GENX(3DSTATE_RASTER_length)];
      struct GENX(3DSTATE_RASTER) raster = {
         GENX(3DSTATE_RASTER_header),
         .GlobalDepthOffsetConstant = cmd_buffer->state.dynamic.depth_bias.bias,
         .GlobalDepthOffsetScale = cmd_buffer->state.dynamic.depth_bias.slope,
         .GlobalDepthOffsetClamp = cmd_buffer->state.dynamic.depth_bias.clamp
      };
      GENX(3DSTATE_RASTER_pack)(NULL, raster_dw, &raster);
      anv_batch_emit_merge(&cmd_buffer->batch, raster_dw,
                           pipeline->gen8.raster);
   }

   /* Stencil reference values moved from COLOR_CALC_STATE in gen8 to
    * 3DSTATE_WM_DEPTH_STENCIL in gen9. That means the dirty bits gets split
    * across different state packets for gen8 and gen9. We handle that by
    * using a big old #if switch here.
    */
#if GEN_GEN == 8
   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE)) {
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GENX(COLOR_CALC_STATE_length) * 4,
                                            64);
      struct GENX(COLOR_CALC_STATE) cc = {
         .BlendConstantColorRed = cmd_buffer->state.dynamic.blend_constants[0],
         .BlendConstantColorGreen = cmd_buffer->state.dynamic.blend_constants[1],
         .BlendConstantColorBlue = cmd_buffer->state.dynamic.blend_constants[2],
         .BlendConstantColorAlpha = cmd_buffer->state.dynamic.blend_constants[3],
         .StencilReferenceValue = d->stencil_reference.front & 0xff,
         .BackFaceStencilReferenceValue = d->stencil_reference.back & 0xff,
      };
      GENX(COLOR_CALC_STATE_pack)(NULL, cc_state.map, &cc);

      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(cc_state);

      anv_batch_emit(&cmd_buffer->batch,
                     GENX(3DSTATE_CC_STATE_POINTERS),
                     .ColorCalcStatePointer = cc_state.offset,
                     .ColorCalcStatePointerValid = true);
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK)) {
      uint32_t wm_depth_stencil_dw[GENX(3DSTATE_WM_DEPTH_STENCIL_length)];
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;

      struct GENX(3DSTATE_WM_DEPTH_STENCIL wm_depth_stencil) = {
         GENX(3DSTATE_WM_DEPTH_STENCIL_header),

         .StencilTestMask = d->stencil_compare_mask.front & 0xff,
         .StencilWriteMask = d->stencil_write_mask.front & 0xff,

         .BackfaceStencilTestMask = d->stencil_compare_mask.back & 0xff,
         .BackfaceStencilWriteMask = d->stencil_write_mask.back & 0xff,
      };
      GENX(3DSTATE_WM_DEPTH_STENCIL_pack)(NULL, wm_depth_stencil_dw,
                                          &wm_depth_stencil);

      anv_batch_emit_merge(&cmd_buffer->batch, wm_depth_stencil_dw,
                           pipeline->gen8.wm_depth_stencil);
   }
#else
   if (cmd_buffer->state.dirty & ANV_CMD_DIRTY_DYNAMIC_BLEND_CONSTANTS) {
      struct anv_state cc_state =
         anv_cmd_buffer_alloc_dynamic_state(cmd_buffer,
                                            GEN9_COLOR_CALC_STATE_length * 4,
                                            64);
      struct GEN9_COLOR_CALC_STATE cc = {
         .BlendConstantColorRed = cmd_buffer->state.dynamic.blend_constants[0],
         .BlendConstantColorGreen = cmd_buffer->state.dynamic.blend_constants[1],
         .BlendConstantColorBlue = cmd_buffer->state.dynamic.blend_constants[2],
         .BlendConstantColorAlpha = cmd_buffer->state.dynamic.blend_constants[3],
      };
      GEN9_COLOR_CALC_STATE_pack(NULL, cc_state.map, &cc);

      if (!cmd_buffer->device->info.has_llc)
         anv_state_clflush(cc_state);

      anv_batch_emit(&cmd_buffer->batch,
                     GEN9_3DSTATE_CC_STATE_POINTERS,
                     .ColorCalcStatePointer = cc_state.offset,
                     .ColorCalcStatePointerValid = true);
   }

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK |
                                  ANV_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE)) {
      uint32_t dwords[GEN9_3DSTATE_WM_DEPTH_STENCIL_length];
      struct anv_dynamic_state *d = &cmd_buffer->state.dynamic;
      struct GEN9_3DSTATE_WM_DEPTH_STENCIL wm_depth_stencil = {
         GEN9_3DSTATE_WM_DEPTH_STENCIL_header,

         .StencilTestMask = d->stencil_compare_mask.front & 0xff,
         .StencilWriteMask = d->stencil_write_mask.front & 0xff,

         .BackfaceStencilTestMask = d->stencil_compare_mask.back & 0xff,
         .BackfaceStencilWriteMask = d->stencil_write_mask.back & 0xff,

         .StencilReferenceValue = d->stencil_reference.front & 0xff,
         .BackfaceStencilReferenceValue = d->stencil_reference.back & 0xff,
      };
      GEN9_3DSTATE_WM_DEPTH_STENCIL_pack(NULL, dwords, &wm_depth_stencil);

      anv_batch_emit_merge(&cmd_buffer->batch, dwords,
                           pipeline->gen9.wm_depth_stencil);
   }
#endif

   if (cmd_buffer->state.dirty & (ANV_CMD_DIRTY_PIPELINE |
                                  ANV_CMD_DIRTY_INDEX_BUFFER)) {
      anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_VF),
         .IndexedDrawCutIndexEnable = pipeline->primitive_restart,
         .CutIndex = cmd_buffer->state.restart_index,
      );
   }

   cmd_buffer->state.dirty = 0;
}

void genX(CmdBindIndexBuffer)(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    _buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_buffer, buffer, _buffer);

   static const uint32_t vk_to_gen_index_type[] = {
      [VK_INDEX_TYPE_UINT16]                    = INDEX_WORD,
      [VK_INDEX_TYPE_UINT32]                    = INDEX_DWORD,
   };

   static const uint32_t restart_index_for_type[] = {
      [VK_INDEX_TYPE_UINT16]                    = UINT16_MAX,
      [VK_INDEX_TYPE_UINT32]                    = UINT32_MAX,
   };

   cmd_buffer->state.restart_index = restart_index_for_type[indexType];

   anv_batch_emit(&cmd_buffer->batch, GENX(3DSTATE_INDEX_BUFFER),
                  .IndexFormat = vk_to_gen_index_type[indexType],
                  .MemoryObjectControlState = GENX(MOCS),
                  .BufferStartingAddress = { buffer->bo, buffer->offset + offset },
                  .BufferSize = buffer->size - offset);

   cmd_buffer->state.dirty |= ANV_CMD_DIRTY_INDEX_BUFFER;
}

static VkResult
flush_compute_descriptor_set(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_device *device = cmd_buffer->device;
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   struct anv_state surfaces = { 0, }, samplers = { 0, };
   VkResult result;

   result = anv_cmd_buffer_emit_samplers(cmd_buffer,
                                         MESA_SHADER_COMPUTE, &samplers);
   if (result != VK_SUCCESS)
      return result;
   result = anv_cmd_buffer_emit_binding_table(cmd_buffer,
                                              MESA_SHADER_COMPUTE, &surfaces);
   if (result != VK_SUCCESS)
      return result;

   struct anv_state push_state = anv_cmd_buffer_cs_push_constants(cmd_buffer);

   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   const struct brw_stage_prog_data *prog_data = &cs_prog_data->base;

   unsigned local_id_dwords = cs_prog_data->local_invocation_id_regs * 8;
   unsigned push_constant_data_size =
      (prog_data->nr_params + local_id_dwords) * 4;
   unsigned reg_aligned_constant_size = ALIGN(push_constant_data_size, 32);
   unsigned push_constant_regs = reg_aligned_constant_size / 32;

   if (push_state.alloc_size) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MEDIA_CURBE_LOAD),
                     .CURBETotalDataLength = push_state.alloc_size,
                     .CURBEDataStartAddress = push_state.offset);
   }

   assert(prog_data->total_shared <= 64 * 1024);
   uint32_t slm_size = 0;
   if (prog_data->total_shared > 0) {
      /* slm_size is in 4k increments, but must be a power of 2. */
      slm_size = 4 * 1024;
      while (slm_size < prog_data->total_shared)
         slm_size <<= 1;
      slm_size /= 4 * 1024;
   }

   struct anv_state state =
      anv_state_pool_emit(&device->dynamic_state_pool,
                          GENX(INTERFACE_DESCRIPTOR_DATA), 64,
                          .KernelStartPointer = pipeline->cs_simd,
                          .KernelStartPointerHigh = 0,
                          .BindingTablePointer = surfaces.offset,
                          .BindingTableEntryCount = 0,
                          .SamplerStatePointer = samplers.offset,
                          .SamplerCount = 0,
                          .ConstantIndirectURBEntryReadLength = push_constant_regs,
                          .ConstantURBEntryReadOffset = 0,
                          .BarrierEnable = cs_prog_data->uses_barrier,
                          .SharedLocalMemorySize = slm_size,
                          .NumberofThreadsinGPGPUThreadGroup =
                             pipeline->cs_thread_width_max);

   uint32_t size = GENX(INTERFACE_DESCRIPTOR_DATA_length) * sizeof(uint32_t);
   anv_batch_emit(&cmd_buffer->batch, GENX(MEDIA_INTERFACE_DESCRIPTOR_LOAD),
                  .InterfaceDescriptorTotalLength = size,
                  .InterfaceDescriptorDataStartAddress = state.offset);

   return VK_SUCCESS;
}

void
genX(cmd_buffer_flush_compute_state)(struct anv_cmd_buffer *cmd_buffer)
{
   struct anv_pipeline *pipeline = cmd_buffer->state.compute_pipeline;
   const struct brw_cs_prog_data *cs_prog_data = get_cs_prog_data(pipeline);
   VkResult result;

   assert(pipeline->active_stages == VK_SHADER_STAGE_COMPUTE_BIT);

   bool needs_slm = cs_prog_data->base.total_shared > 0;
   genX(cmd_buffer_config_l3)(cmd_buffer, needs_slm);

   genX(flush_pipeline_select_gpgpu)(cmd_buffer);

   if (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE)
      anv_batch_emit_batch(&cmd_buffer->batch, &pipeline->batch);

   if ((cmd_buffer->state.descriptors_dirty & VK_SHADER_STAGE_COMPUTE_BIT) ||
       (cmd_buffer->state.compute_dirty & ANV_CMD_DIRTY_PIPELINE)) {
      result = flush_compute_descriptor_set(cmd_buffer);
      assert(result == VK_SUCCESS);
      cmd_buffer->state.descriptors_dirty &= ~VK_SHADER_STAGE_COMPUTE_BIT;
   }

   cmd_buffer->state.compute_dirty = 0;
}

void genX(CmdSetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     _event,
    VkPipelineStageFlags                        stageMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_event, event, _event);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL),
                  .DestinationAddressType = DAT_PPGTT,
                  .PostSyncOperation = WriteImmediateData,
                  .Address = {
                     &cmd_buffer->device->dynamic_state_block_pool.bo,
                     event->state.offset
                   },
                  .ImmediateData = VK_EVENT_SET);
}

void genX(CmdResetEvent)(
    VkCommandBuffer                             commandBuffer,
    VkEvent                                     _event,
    VkPipelineStageFlags                        stageMask)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_event, event, _event);

   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL),
                  .DestinationAddressType = DAT_PPGTT,
                  .PostSyncOperation = WriteImmediateData,
                  .Address = {
                     &cmd_buffer->device->dynamic_state_block_pool.bo,
                     event->state.offset
                   },
                  .ImmediateData = VK_EVENT_RESET);
}

void genX(CmdWaitEvents)(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    eventCount,
    const VkEvent*                              pEvents,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        destStageMask,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   for (uint32_t i = 0; i < eventCount; i++) {
      ANV_FROM_HANDLE(anv_event, event, pEvents[i]);

      anv_batch_emit(&cmd_buffer->batch, GENX(MI_SEMAPHORE_WAIT),
                     .WaitMode = PollingMode,
                     .CompareOperation = COMPARE_SAD_EQUAL_SDD,
                     .SemaphoreDataDword = VK_EVENT_SET,
                     .SemaphoreAddress = {
                        &cmd_buffer->device->dynamic_state_block_pool.bo,
                        event->state.offset
                     });
   }

   genX(CmdPipelineBarrier)(commandBuffer, srcStageMask, destStageMask,
                            false, /* byRegion */
                            memoryBarrierCount, pMemoryBarriers,
                            bufferMemoryBarrierCount, pBufferMemoryBarriers,
                            imageMemoryBarrierCount, pImageMemoryBarriers);
}
