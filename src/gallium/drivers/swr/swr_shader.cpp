/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 ***************************************************************************/

// llvm redefines DEBUG
#pragma push_macro("DEBUG")
#undef DEBUG
#include "JitManager.h"
#include "llvm-c/Core.h"
#include "llvm/Support/CBindingWrapping.h"
#pragma pop_macro("DEBUG")

#include "state.h"
#include "gen_state_llvm.h"
#include "builder.h"

#include "tgsi/tgsi_strings.h"
#include "util/u_format.h"
#include "util/u_prim.h"
#include "gallivm/lp_bld_init.h"
#include "gallivm/lp_bld_flow.h"
#include "gallivm/lp_bld_struct.h"
#include "gallivm/lp_bld_tgsi.h"

#include "swr_context.h"
#include "gen_swr_context_llvm.h"
#include "swr_resource.h"
#include "swr_state.h"
#include "swr_screen.h"

using namespace SwrJit;
using namespace llvm;

static unsigned
locate_linkage(ubyte name, ubyte index, struct tgsi_shader_info *info);

bool operator==(const swr_jit_fs_key &lhs, const swr_jit_fs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_vs_key &lhs, const swr_jit_vs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_fetch_key &lhs, const swr_jit_fetch_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const swr_jit_gs_key &lhs, const swr_jit_gs_key &rhs)
{
   return !memcmp(&lhs, &rhs, sizeof(lhs));
}

static void
swr_generate_sampler_key(const struct lp_tgsi_info &info,
                         struct swr_context *ctx,
                         enum pipe_shader_type shader_type,
                         struct swr_jit_sampler_key &key)
{
   key.nr_samplers = info.base.file_max[TGSI_FILE_SAMPLER] + 1;

   for (unsigned i = 0; i < key.nr_samplers; i++) {
      if (info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
         lp_sampler_static_sampler_state(
            &key.sampler[i].sampler_state,
            ctx->samplers[shader_type][i]);
      }
   }

   /*
    * XXX If TGSI_FILE_SAMPLER_VIEW exists assume all texture opcodes
    * are dx10-style? Can't really have mixed opcodes, at least not
    * if we want to skip the holes here (without rescanning tgsi).
    */
   if (info.base.file_max[TGSI_FILE_SAMPLER_VIEW] != -1) {
      key.nr_sampler_views =
         info.base.file_max[TGSI_FILE_SAMPLER_VIEW] + 1;
      for (unsigned i = 0; i < key.nr_sampler_views; i++) {
         if (info.base.file_mask[TGSI_FILE_SAMPLER_VIEW] & (1 << i)) {
            const struct pipe_sampler_view *view =
               ctx->sampler_views[shader_type][i];
            lp_sampler_static_texture_state(
               &key.sampler[i].texture_state, view);
            if (view) {
               struct swr_resource *swr_res = swr_resource(view->texture);
               const struct util_format_description *desc =
                  util_format_description(view->format);
               if (swr_res->has_depth && swr_res->has_stencil &&
                   !util_format_has_depth(desc))
                  key.sampler[i].texture_state.format = PIPE_FORMAT_S8_UINT;
            }
         }
      }
   } else {
      key.nr_sampler_views = key.nr_samplers;
      for (unsigned i = 0; i < key.nr_sampler_views; i++) {
         if (info.base.file_mask[TGSI_FILE_SAMPLER] & (1 << i)) {
            const struct pipe_sampler_view *view =
               ctx->sampler_views[shader_type][i];
            lp_sampler_static_texture_state(
               &key.sampler[i].texture_state, view);
            if (view) {
               struct swr_resource *swr_res = swr_resource(view->texture);
               const struct util_format_description *desc =
                  util_format_description(view->format);
               if (swr_res->has_depth && swr_res->has_stencil &&
                   !util_format_has_depth(desc))
                  key.sampler[i].texture_state.format = PIPE_FORMAT_S8_UINT;
            }
         }
      }
   }
}

void
swr_generate_fs_key(struct swr_jit_fs_key &key,
                    struct swr_context *ctx,
                    swr_fragment_shader *swr_fs)
{
   memset(&key, 0, sizeof(key));

   key.nr_cbufs = ctx->framebuffer.nr_cbufs;
   key.light_twoside = ctx->rasterizer->light_twoside;
   key.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable;

   struct tgsi_shader_info *pPrevShader;
   if (ctx->gs)
      pPrevShader = &ctx->gs->info.base;
   else
      pPrevShader = &ctx->vs->info.base;

   memcpy(&key.vs_output_semantic_name,
          &pPrevShader->output_semantic_name,
          sizeof(key.vs_output_semantic_name));
   memcpy(&key.vs_output_semantic_idx,
          &pPrevShader->output_semantic_index,
          sizeof(key.vs_output_semantic_idx));

   swr_generate_sampler_key(swr_fs->info, ctx, PIPE_SHADER_FRAGMENT, key);
}

void
swr_generate_vs_key(struct swr_jit_vs_key &key,
                    struct swr_context *ctx,
                    swr_vertex_shader *swr_vs)
{
   memset(&key, 0, sizeof(key));

   key.clip_plane_mask =
      swr_vs->info.base.clipdist_writemask ?
      swr_vs->info.base.clipdist_writemask & ctx->rasterizer->clip_plane_enable :
      ctx->rasterizer->clip_plane_enable;

   swr_generate_sampler_key(swr_vs->info, ctx, PIPE_SHADER_VERTEX, key);
}

void
swr_generate_fetch_key(struct swr_jit_fetch_key &key,
                       struct swr_vertex_element_state *velems)
{
   memset(&key, 0, sizeof(key));

   key.fsState = velems->fsState;
}

void
swr_generate_gs_key(struct swr_jit_gs_key &key,
                    struct swr_context *ctx,
                    swr_geometry_shader *swr_gs)
{
   memset(&key, 0, sizeof(key));

   struct tgsi_shader_info *pPrevShader = &ctx->vs->info.base;

   memcpy(&key.vs_output_semantic_name,
          &pPrevShader->output_semantic_name,
          sizeof(key.vs_output_semantic_name));
   memcpy(&key.vs_output_semantic_idx,
          &pPrevShader->output_semantic_index,
          sizeof(key.vs_output_semantic_idx));

   swr_generate_sampler_key(swr_gs->info, ctx, PIPE_SHADER_GEOMETRY, key);
}

struct BuilderSWR : public Builder {
   BuilderSWR(JitManager *pJitMgr, const char *pName)
      : Builder(pJitMgr)
   {
      pJitMgr->SetupNewModule();
      gallivm = gallivm_create(pName, wrap(&JM()->mContext));
      pJitMgr->mpCurrentModule = unwrap(gallivm->module);
   }

   ~BuilderSWR() {
      gallivm_free_ir(gallivm);
   }

   struct gallivm_state *gallivm;
   PFN_VERTEX_FUNC CompileVS(struct swr_context *ctx, swr_jit_vs_key &key);
   PFN_PIXEL_KERNEL CompileFS(struct swr_context *ctx, swr_jit_fs_key &key);
   PFN_GS_FUNC CompileGS(struct swr_context *ctx, swr_jit_gs_key &key);

   LLVMValueRef
   swr_gs_llvm_fetch_input(const struct lp_build_tgsi_gs_iface *gs_iface,
                           struct lp_build_tgsi_context * bld_base,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index);
   void
   swr_gs_llvm_emit_vertex(const struct lp_build_tgsi_gs_iface *gs_base,
                           struct lp_build_tgsi_context * bld_base,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec);

   void
   swr_gs_llvm_end_primitive(const struct lp_build_tgsi_gs_iface *gs_base,
                             struct lp_build_tgsi_context * bld_base,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec);

   void
   swr_gs_llvm_epilogue(const struct lp_build_tgsi_gs_iface *gs_base,
                        struct lp_build_tgsi_context * bld_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec);

};

struct swr_gs_llvm_iface {
   struct lp_build_tgsi_gs_iface base;
   struct tgsi_shader_info *info;

   BuilderSWR *pBuilder;

   Value *pGsCtx;
   SWR_GS_STATE *pGsState;
   uint32_t num_outputs;
   uint32_t num_verts_per_prim;

   Value *pVtxAttribMap;
};

// trampoline functions so we can use the builder llvm construction methods
static LLVMValueRef
swr_gs_llvm_fetch_input(const struct lp_build_tgsi_gs_iface *gs_iface,
                           struct lp_build_tgsi_context * bld_base,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_iface;

    return iface->pBuilder->swr_gs_llvm_fetch_input(gs_iface, bld_base,
                                                   is_vindex_indirect,
                                                   vertex_index,
                                                   is_aindex_indirect,
                                                   attrib_index,
                                                   swizzle_index);
}

static void
swr_gs_llvm_emit_vertex(const struct lp_build_tgsi_gs_iface *gs_base,
                           struct lp_build_tgsi_context * bld_base,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_emit_vertex(gs_base, bld_base,
                                            outputs,
                                            emitted_vertices_vec);
}

static void
swr_gs_llvm_end_primitive(const struct lp_build_tgsi_gs_iface *gs_base,
                             struct lp_build_tgsi_context * bld_base,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_end_primitive(gs_base, bld_base,
                                              verts_per_prim_vec,
                                              emitted_prims_vec);
}

static void
swr_gs_llvm_epilogue(const struct lp_build_tgsi_gs_iface *gs_base,
                        struct lp_build_tgsi_context * bld_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;

    iface->pBuilder->swr_gs_llvm_epilogue(gs_base, bld_base,
                                         total_emitted_vertices_vec,
                                         emitted_prims_vec);
}

LLVMValueRef
BuilderSWR::swr_gs_llvm_fetch_input(const struct lp_build_tgsi_gs_iface *gs_iface,
                           struct lp_build_tgsi_context * bld_base,
                           boolean is_vindex_indirect,
                           LLVMValueRef vertex_index,
                           boolean is_aindex_indirect,
                           LLVMValueRef attrib_index,
                           LLVMValueRef swizzle_index)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_iface;

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    assert(is_vindex_indirect == false && is_aindex_indirect == false);

    Value *attrib =
       LOAD(GEP(iface->pVtxAttribMap, {C(0), unwrap(attrib_index)}));

    Value *pInput =
       LOAD(GEP(iface->pGsCtx,
                {C(0),
                 C(SWR_GS_CONTEXT_vert),
                 unwrap(vertex_index),
                 C(0),
                 attrib,
                 unwrap(swizzle_index)}));

    return wrap(pInput);
}

void
BuilderSWR::swr_gs_llvm_emit_vertex(const struct lp_build_tgsi_gs_iface *gs_base,
                           struct lp_build_tgsi_context * bld_base,
                           LLVMValueRef (*outputs)[4],
                           LLVMValueRef emitted_vertices_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;
    SWR_GS_STATE *pGS = iface->pGsState;

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    const uint32_t simdVertexStride = sizeof(simdvertex);
    const uint32_t numSimdBatches = (pGS->maxNumVerts + 7) / 8;
    const uint32_t inputPrimStride = numSimdBatches * simdVertexStride;

    Value *pStream = LOAD(iface->pGsCtx, { 0, SWR_GS_CONTEXT_pStream });
    Value *vMask = LOAD(iface->pGsCtx, { 0, SWR_GS_CONTEXT_mask });
    Value *vMask1 = TRUNC(vMask, VectorType::get(mInt1Ty, 8));

    Value *vOffsets = C({
          inputPrimStride * 0,
          inputPrimStride * 1,
          inputPrimStride * 2,
          inputPrimStride * 3,
          inputPrimStride * 4,
          inputPrimStride * 5,
          inputPrimStride * 6,
          inputPrimStride * 7 } );

    Value *vVertexSlot = ASHR(unwrap(emitted_vertices_vec), 3);
    Value *vSimdSlot = AND(unwrap(emitted_vertices_vec), 7);

    for (uint32_t attrib = 0; attrib < iface->num_outputs; ++attrib) {
       uint32_t attribSlot = attrib;
       if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_PSIZE)
          attribSlot = VERTEX_POINT_SIZE_SLOT;
       else if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_PRIMID)
          attribSlot = VERTEX_PRIMID_SLOT;
       else if (iface->info->output_semantic_name[attrib] == TGSI_SEMANTIC_LAYER)
          attribSlot = VERTEX_RTAI_SLOT;

       Value *vOffsetsAttrib =
          ADD(vOffsets, MUL(vVertexSlot, VIMMED1((uint32_t)sizeof(simdvertex))));
       vOffsetsAttrib =
          ADD(vOffsetsAttrib, VIMMED1((uint32_t)(attribSlot*sizeof(simdvector))));
       vOffsetsAttrib =
          ADD(vOffsetsAttrib, MUL(vSimdSlot, VIMMED1((uint32_t)sizeof(float))));

       for (uint32_t channel = 0; channel < 4; ++channel) {
          Value *vData = LOAD(unwrap(outputs[attrib][channel]));
          Value *vPtrs = GEP(pStream, vOffsetsAttrib);

          vPtrs = BITCAST(vPtrs,
                          VectorType::get(PointerType::get(mFP32Ty, 0), 8));

          MASKED_SCATTER(vData, vPtrs, 32, vMask1);

          vOffsetsAttrib =
             ADD(vOffsetsAttrib, VIMMED1((uint32_t)sizeof(simdscalar)));
       }
    }
}

void
BuilderSWR::swr_gs_llvm_end_primitive(const struct lp_build_tgsi_gs_iface *gs_base,
                             struct lp_build_tgsi_context * bld_base,
                             LLVMValueRef verts_per_prim_vec,
                             LLVMValueRef emitted_prims_vec)
{
    swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;
    SWR_GS_STATE *pGS = iface->pGsState;

    IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

    Value *pCutBuffer =
       LOAD(iface->pGsCtx, {0, SWR_GS_CONTEXT_pCutOrStreamIdBuffer});
    Value *vMask = LOAD(iface->pGsCtx, { 0, SWR_GS_CONTEXT_mask });
    Value *vMask1 = TRUNC(vMask, VectorType::get(mInt1Ty, 8));

    uint32_t vertsPerPrim = iface->num_verts_per_prim;

    Value *vCount =
       ADD(MUL(unwrap(emitted_prims_vec), VIMMED1(vertsPerPrim)),
           unwrap(verts_per_prim_vec));

    struct lp_build_tgsi_soa_context *bld = lp_soa_context(bld_base);
    vCount = LOAD(unwrap(bld->total_emitted_vertices_vec_ptr));

    struct lp_exec_mask *exec_mask = &bld->exec_mask;
    Value *mask = unwrap(lp_build_mask_value(bld->mask));
    if (exec_mask->has_mask)
       mask = AND(mask, unwrap(exec_mask->exec_mask));

    Value *cmpMask = VMASK(ICMP_NE(unwrap(verts_per_prim_vec), VIMMED1(0)));
    mask = AND(mask, cmpMask);
    vMask1 = TRUNC(mask, VectorType::get(mInt1Ty, 8));

    const uint32_t cutPrimStride =
       (pGS->maxNumVerts + JM()->mVWidth - 1) / JM()->mVWidth;
    Value *vOffsets = C({
          (uint32_t)(cutPrimStride * 0),
          (uint32_t)(cutPrimStride * 1),
          (uint32_t)(cutPrimStride * 2),
          (uint32_t)(cutPrimStride * 3),
          (uint32_t)(cutPrimStride * 4),
          (uint32_t)(cutPrimStride * 5),
          (uint32_t)(cutPrimStride * 6),
          (uint32_t)(cutPrimStride * 7) } );

    vCount = SUB(vCount, VIMMED1(1));
    Value *vOffset = ADD(UDIV(vCount, VIMMED1(8)), vOffsets);
    Value *vValue = SHL(VIMMED1(1), UREM(vCount, VIMMED1(8)));

    vValue = TRUNC(vValue, VectorType::get(mInt8Ty, 8));

    Value *vPtrs = GEP(pCutBuffer, vOffset);
    vPtrs =
       BITCAST(vPtrs, VectorType::get(PointerType::get(mInt8Ty, 0), JM()->mVWidth));

    Value *vGather = MASKED_GATHER(vPtrs, 32, vMask1);
    vValue = OR(vGather, vValue);
    MASKED_SCATTER(vValue, vPtrs, 32, vMask1);
}

void
BuilderSWR::swr_gs_llvm_epilogue(const struct lp_build_tgsi_gs_iface *gs_base,
                        struct lp_build_tgsi_context * bld_base,
                        LLVMValueRef total_emitted_vertices_vec,
                        LLVMValueRef emitted_prims_vec)
{
   swr_gs_llvm_iface *iface = (swr_gs_llvm_iface*)gs_base;
   SWR_GS_STATE *pGS = iface->pGsState;

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   STORE(unwrap(total_emitted_vertices_vec), iface->pGsCtx, {0, SWR_GS_CONTEXT_vertexCount});
}

PFN_GS_FUNC
BuilderSWR::CompileGS(struct swr_context *ctx, swr_jit_gs_key &key)
{
   SWR_GS_STATE *pGS = &ctx->gs->gsState;
   struct tgsi_shader_info *info = &ctx->gs->info.base;

   pGS->gsEnable = true;

   pGS->numInputAttribs = info->num_inputs;
   pGS->outputTopology =
      swr_convert_prim_topology(info->properties[TGSI_PROPERTY_GS_OUTPUT_PRIM]);
   pGS->maxNumVerts = info->properties[TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES];
   pGS->instanceCount = info->properties[TGSI_PROPERTY_GS_INVOCATIONS];

   pGS->emitsRenderTargetArrayIndex = info->writes_layer;
   pGS->emitsPrimitiveID = info->writes_primid;
   pGS->emitsViewportArrayIndex = info->writes_viewport_index;

   // XXX: single stream for now...
   pGS->isSingleStream = true;
   pGS->singleStreamID = 0;

   struct swr_geometry_shader *gs = ctx->gs;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);

   std::vector<Type *> gsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(Gen_SWR_GS_CONTEXT(JM()), 0)};
   FunctionType *vsFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), gsArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(vsFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "GS",
                                     JM()->mpCurrentModule);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pGsCtx = &*argitr++;
   pGsCtx->setName("gsCtx");

   Value *consts_ptr =
      GEP(hPrivateData, {C(0), C(swr_draw_context_constantGS)});
   consts_ptr->setName("gs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsGS});
   const_sizes_ptr->setName("num_gs_constants");

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_GEOMETRY);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));
   system_values.prim_id = wrap(LOAD(pGsCtx, {0, SWR_GS_CONTEXT_PrimitiveID}));
   system_values.instance_id = wrap(LOAD(pGsCtx, {0, SWR_GS_CONTEXT_InstanceID}));

   std::vector<Constant*> mapConstants;
   Value *vtxAttribMap = ALLOCA(ArrayType::get(mInt32Ty, PIPE_MAX_SHADER_INPUTS));
   for (unsigned slot = 0; slot < info->num_inputs; slot++) {
      ubyte semantic_name = info->input_semantic_name[slot];
      ubyte semantic_idx = info->input_semantic_index[slot];

      unsigned vs_slot =
         locate_linkage(semantic_name, semantic_idx, &ctx->vs->info.base) + 1;

      STORE(C(vs_slot), vtxAttribMap, {0, slot});
      mapConstants.push_back(C(vs_slot));
   }

   struct lp_build_mask_context mask;
   Value *mask_val = LOAD(pGsCtx, {0, SWR_GS_CONTEXT_mask}, "gsMask");
   lp_build_mask_begin(&mask, gallivm,
                       lp_type_float_vec(32, 32 * 8), wrap(mask_val));

   // zero out cut buffer so we can load/modify/store bits
   MEMSET(LOAD(pGsCtx, {0, SWR_GS_CONTEXT_pCutOrStreamIdBuffer}),
          C((char)0),
          pGS->instanceCount * ((pGS->maxNumVerts + 7) / 8) * JM()->mVWidth,
          sizeof(float) * KNOB_SIMD_WIDTH);

   struct swr_gs_llvm_iface gs_iface;
   gs_iface.base.fetch_input = ::swr_gs_llvm_fetch_input;
   gs_iface.base.emit_vertex = ::swr_gs_llvm_emit_vertex;
   gs_iface.base.end_primitive = ::swr_gs_llvm_end_primitive;
   gs_iface.base.gs_epilogue = ::swr_gs_llvm_epilogue;
   gs_iface.pBuilder = this;
   gs_iface.pGsCtx = pGsCtx;
   gs_iface.pGsState = pGS;
   gs_iface.num_outputs = gs->info.base.num_outputs;
   gs_iface.num_verts_per_prim =
      u_vertices_per_prim((pipe_prim_type)info->properties[TGSI_PROPERTY_GS_OUTPUT_PRIM]);
   gs_iface.info = info;
   gs_iface.pVtxAttribMap = vtxAttribMap;

   lp_build_tgsi_soa(gallivm,
                     gs->pipe.tokens,
                     lp_type_float_vec(32, 32 * 8),
                     &mask,
                     wrap(consts_ptr),
                     wrap(const_sizes_ptr),
                     &system_values,
                     inputs,
                     outputs,
                     wrap(hPrivateData), // (sampler context)
                     NULL, // thread data
                     sampler,
                     &gs->info.base,
                     &gs_iface.base);

   lp_build_mask_end(&mask);

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   RET_VOID();

   gallivm_verify_function(gallivm, wrap(pFunction));
   gallivm_compile_module(gallivm);

   PFN_GS_FUNC pFunc =
      (PFN_GS_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   debug_printf("geom shader  %p\n", pFunc);
   assert(pFunc && "Error: GeomShader = NULL");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}

PFN_GS_FUNC
swr_compile_gs(struct swr_context *ctx, swr_jit_gs_key &key)
{
   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "GS");
   PFN_GS_FUNC func = builder.CompileGS(ctx, key);

   ctx->gs->map.insert(std::make_pair(key, make_unique<VariantGS>(builder.gallivm, func)));
   return func;
}

PFN_VERTEX_FUNC
BuilderSWR::CompileVS(struct swr_context *ctx, swr_jit_vs_key &key)
{
   struct swr_vertex_shader *swr_vs = ctx->vs;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(outputs, 0, sizeof(outputs));

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);

   std::vector<Type *> vsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(Gen_SWR_VS_CONTEXT(JM()), 0)};
   FunctionType *vsFuncType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), vsArgs, false);

   // create new vertex shader function
   auto pFunction = Function::Create(vsFuncType,
                                     GlobalValue::ExternalLinkage,
                                     "VS",
                                     JM()->mpCurrentModule);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto argitr = pFunction->arg_begin();
   Value *hPrivateData = &*argitr++;
   hPrivateData->setName("hPrivateData");
   Value *pVsCtx = &*argitr++;
   pVsCtx->setName("vsCtx");
   
   Value *consts_ptr = GEP(hPrivateData, {C(0), C(swr_draw_context_constantVS)});

   consts_ptr->setName("vs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsVS});
   const_sizes_ptr->setName("num_vs_constants");

   Value *vtxInput = LOAD(pVsCtx, {0, SWR_VS_CONTEXT_pVin});

   for (uint32_t attrib = 0; attrib < PIPE_MAX_SHADER_INPUTS; attrib++) {
      const unsigned mask = swr_vs->info.base.input_usage_mask[attrib];
      for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
         if (mask & (1 << channel)) {
            inputs[attrib][channel] =
               wrap(LOAD(vtxInput, {0, 0, attrib, channel}));
         }
      }
   }

   struct lp_build_sampler_soa *sampler =
      swr_sampler_soa_create(key.sampler, PIPE_SHADER_VERTEX);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));
   system_values.instance_id = wrap(LOAD(pVsCtx, {0, SWR_VS_CONTEXT_InstanceID}));
   system_values.vertex_id = wrap(LOAD(pVsCtx, {0, SWR_VS_CONTEXT_VertexID}));

   lp_build_tgsi_soa(gallivm,
                     swr_vs->pipe.tokens,
                     lp_type_float_vec(32, 32 * 8),
                     NULL, // mask
                     wrap(consts_ptr),
                     wrap(const_sizes_ptr),
                     &system_values,
                     inputs,
                     outputs,
                     wrap(hPrivateData), // (sampler context)
                     NULL, // thread data
                     sampler, // sampler
                     &swr_vs->info.base,
                     NULL); // geometry shader face

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   Value *vtxOutput = LOAD(pVsCtx, {0, SWR_VS_CONTEXT_pVout});

   for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
      for (uint32_t attrib = 0; attrib < PIPE_MAX_SHADER_OUTPUTS; attrib++) {
         if (!outputs[attrib][channel])
            continue;

         Value *val = LOAD(unwrap(outputs[attrib][channel]));

         uint32_t outSlot = attrib;
         if (swr_vs->info.base.output_semantic_name[attrib] == TGSI_SEMANTIC_PSIZE)
            outSlot = VERTEX_POINT_SIZE_SLOT;
         STORE(val, vtxOutput, {0, 0, outSlot, channel});
      }
   }

   if (ctx->rasterizer->clip_plane_enable ||
       swr_vs->info.base.culldist_writemask) {
      unsigned clip_mask = ctx->rasterizer->clip_plane_enable;

      unsigned cv = 0;
      if (swr_vs->info.base.writes_clipvertex) {
         cv = 1 + locate_linkage(TGSI_SEMANTIC_CLIPVERTEX, 0,
                                 &swr_vs->info.base);
      } else {
         for (int i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
            if (swr_vs->info.base.output_semantic_name[i] == TGSI_SEMANTIC_POSITION &&
                swr_vs->info.base.output_semantic_index[i] == 0) {
               cv = i;
               break;
            }
         }
      }
      LLVMValueRef cx = LLVMBuildLoad(gallivm->builder, outputs[cv][0], "");
      LLVMValueRef cy = LLVMBuildLoad(gallivm->builder, outputs[cv][1], "");
      LLVMValueRef cz = LLVMBuildLoad(gallivm->builder, outputs[cv][2], "");
      LLVMValueRef cw = LLVMBuildLoad(gallivm->builder, outputs[cv][3], "");

      for (unsigned val = 0; val < PIPE_MAX_CLIP_PLANES; val++) {
         // clip distance overrides user clip planes
         if ((swr_vs->info.base.clipdist_writemask & clip_mask & (1 << val)) ||
             ((swr_vs->info.base.culldist_writemask << swr_vs->info.base.num_written_clipdistance) & (1 << val))) {
            unsigned cv = 1 + locate_linkage(TGSI_SEMANTIC_CLIPDIST, val < 4 ? 0 : 1,
                                             &swr_vs->info.base);
            if (val < 4) {
               LLVMValueRef dist = LLVMBuildLoad(gallivm->builder, outputs[cv][val], "");
               STORE(unwrap(dist), vtxOutput, {0, 0, VERTEX_CLIPCULL_DIST_LO_SLOT, val});
            } else {
               LLVMValueRef dist = LLVMBuildLoad(gallivm->builder, outputs[cv][val - 4], "");
               STORE(unwrap(dist), vtxOutput, {0, 0, VERTEX_CLIPCULL_DIST_HI_SLOT, val - 4});
            }
            continue;
         }

         if (!(clip_mask & (1 << val)))
            continue;

         Value *px = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 0}));
         Value *py = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 1}));
         Value *pz = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 2}));
         Value *pw = LOAD(GEP(hPrivateData, {0, swr_draw_context_userClipPlanes, val, 3}));
         Value *dist = FADD(FMUL(unwrap(cx), VBROADCAST(px)),
                            FADD(FMUL(unwrap(cy), VBROADCAST(py)),
                                 FADD(FMUL(unwrap(cz), VBROADCAST(pz)),
                                      FMUL(unwrap(cw), VBROADCAST(pw)))));

         if (val < 4)
            STORE(dist, vtxOutput, {0, 0, VERTEX_CLIPCULL_DIST_LO_SLOT, val});
         else
            STORE(dist, vtxOutput, {0, 0, VERTEX_CLIPCULL_DIST_HI_SLOT, val - 4});
      }
   }

   RET_VOID();

   gallivm_verify_function(gallivm, wrap(pFunction));
   gallivm_compile_module(gallivm);

   //   lp_debug_dump_value(func);

   PFN_VERTEX_FUNC pFunc =
      (PFN_VERTEX_FUNC)gallivm_jit_function(gallivm, wrap(pFunction));

   debug_printf("vert shader  %p\n", pFunc);
   assert(pFunc && "Error: VertShader = NULL");

   JM()->mIsModuleFinalized = true;

   return pFunc;
}

PFN_VERTEX_FUNC
swr_compile_vs(struct swr_context *ctx, swr_jit_vs_key &key)
{
   if (!ctx->vs->pipe.tokens)
      return NULL;

   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "VS");
   PFN_VERTEX_FUNC func = builder.CompileVS(ctx, key);

   ctx->vs->map.insert(std::make_pair(key, make_unique<VariantVS>(builder.gallivm, func)));
   return func;
}

static unsigned
locate_linkage(ubyte name, ubyte index, struct tgsi_shader_info *info)
{
   for (int i = 0; i < PIPE_MAX_SHADER_OUTPUTS; i++) {
      if ((info->output_semantic_name[i] == name)
          && (info->output_semantic_index[i] == index)) {
         return i - 1; // position is not part of the linkage
      }
   }

   return 0xFFFFFFFF;
}

PFN_PIXEL_KERNEL
BuilderSWR::CompileFS(struct swr_context *ctx, swr_jit_fs_key &key)
{
   struct swr_fragment_shader *swr_fs = ctx->fs;

   struct tgsi_shader_info *pPrevShader;
   if (ctx->gs)
      pPrevShader = &ctx->gs->info.base;
   else
      pPrevShader = &ctx->vs->info.base;

   LLVMValueRef inputs[PIPE_MAX_SHADER_INPUTS][TGSI_NUM_CHANNELS];
   LLVMValueRef outputs[PIPE_MAX_SHADER_OUTPUTS][TGSI_NUM_CHANNELS];

   memset(inputs, 0, sizeof(inputs));
   memset(outputs, 0, sizeof(outputs));

   struct lp_build_sampler_soa *sampler = NULL;

   AttrBuilder attrBuilder;
   attrBuilder.addStackAlignmentAttr(JM()->mVWidth * sizeof(float));
   AttributeSet attrSet = AttributeSet::get(
      JM()->mContext, AttributeSet::FunctionIndex, attrBuilder);

   std::vector<Type *> fsArgs{PointerType::get(Gen_swr_draw_context(JM()), 0),
                              PointerType::get(Gen_SWR_PS_CONTEXT(JM()), 0)};
   FunctionType *funcType =
      FunctionType::get(Type::getVoidTy(JM()->mContext), fsArgs, false);

   auto pFunction = Function::Create(funcType,
                                     GlobalValue::ExternalLinkage,
                                     "FS",
                                     JM()->mpCurrentModule);
   pFunction->addAttributes(AttributeSet::FunctionIndex, attrSet);

   BasicBlock *block = BasicBlock::Create(JM()->mContext, "entry", pFunction);
   IRB()->SetInsertPoint(block);
   LLVMPositionBuilderAtEnd(gallivm->builder, wrap(block));

   auto args = pFunction->arg_begin();
   Value *hPrivateData = &*args++;
   hPrivateData->setName("hPrivateData");
   Value *pPS = &*args++;
   pPS->setName("psCtx");

   Value *consts_ptr = GEP(hPrivateData, {0, swr_draw_context_constantFS});
   consts_ptr->setName("fs_constants");
   Value *const_sizes_ptr =
      GEP(hPrivateData, {0, swr_draw_context_num_constantsFS});
   const_sizes_ptr->setName("num_fs_constants");

   // load *pAttribs, *pPerspAttribs
   Value *pRawAttribs = LOAD(pPS, {0, SWR_PS_CONTEXT_pAttribs}, "pRawAttribs");
   Value *pPerspAttribs =
      LOAD(pPS, {0, SWR_PS_CONTEXT_pPerspAttribs}, "pPerspAttribs");

   swr_fs->constantMask = 0;
   swr_fs->flatConstantMask = 0;
   swr_fs->pointSpriteMask = 0;

   for (int attrib = 0; attrib < PIPE_MAX_SHADER_INPUTS; attrib++) {
      const unsigned mask = swr_fs->info.base.input_usage_mask[attrib];
      const unsigned interpMode = swr_fs->info.base.input_interpolate[attrib];
      const unsigned interpLoc = swr_fs->info.base.input_interpolate_loc[attrib];

      if (!mask)
         continue;

      // load i,j
      Value *vi = nullptr, *vj = nullptr;
      switch (interpLoc) {
      case TGSI_INTERPOLATE_LOC_CENTER:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_center}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_center}, "j");
         break;
      case TGSI_INTERPOLATE_LOC_CENTROID:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_centroid}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_centroid}, "j");
         break;
      case TGSI_INTERPOLATE_LOC_SAMPLE:
         vi = LOAD(pPS, {0, SWR_PS_CONTEXT_vI, PixelPositions_sample}, "i");
         vj = LOAD(pPS, {0, SWR_PS_CONTEXT_vJ, PixelPositions_sample}, "j");
         break;
      }

      // load/compute w
      Value *vw = nullptr, *pAttribs;
      if (interpMode == TGSI_INTERPOLATE_PERSPECTIVE ||
          interpMode == TGSI_INTERPOLATE_COLOR) {
         pAttribs = pPerspAttribs;
         switch (interpLoc) {
         case TGSI_INTERPOLATE_LOC_CENTER:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_center}));
            break;
         case TGSI_INTERPOLATE_LOC_CENTROID:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_centroid}));
            break;
         case TGSI_INTERPOLATE_LOC_SAMPLE:
            vw = VRCP(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_sample}));
            break;
         }
      } else {
         pAttribs = pRawAttribs;
         vw = VIMMED1(1.f);
      }

      vw->setName("w");

      ubyte semantic_name = swr_fs->info.base.input_semantic_name[attrib];
      ubyte semantic_idx = swr_fs->info.base.input_semantic_index[attrib];

      if (semantic_name == TGSI_SEMANTIC_FACE) {
         Value *ff =
            UI_TO_FP(LOAD(pPS, {0, SWR_PS_CONTEXT_frontFace}), mFP32Ty);
         ff = FSUB(FMUL(ff, C(2.0f)), C(1.0f));
         ff = VECTOR_SPLAT(JM()->mVWidth, ff, "vFrontFace");

         inputs[attrib][0] = wrap(ff);
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(1.0f));
         continue;
      } else if (semantic_name == TGSI_SEMANTIC_POSITION) { // gl_FragCoord
         if (swr_fs->info.base.properties[TGSI_PROPERTY_FS_COORD_PIXEL_CENTER] ==
             TGSI_FS_COORD_PIXEL_CENTER_HALF_INTEGER) {
            inputs[attrib][0] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vX, PixelPositions_center}, "vX"));
            inputs[attrib][1] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vY, PixelPositions_center}, "vY"));
         } else {
            inputs[attrib][0] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vX, PixelPositions_UL}, "vX"));
            inputs[attrib][1] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vY, PixelPositions_UL}, "vY"));
         }
         inputs[attrib][2] = wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vZ}, "vZ"));
         inputs[attrib][3] =
            wrap(LOAD(pPS, {0, SWR_PS_CONTEXT_vOneOverW, PixelPositions_center}, "vOneOverW"));
         continue;
      } else if (semantic_name == TGSI_SEMANTIC_PRIMID) {
         Value *primID = LOAD(pPS, {0, SWR_PS_CONTEXT_primID}, "primID");
         inputs[attrib][0] = wrap(VECTOR_SPLAT(JM()->mVWidth, primID));
         inputs[attrib][1] = wrap(VIMMED1(0));
         inputs[attrib][2] = wrap(VIMMED1(0));
         inputs[attrib][3] = wrap(VIMMED1(0));
         continue;
      }

      unsigned linkedAttrib =
         locate_linkage(semantic_name, semantic_idx, pPrevShader);

      if (semantic_name == TGSI_SEMANTIC_GENERIC &&
          key.sprite_coord_enable & (1 << semantic_idx)) {
         /* we add an extra attrib to the backendState in swr_update_derived. */
         linkedAttrib = pPrevShader->num_outputs - 1;
         swr_fs->pointSpriteMask |= (1 << linkedAttrib);
      } else if (linkedAttrib == 0xFFFFFFFF) {
         inputs[attrib][0] = wrap(VIMMED1(0.0f));
         inputs[attrib][1] = wrap(VIMMED1(0.0f));
         inputs[attrib][2] = wrap(VIMMED1(0.0f));
         inputs[attrib][3] = wrap(VIMMED1(1.0f));
         /* If we're reading in color and 2-sided lighting is enabled, we have
          * to keep going.
          */
         if (semantic_name != TGSI_SEMANTIC_COLOR || !key.light_twoside)
            continue;
      } else {
         if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
            swr_fs->constantMask |= 1 << linkedAttrib;
         } else if (interpMode == TGSI_INTERPOLATE_COLOR) {
            swr_fs->flatConstantMask |= 1 << linkedAttrib;
         }
      }

      unsigned bcolorAttrib = 0xFFFFFFFF;
      Value *offset = NULL;
      if (semantic_name == TGSI_SEMANTIC_COLOR && key.light_twoside) {
         bcolorAttrib = locate_linkage(
               TGSI_SEMANTIC_BCOLOR, semantic_idx, pPrevShader);
         /* Neither front nor back colors were available. Nothing to load. */
         if (bcolorAttrib == 0xFFFFFFFF && linkedAttrib == 0xFFFFFFFF)
            continue;
         /* If there is no front color, just always use the back color. */
         if (linkedAttrib == 0xFFFFFFFF)
            linkedAttrib = bcolorAttrib;

         if (bcolorAttrib != 0xFFFFFFFF) {
            if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
               swr_fs->constantMask |= 1 << bcolorAttrib;
            } else if (interpMode == TGSI_INTERPOLATE_COLOR) {
               swr_fs->flatConstantMask |= 1 << bcolorAttrib;
            }

            unsigned diff = 12 * (bcolorAttrib - linkedAttrib);

            if (diff) {
               Value *back =
                  XOR(C(1), LOAD(pPS, {0, SWR_PS_CONTEXT_frontFace}), "backFace");

               offset = MUL(back, C(diff));
               offset->setName("offset");
            }
         }
      }

      for (int channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
         if (mask & (1 << channel)) {
            Value *indexA = C(linkedAttrib * 12 + channel);
            Value *indexB = C(linkedAttrib * 12 + channel + 4);
            Value *indexC = C(linkedAttrib * 12 + channel + 8);

            if (offset) {
               indexA = ADD(indexA, offset);
               indexB = ADD(indexB, offset);
               indexC = ADD(indexC, offset);
            }

            Value *va = VBROADCAST(LOAD(GEP(pAttribs, indexA)));
            Value *vb = VBROADCAST(LOAD(GEP(pAttribs, indexB)));
            Value *vc = VBROADCAST(LOAD(GEP(pAttribs, indexC)));

            if (interpMode == TGSI_INTERPOLATE_CONSTANT) {
               inputs[attrib][channel] = wrap(va);
            } else {
               Value *vk = FSUB(FSUB(VIMMED1(1.0f), vi), vj);

               vc = FMUL(vk, vc);

               Value *interp = FMUL(va, vi);
               Value *interp1 = FMUL(vb, vj);
               interp = FADD(interp, interp1);
               interp = FADD(interp, vc);
               if (interpMode == TGSI_INTERPOLATE_PERSPECTIVE ||
                   interpMode == TGSI_INTERPOLATE_COLOR)
                  interp = FMUL(interp, vw);
               inputs[attrib][channel] = wrap(interp);
            }
         }
      }
   }

   sampler = swr_sampler_soa_create(key.sampler, PIPE_SHADER_FRAGMENT);

   struct lp_bld_tgsi_system_values system_values;
   memset(&system_values, 0, sizeof(system_values));

   struct lp_build_mask_context mask;

   if (swr_fs->info.base.uses_kill) {
      Value *mask_val = LOAD(pPS, {0, SWR_PS_CONTEXT_activeMask}, "activeMask");
      lp_build_mask_begin(
         &mask, gallivm, lp_type_float_vec(32, 32 * 8), wrap(mask_val));
   }

   lp_build_tgsi_soa(gallivm,
                     swr_fs->pipe.tokens,
                     lp_type_float_vec(32, 32 * 8),
                     swr_fs->info.base.uses_kill ? &mask : NULL, // mask
                     wrap(consts_ptr),
                     wrap(const_sizes_ptr),
                     &system_values,
                     inputs,
                     outputs,
                     wrap(hPrivateData),
                     NULL, // thread data
                     sampler, // sampler
                     &swr_fs->info.base,
                     NULL); // geometry shader face

   sampler->destroy(sampler);

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   for (uint32_t attrib = 0; attrib < swr_fs->info.base.num_outputs;
        attrib++) {
      switch (swr_fs->info.base.output_semantic_name[attrib]) {
      case TGSI_SEMANTIC_POSITION: {
         // write z
         LLVMValueRef outZ =
            LLVMBuildLoad(gallivm->builder, outputs[attrib][2], "");
         STORE(unwrap(outZ), pPS, {0, SWR_PS_CONTEXT_vZ});
         break;
      }
      case TGSI_SEMANTIC_COLOR: {
         for (uint32_t channel = 0; channel < TGSI_NUM_CHANNELS; channel++) {
            if (!outputs[attrib][channel])
               continue;

            LLVMValueRef out =
               LLVMBuildLoad(gallivm->builder, outputs[attrib][channel], "");
            if (swr_fs->info.base.properties[TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS] &&
                swr_fs->info.base.output_semantic_index[attrib] == 0) {
               for (uint32_t rt = 0; rt < key.nr_cbufs; rt++) {
                  STORE(unwrap(out),
                        pPS,
                        {0, SWR_PS_CONTEXT_shaded, rt, channel});
               }
            } else {
               STORE(unwrap(out),
                     pPS,
                     {0,
                           SWR_PS_CONTEXT_shaded,
                           swr_fs->info.base.output_semantic_index[attrib],
                           channel});
            }
         }
         break;
      }
      default: {
         fprintf(stderr,
                 "unknown output from FS %s[%d]\n",
                 tgsi_semantic_names[swr_fs->info.base
                                        .output_semantic_name[attrib]],
                 swr_fs->info.base.output_semantic_index[attrib]);
         break;
      }
      }
   }

   LLVMValueRef mask_result = 0;
   if (swr_fs->info.base.uses_kill) {
      mask_result = lp_build_mask_end(&mask);
   }

   IRB()->SetInsertPoint(unwrap(LLVMGetInsertBlock(gallivm->builder)));

   if (swr_fs->info.base.uses_kill) {
      STORE(unwrap(mask_result), pPS, {0, SWR_PS_CONTEXT_activeMask});
   }

   RET_VOID();

   gallivm_verify_function(gallivm, wrap(pFunction));

   gallivm_compile_module(gallivm);

   PFN_PIXEL_KERNEL kernel =
      (PFN_PIXEL_KERNEL)gallivm_jit_function(gallivm, wrap(pFunction));
   debug_printf("frag shader  %p\n", kernel);
   assert(kernel && "Error: FragShader = NULL");

   JM()->mIsModuleFinalized = true;

   return kernel;
}

PFN_PIXEL_KERNEL
swr_compile_fs(struct swr_context *ctx, swr_jit_fs_key &key)
{
   if (!ctx->fs->pipe.tokens)
      return NULL;

   BuilderSWR builder(
      reinterpret_cast<JitManager *>(swr_screen(ctx->pipe.screen)->hJitMgr),
      "FS");
   PFN_PIXEL_KERNEL func = builder.CompileFS(ctx, key);

   ctx->fs->map.insert(std::make_pair(key, make_unique<VariantFS>(builder.gallivm, func)));
   return func;
}
