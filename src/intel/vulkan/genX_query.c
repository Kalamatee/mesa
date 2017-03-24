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

VkResult genX(CreateQueryPool)(
    VkDevice                                    _device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   struct anv_query_pool *pool;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);

   /* Query pool slots are made up of some number of 64-bit values packed
    * tightly together.  The first 64-bit value is always the "available" bit
    * which is 0 when the query is unavailable and 1 when it is available.
    * The 64-bit values that follow are determined by the type of query.
    */
   uint32_t uint64s_per_slot = 1;

   VkQueryPipelineStatisticFlags pipeline_statistics = 0;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      /* Occlusion queries have two values: begin and end. */
      uint64s_per_slot += 2;
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      /* Timestamps just have the one timestamp value */
      uint64s_per_slot += 1;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      pipeline_statistics = pCreateInfo->pipelineStatistics;
      /* We're going to trust this field implicitly so we need to ensure that
       * no unhandled extension bits leak in.
       */
      pipeline_statistics &= ANV_PIPELINE_STATISTICS_MASK;

      /* Statistics queries have a min and max for every statistic */
      uint64s_per_slot += 2 * _mesa_bitcount(pipeline_statistics);
      break;
   default:
      assert(!"Invalid query type");
   }

   pool = vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

   pool->type = pCreateInfo->queryType;
   pool->pipeline_statistics = pipeline_statistics;
   pool->stride = uint64s_per_slot * sizeof(uint64_t);
   pool->slots = pCreateInfo->queryCount;

   uint64_t size = pool->slots * pool->stride;
   result = anv_bo_init_new(&pool->bo, device, size);
   if (result != VK_SUCCESS)
      goto fail;

   pool->bo.map = anv_gem_mmap(device, pool->bo.gem_handle, 0, size, 0);

   *pQueryPool = anv_query_pool_to_handle(pool);

   return VK_SUCCESS;

 fail:
   vk_free2(&device->alloc, pAllocator, pool);

   return result;
}

void genX(DestroyQueryPool)(
    VkDevice                                    _device,
    VkQueryPool                                 _pool,
    const VkAllocationCallbacks*                pAllocator)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_query_pool, pool, _pool);

   if (!pool)
      return;

   anv_gem_munmap(pool->bo.map, pool->bo.size);
   anv_gem_close(device, pool->bo.gem_handle);
   vk_free2(&device->alloc, pAllocator, pool);
}

static void
cpu_write_query_result(void *dst_slot, VkQueryResultFlags flags,
                       uint32_t value_index, uint64_t result)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      uint64_t *dst64 = dst_slot;
      dst64[value_index] = result;
   } else {
      uint32_t *dst32 = dst_slot;
      dst32[value_index] = result;
   }
}

VkResult genX(GetQueryPoolResults)(
    VkDevice                                    _device,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    size_t                                      dataSize,
    void*                                       pData,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
   ANV_FROM_HANDLE(anv_device, device, _device);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   int64_t timeout = INT64_MAX;
   int ret;

   assert(pool->type == VK_QUERY_TYPE_OCCLUSION ||
          pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS ||
          pool->type == VK_QUERY_TYPE_TIMESTAMP);

   if (unlikely(device->lost))
      return VK_ERROR_DEVICE_LOST;

   if (pData == NULL)
      return VK_SUCCESS;

   if (flags & VK_QUERY_RESULT_WAIT_BIT) {
      ret = anv_gem_wait(device, pool->bo.gem_handle, &timeout);
      if (ret == -1) {
         /* We don't know the real error. */
         return vk_errorf(VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "gem_wait failed %m");
      }
   }

   void *data_end = pData + dataSize;

   if (!device->info.has_llc) {
      uint64_t offset = firstQuery * pool->stride;
      uint64_t size = queryCount * pool->stride;
      anv_invalidate_range(pool->bo.map + offset,
                           MIN2(size, pool->bo.size - offset));
   }

   VkResult status = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      uint64_t *slot = pool->bo.map + (firstQuery + i) * pool->stride;

      /* Availability is always at the start of the slot */
      bool available = slot[0];

      /* From the Vulkan 1.0.42 spec:
       *
       *    "If VK_QUERY_RESULT_WAIT_BIT and VK_QUERY_RESULT_PARTIAL_BIT are
       *    both not set then no result values are written to pData for
       *    queries that are in the unavailable state at the time of the call,
       *    and vkGetQueryPoolResults returns VK_NOT_READY. However,
       *    availability state is still written to pData for those queries if
       *    VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set."
       */
      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      if (write_results) {
         switch (pool->type) {
         case VK_QUERY_TYPE_OCCLUSION: {
            cpu_write_query_result(pData, flags, 0, slot[2] - slot[1]);
            break;
         }

         case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
            uint32_t statistics = pool->pipeline_statistics;
            uint32_t idx = 0;
            while (statistics) {
               uint32_t stat = u_bit_scan(&statistics);
               uint64_t result = slot[idx * 2 + 2] - slot[idx * 2 + 1];

               /* WaDividePSInvocationCountBy4:HSW,BDW */
               if ((device->info.gen == 8 || device->info.is_haswell) &&
                   (1 << stat) == VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT)
                  result >>= 2;

               cpu_write_query_result(pData, flags, idx, result);

               idx++;
            }
            assert(idx == _mesa_bitcount(pool->pipeline_statistics));
            break;
         }

         case VK_QUERY_TYPE_TIMESTAMP: {
            cpu_write_query_result(pData, flags, 0, slot[1]);
            break;
         }
         default:
            unreachable("invalid pool type");
         }
      } else {
         status = VK_NOT_READY;
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         uint32_t idx = (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) ?
                        _mesa_bitcount(pool->pipeline_statistics) : 1;
         cpu_write_query_result(pData, flags, idx, available);
      }

      pData += stride;
      if (pData >= data_end)
         break;
   }

   return status;
}

static void
emit_ps_depth_count(struct anv_cmd_buffer *cmd_buffer,
                    struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WritePSDepthCount;
      pc.DepthStallEnable        = true;
      pc.Address                 = (struct anv_address) { bo, offset };

      if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
         pc.CommandStreamerStallEnable = true;
   }
}

static void
emit_query_availability(struct anv_cmd_buffer *cmd_buffer,
                        struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
      pc.DestinationAddressType  = DAT_PPGTT;
      pc.PostSyncOperation       = WriteImmediateData;
      pc.Address                 = (struct anv_address) { bo, offset };
      pc.ImmediateData           = 1;
   }
}

void genX(CmdResetQueryPool)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   for (uint32_t i = 0; i < queryCount; i++) {
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_DATA_IMM), sdm) {
         sdm.Address = (struct anv_address) {
            .bo = &pool->bo,
            .offset = (firstQuery + i) * pool->stride,
         };
         sdm.ImmediateData = 0;
      }
   }
}

static const uint32_t vk_pipeline_stat_to_reg[] = {
   GENX(IA_VERTICES_COUNT_num),
   GENX(IA_PRIMITIVES_COUNT_num),
   GENX(VS_INVOCATION_COUNT_num),
   GENX(GS_INVOCATION_COUNT_num),
   GENX(GS_PRIMITIVES_COUNT_num),
   GENX(CL_INVOCATION_COUNT_num),
   GENX(CL_PRIMITIVES_COUNT_num),
   GENX(PS_INVOCATION_COUNT_num),
   GENX(HS_INVOCATION_COUNT_num),
   GENX(DS_INVOCATION_COUNT_num),
   GENX(CS_INVOCATION_COUNT_num),
};

static void
emit_pipeline_stat(struct anv_cmd_buffer *cmd_buffer, uint32_t stat,
                   struct anv_bo *bo, uint32_t offset)
{
   STATIC_ASSERT(ANV_PIPELINE_STATISTICS_MASK ==
                 (1 << ARRAY_SIZE(vk_pipeline_stat_to_reg)) - 1);

   assert(stat < ARRAY_SIZE(vk_pipeline_stat_to_reg));
   uint32_t reg = vk_pipeline_stat_to_reg[stat];

   anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg,
      lrm.MemoryAddress    = (struct anv_address) { bo, offset };
   }
   anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg + 4,
      lrm.MemoryAddress    = (struct anv_address) { bo, offset + 4 };
   }
}

void genX(CmdBeginQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   /* Workaround: When meta uses the pipeline with the VS disabled, it seems
    * that the pipelining of the depth write breaks. What we see is that
    * samples from the render pass clear leaks into the first query
    * immediately after the clear. Doing a pipecontrol with a post-sync
    * operation and DepthStallEnable seems to work around the issue.
    */
   if (cmd_buffer->state.need_query_wa) {
      cmd_buffer->state.need_query_wa = false;
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DepthCacheFlushEnable   = true;
         pc.DepthStallEnable        = true;
      }
   }

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, &pool->bo, query * pool->stride + 8);
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      /* TODO: This might only be necessary for certain stats */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      uint32_t statistics = pool->pipeline_statistics;
      uint32_t offset = query * pool->stride + 8;
      while (statistics) {
         uint32_t stat = u_bit_scan(&statistics);
         emit_pipeline_stat(cmd_buffer, stat, &pool->bo, offset);
         offset += 16;
      }
      break;
   }

   default:
      unreachable("");
   }
}

void genX(CmdEndQuery)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_ps_depth_count(cmd_buffer, &pool->bo, query * pool->stride + 16);
      emit_query_availability(cmd_buffer, &pool->bo, query * pool->stride);
      break;

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      /* TODO: This might only be necessary for certain stats */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard = true;
      }

      uint32_t statistics = pool->pipeline_statistics;
      uint32_t offset = query * pool->stride + 16;
      while (statistics) {
         uint32_t stat = u_bit_scan(&statistics);
         emit_pipeline_stat(cmd_buffer, stat, &pool->bo, offset);
         offset += 16;
      }

      emit_query_availability(cmd_buffer, &pool->bo, query * pool->stride);
      break;
   }

   default:
      unreachable("");
   }
}

#define TIMESTAMP 0x2358

void genX(CmdWriteTimestamp)(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   uint32_t offset = query * pool->stride;

   assert(pool->type == VK_QUERY_TYPE_TIMESTAMP);

   switch (pipelineStage) {
   case VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = TIMESTAMP;
         srm.MemoryAddress    = (struct anv_address) { &pool->bo, offset + 8 };
      }
      anv_batch_emit(&cmd_buffer->batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = TIMESTAMP + 4;
         srm.MemoryAddress    = (struct anv_address) { &pool->bo, offset + 12 };
      }
      break;

   default:
      /* Everything else is bottom-of-pipe */
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.DestinationAddressType  = DAT_PPGTT;
         pc.PostSyncOperation       = WriteTimestamp;
         pc.Address = (struct anv_address) { &pool->bo, offset + 8 };

         if (GEN_GEN == 9 && cmd_buffer->device->info.gt == 4)
            pc.CommandStreamerStallEnable = true;
      }
      break;
   }

   emit_query_availability(cmd_buffer, &pool->bo, offset);
}

#if GEN_GEN > 7 || GEN_IS_HASWELL

#define alu_opcode(v)   __gen_uint((v),  20, 31)
#define alu_operand1(v) __gen_uint((v),  10, 19)
#define alu_operand2(v) __gen_uint((v),   0,  9)
#define alu(opcode, operand1, operand2) \
   alu_opcode(opcode) | alu_operand1(operand1) | alu_operand2(operand2)

#define OPCODE_NOOP      0x000
#define OPCODE_LOAD      0x080
#define OPCODE_LOADINV   0x480
#define OPCODE_LOAD0     0x081
#define OPCODE_LOAD1     0x481
#define OPCODE_ADD       0x100
#define OPCODE_SUB       0x101
#define OPCODE_AND       0x102
#define OPCODE_OR        0x103
#define OPCODE_XOR       0x104
#define OPCODE_STORE     0x180
#define OPCODE_STOREINV  0x580

#define OPERAND_R0   0x00
#define OPERAND_R1   0x01
#define OPERAND_R2   0x02
#define OPERAND_R3   0x03
#define OPERAND_R4   0x04
#define OPERAND_SRCA 0x20
#define OPERAND_SRCB 0x21
#define OPERAND_ACCU 0x31
#define OPERAND_ZF   0x32
#define OPERAND_CF   0x33

#define CS_GPR(n) (0x2600 + (n) * 8)

static void
emit_load_alu_reg_u64(struct anv_batch *batch, uint32_t reg,
                      struct anv_bo *bo, uint32_t offset)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg,
      lrm.MemoryAddress    = (struct anv_address) { bo, offset };
   }
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_MEM), lrm) {
      lrm.RegisterAddress  = reg + 4;
      lrm.MemoryAddress    = (struct anv_address) { bo, offset + 4 };
   }
}

static void
emit_load_alu_reg_imm32(struct anv_batch *batch, uint32_t reg, uint32_t imm)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_IMM), lri) {
      lri.RegisterOffset   = reg;
      lri.DataDWord        = imm;
   }
}

static void
emit_load_alu_reg_imm64(struct anv_batch *batch, uint32_t reg, uint64_t imm)
{
   emit_load_alu_reg_imm32(batch, reg, (uint32_t)imm);
   emit_load_alu_reg_imm32(batch, reg + 4, (uint32_t)(imm >> 32));
}

static void
emit_load_alu_reg_reg32(struct anv_batch *batch, uint32_t src, uint32_t dst)
{
   anv_batch_emit(batch, GENX(MI_LOAD_REGISTER_REG), lrr) {
      lrr.SourceRegisterAddress      = src;
      lrr.DestinationRegisterAddress = dst;
   }
}

/*
 * GPR0 = GPR0 & ((1ull << n) - 1);
 */
static void
keep_gpr0_lower_n_bits(struct anv_batch *batch, uint32_t n)
{
   assert(n < 64);
   emit_load_alu_reg_imm64(batch, CS_GPR(1), (1ull << n) - 1);

   uint32_t *dw = anv_batch_emitn(batch, 5, GENX(MI_MATH));
   dw[1] = alu(OPCODE_LOAD, OPERAND_SRCA, OPERAND_R0);
   dw[2] = alu(OPCODE_LOAD, OPERAND_SRCB, OPERAND_R1);
   dw[3] = alu(OPCODE_AND, 0, 0);
   dw[4] = alu(OPCODE_STORE, OPERAND_R0, OPERAND_ACCU);
}

/*
 * GPR0 = GPR0 << 30;
 */
static void
shl_gpr0_by_30_bits(struct anv_batch *batch)
{
   /* First we mask 34 bits of GPR0 to prevent overflow */
   keep_gpr0_lower_n_bits(batch, 34);

   const uint32_t outer_count = 5;
   const uint32_t inner_count = 6;
   STATIC_ASSERT(outer_count * inner_count == 30);
   const uint32_t cmd_len = 1 + inner_count * 4;

   /* We'll emit 5 commands, each shifting GPR0 left by 6 bits, for a total of
    * 30 left shifts.
    */
   for (int o = 0; o < outer_count; o++) {
      /* Submit one MI_MATH to shift left by 6 bits */
      uint32_t *dw = anv_batch_emitn(batch, cmd_len, GENX(MI_MATH));
      dw++;
      for (int i = 0; i < inner_count; i++, dw += 4) {
         dw[0] = alu(OPCODE_LOAD, OPERAND_SRCA, OPERAND_R0);
         dw[1] = alu(OPCODE_LOAD, OPERAND_SRCB, OPERAND_R0);
         dw[2] = alu(OPCODE_ADD, 0, 0);
         dw[3] = alu(OPCODE_STORE, OPERAND_R0, OPERAND_ACCU);
      }
   }
}

/*
 * GPR0 = GPR0 >> 2;
 *
 * Note that the upper 30 bits of GPR are lost!
 */
static void
shr_gpr0_by_2_bits(struct anv_batch *batch)
{
   shl_gpr0_by_30_bits(batch);
   emit_load_alu_reg_reg32(batch, CS_GPR(0) + 4, CS_GPR(0));
   emit_load_alu_reg_imm32(batch, CS_GPR(0) + 4, 0);
}

static void
gpu_write_query_result(struct anv_batch *batch,
                       struct anv_buffer *dst_buffer, uint32_t dst_offset,
                       VkQueryResultFlags flags,
                       uint32_t value_index, uint32_t reg)
{
   if (flags & VK_QUERY_RESULT_64_BIT)
      dst_offset += value_index * 8;
   else
      dst_offset += value_index * 4;

   anv_batch_emit(batch, GENX(MI_STORE_REGISTER_MEM), srm) {
      srm.RegisterAddress  = reg;
      srm.MemoryAddress    = (struct anv_address) {
         .bo = dst_buffer->bo,
         .offset = dst_buffer->offset + dst_offset,
      };
   }

   if (flags & VK_QUERY_RESULT_64_BIT) {
      anv_batch_emit(batch, GENX(MI_STORE_REGISTER_MEM), srm) {
         srm.RegisterAddress  = reg + 4;
         srm.MemoryAddress    = (struct anv_address) {
            .bo = dst_buffer->bo,
            .offset = dst_buffer->offset + dst_offset + 4,
         };
      }
   }
}

static void
compute_query_result(struct anv_batch *batch, uint32_t dst_reg,
                     struct anv_bo *bo, uint32_t offset)
{
   emit_load_alu_reg_u64(batch, CS_GPR(0), bo, offset);
   emit_load_alu_reg_u64(batch, CS_GPR(1), bo, offset + 8);

   /* FIXME: We need to clamp the result for 32 bit. */

   uint32_t *dw = anv_batch_emitn(batch, 5, GENX(MI_MATH));
   if (!dw) {
      anv_batch_set_error(batch, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   dw[1] = alu(OPCODE_LOAD, OPERAND_SRCA, OPERAND_R1);
   dw[2] = alu(OPCODE_LOAD, OPERAND_SRCB, OPERAND_R0);
   dw[3] = alu(OPCODE_SUB, 0, 0);
   dw[4] = alu(OPCODE_STORE, dst_reg, OPERAND_ACCU);
}

void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   ANV_FROM_HANDLE(anv_cmd_buffer, cmd_buffer, commandBuffer);
   ANV_FROM_HANDLE(anv_query_pool, pool, queryPool);
   ANV_FROM_HANDLE(anv_buffer, buffer, destBuffer);
   uint32_t slot_offset;

   if (flags & VK_QUERY_RESULT_WAIT_BIT) {
      anv_batch_emit(&cmd_buffer->batch, GENX(PIPE_CONTROL), pc) {
         pc.CommandStreamerStallEnable = true;
         pc.StallAtPixelScoreboard     = true;
      }
   }

   for (uint32_t i = 0; i < queryCount; i++) {
      slot_offset = (firstQuery + i) * pool->stride;
      switch (pool->type) {
      case VK_QUERY_TYPE_OCCLUSION:
         compute_query_result(&cmd_buffer->batch, OPERAND_R2,
                              &pool->bo, slot_offset + 8);
         gpu_write_query_result(&cmd_buffer->batch, buffer, destOffset,
                                flags, 0, CS_GPR(2));
         break;

      case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
         uint32_t statistics = pool->pipeline_statistics;
         uint32_t idx = 0;
         while (statistics) {
            uint32_t stat = u_bit_scan(&statistics);

            compute_query_result(&cmd_buffer->batch, OPERAND_R0,
                                 &pool->bo, slot_offset + idx * 16 + 8);

            /* WaDividePSInvocationCountBy4:HSW,BDW */
            if ((cmd_buffer->device->info.gen == 8 ||
                 cmd_buffer->device->info.is_haswell) &&
                (1 << stat) == VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT) {
               shr_gpr0_by_2_bits(&cmd_buffer->batch);
            }

            gpu_write_query_result(&cmd_buffer->batch, buffer, destOffset,
                                   flags, idx, CS_GPR(0));

            idx++;
         }
         assert(idx == _mesa_bitcount(pool->pipeline_statistics));
         break;
      }

      case VK_QUERY_TYPE_TIMESTAMP:
         emit_load_alu_reg_u64(&cmd_buffer->batch,
                               CS_GPR(2), &pool->bo, slot_offset + 8);
         gpu_write_query_result(&cmd_buffer->batch, buffer, destOffset,
                                flags, 0, CS_GPR(2));
         break;

      default:
         unreachable("unhandled query type");
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         uint32_t idx = (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) ?
                        _mesa_bitcount(pool->pipeline_statistics) : 1;

         emit_load_alu_reg_u64(&cmd_buffer->batch, CS_GPR(0),
                               &pool->bo, slot_offset);
         gpu_write_query_result(&cmd_buffer->batch, buffer, destOffset,
                                flags, idx, CS_GPR(0));
      }

      destOffset += destStride;
   }
}

#else
void genX(CmdCopyQueryPoolResults)(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    destBuffer,
    VkDeviceSize                                destOffset,
    VkDeviceSize                                destStride,
    VkQueryResultFlags                          flags)
{
   anv_finishme("Queries not yet supported on Ivy Bridge");
}
#endif
