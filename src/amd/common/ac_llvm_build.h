/*
 * Copyright 2016 Bas Nieuwenhuizen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
#ifndef AC_LLVM_BUILD_H
#define AC_LLVM_BUILD_H

#include <stdbool.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ac_llvm_context {
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;

	LLVMTypeRef voidt;
	LLVMTypeRef i1;
	LLVMTypeRef i8;
	LLVMTypeRef i32;
	LLVMTypeRef f32;
	LLVMTypeRef v4i32;
	LLVMTypeRef v4f32;
	LLVMTypeRef v16i8;

	unsigned range_md_kind;
	unsigned invariant_load_md_kind;
	unsigned uniform_md_kind;
	unsigned fpmath_md_kind;
	LLVMValueRef fpmath_md_2p5_ulp;
	LLVMValueRef empty_md;
};

void
ac_llvm_context_init(struct ac_llvm_context *ctx, LLVMContextRef context);

LLVMValueRef
ac_build_intrinsic(struct ac_llvm_context *ctx, const char *name,
		   LLVMTypeRef return_type, LLVMValueRef *params,
		   unsigned param_count, unsigned attrib_mask);

void ac_build_type_name_for_intr(LLVMTypeRef type, char *buf, unsigned bufsize);

LLVMValueRef
ac_build_gather_values_extended(struct ac_llvm_context *ctx,
				LLVMValueRef *values,
				unsigned value_count,
				unsigned value_stride,
				bool load);
LLVMValueRef
ac_build_gather_values(struct ac_llvm_context *ctx,
		       LLVMValueRef *values,
		       unsigned value_count);

LLVMValueRef
ac_build_fdiv(struct ac_llvm_context *ctx,
	      LLVMValueRef num,
	      LLVMValueRef den);

void
ac_prepare_cube_coords(struct ac_llvm_context *ctx,
		       bool is_deriv, bool is_array,
		       LLVMValueRef *coords_arg,
		       LLVMValueRef *derivs_arg);


LLVMValueRef
ac_build_fs_interp(struct ac_llvm_context *ctx,
		   LLVMValueRef llvm_chan,
		   LLVMValueRef attr_number,
		   LLVMValueRef params,
		   LLVMValueRef i,
		   LLVMValueRef j);

LLVMValueRef
ac_build_fs_interp_mov(struct ac_llvm_context *ctx,
		       LLVMValueRef parameter,
		       LLVMValueRef llvm_chan,
		       LLVMValueRef attr_number,
		       LLVMValueRef params);

LLVMValueRef
ac_build_gep0(struct ac_llvm_context *ctx,
	      LLVMValueRef base_ptr,
	      LLVMValueRef index);

void
ac_build_indexed_store(struct ac_llvm_context *ctx,
		       LLVMValueRef base_ptr, LLVMValueRef index,
		       LLVMValueRef value);

LLVMValueRef
ac_build_indexed_load(struct ac_llvm_context *ctx,
		      LLVMValueRef base_ptr, LLVMValueRef index,
		      bool uniform);

LLVMValueRef
ac_build_indexed_load_const(struct ac_llvm_context *ctx,
			    LLVMValueRef base_ptr, LLVMValueRef index);

void
ac_build_buffer_store_dword(struct ac_llvm_context *ctx,
			    LLVMValueRef rsrc,
			    LLVMValueRef vdata,
			    unsigned num_channels,
			    LLVMValueRef voffset,
			    LLVMValueRef soffset,
			    unsigned inst_offset,
		            bool glc,
		            bool slc,
			    bool writeonly_memory,
			    bool has_add_tid);
LLVMValueRef
ac_build_buffer_load(struct ac_llvm_context *ctx,
		     LLVMValueRef rsrc,
		     int num_channels,
		     LLVMValueRef vindex,
		     LLVMValueRef voffset,
		     LLVMValueRef soffset,
		     unsigned inst_offset,
		     unsigned glc,
		     unsigned slc,
		     bool readonly_memory);

LLVMValueRef ac_build_buffer_load_format(struct ac_llvm_context *ctx,
					 LLVMValueRef rsrc,
					 LLVMValueRef vindex,
					 LLVMValueRef voffset,
					 bool readonly_memory);

LLVMValueRef
ac_get_thread_id(struct ac_llvm_context *ctx);

#define AC_TID_MASK_TOP_LEFT 0xfffffffc
#define AC_TID_MASK_TOP      0xfffffffd
#define AC_TID_MASK_LEFT     0xfffffffe

LLVMValueRef
ac_build_ddxy(struct ac_llvm_context *ctx,
	      bool has_ds_bpermute,
	      uint32_t mask,
	      int idx,
	      LLVMValueRef lds,
	      LLVMValueRef val);

#define AC_SENDMSG_GS 2
#define AC_SENDMSG_GS_DONE 3

#define AC_SENDMSG_GS_OP_NOP      (0 << 4)
#define AC_SENDMSG_GS_OP_CUT      (1 << 4)
#define AC_SENDMSG_GS_OP_EMIT     (2 << 4)
#define AC_SENDMSG_GS_OP_EMIT_CUT (3 << 4)

void ac_build_sendmsg(struct ac_llvm_context *ctx,
		      uint32_t msg,
		      LLVMValueRef wave_id);

LLVMValueRef ac_build_imsb(struct ac_llvm_context *ctx,
			   LLVMValueRef arg,
			   LLVMTypeRef dst_type);

LLVMValueRef ac_build_umsb(struct ac_llvm_context *ctx,
			  LLVMValueRef arg,
			  LLVMTypeRef dst_type);

LLVMValueRef ac_build_clamp(struct ac_llvm_context *ctx, LLVMValueRef value);

struct ac_export_args {
	LLVMValueRef out[4];
        unsigned target;
        unsigned enabled_channels;
        bool compr;
        bool done;
        bool valid_mask;
};

void ac_build_export(struct ac_llvm_context *ctx, struct ac_export_args *a);

enum ac_image_opcode {
	ac_image_sample,
	ac_image_gather4,
	ac_image_load,
	ac_image_load_mip,
	ac_image_get_lod,
	ac_image_get_resinfo,
};

struct ac_image_args {
	enum ac_image_opcode opcode;
	bool level_zero;
	bool bias;
	bool lod;
	bool deriv;
	bool compare;
	bool offset;

	LLVMValueRef resource;
	LLVMValueRef sampler;
	LLVMValueRef addr;
	unsigned dmask;
	bool unorm;
	bool da;
};

LLVMValueRef ac_build_image_opcode(struct ac_llvm_context *ctx,
				   struct ac_image_args *a);
LLVMValueRef ac_build_cvt_pkrtz_f16(struct ac_llvm_context *ctx,
				    LLVMValueRef args[2]);
void ac_build_kill(struct ac_llvm_context *ctx, LLVMValueRef value);
LLVMValueRef ac_build_bfe(struct ac_llvm_context *ctx, LLVMValueRef input,
			  LLVMValueRef offset, LLVMValueRef width,
			  bool is_signed);

void ac_get_image_intr_name(const char *base_name,
			    LLVMTypeRef data_type,
			    LLVMTypeRef coords_type,
			    LLVMTypeRef rsrc_type,
			    char *out_name, unsigned out_len);
#ifdef __cplusplus
}
#endif

#endif
