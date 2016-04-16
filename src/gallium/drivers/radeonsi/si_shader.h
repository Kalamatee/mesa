/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Tom Stellard <thomas.stellard@amd.com>
 *	Michel Dänzer <michel.daenzer@amd.com>
 *      Christian König <christian.koenig@amd.com>
 */

/* How linking shader inputs and outputs between vertex, tessellation, and
 * geometry shaders works.
 *
 * Inputs and outputs between shaders are stored in a buffer. This buffer
 * lives in LDS (typical case for tessellation), but it can also live
 * in memory (ESGS). Each input or output has a fixed location within a vertex.
 * The highest used input or output determines the stride between vertices.
 *
 * Since GS and tessellation are only possible in the OpenGL core profile,
 * only these semantics are valid for per-vertex data:
 *
 *   Name             Location
 *
 *   POSITION         0
 *   PSIZE            1
 *   CLIPDIST0..1     2..3
 *   CULLDIST0..1     (not implemented)
 *   GENERIC0..31     4..35
 *
 * For example, a shader only writing GENERIC0 has the output stride of 5.
 *
 * Only these semantics are valid for per-patch data:
 *
 *   Name             Location
 *
 *   TESSOUTER        0
 *   TESSINNER        1
 *   PATCH0..29       2..31
 *
 * That's how independent shaders agree on input and output locations.
 * The si_shader_io_get_unique_index function assigns the locations.
 *
 * For tessellation, other required information for calculating the input and
 * output addresses like the vertex stride, the patch stride, and the offsets
 * where per-vertex and per-patch data start, is passed to the shader via
 * user data SGPRs. The offsets and strides are calculated at draw time and
 * aren't available at compile time.
 */

#ifndef SI_SHADER_H
#define SI_SHADER_H

#include <llvm-c/Core.h> /* LLVMModuleRef */
#include "tgsi/tgsi_scan.h"
#include "si_state.h"

struct radeon_shader_binary;
struct radeon_shader_reloc;

#define SI_MAX_VS_OUTPUTS	40

#define SI_SGPR_RW_BUFFERS	0  /* rings (& stream-out, VS only) */
#define SI_SGPR_CONST_BUFFERS	2
#define SI_SGPR_SAMPLERS	4  /* images & sampler states interleaved */
#define SI_SGPR_IMAGES		6
#define SI_SGPR_SHADER_BUFFERS	8
#define SI_SGPR_VERTEX_BUFFERS	10  /* VS only */
#define SI_SGPR_BASE_VERTEX	12 /* VS only */
#define SI_SGPR_START_INSTANCE	13 /* VS only */
#define SI_SGPR_VS_STATE_BITS	14 /* VS(VS) only */
#define SI_SGPR_LS_OUT_LAYOUT	14 /* VS(LS) only */
#define SI_SGPR_TCS_OUT_OFFSETS	10 /* TCS & TES only */
#define SI_SGPR_TCS_OUT_LAYOUT	11 /* TCS & TES only */
#define SI_SGPR_TCS_IN_LAYOUT	12 /* TCS only */
#define SI_SGPR_ALPHA_REF	10 /* PS only */

#define SI_VS_NUM_USER_SGPR	15 /* API VS */
#define SI_ES_NUM_USER_SGPR	14 /* API VS */
#define SI_LS_NUM_USER_SGPR	15 /* API VS */
#define SI_TCS_NUM_USER_SGPR	13
#define SI_TES_NUM_USER_SGPR	12
#define SI_GS_NUM_USER_SGPR	10
#define SI_GSCOPY_NUM_USER_SGPR	4
#define SI_PS_NUM_USER_SGPR	11

/* LLVM function parameter indices */
#define SI_PARAM_RW_BUFFERS	0
#define SI_PARAM_CONST_BUFFERS	1
#define SI_PARAM_SAMPLERS	2
#define SI_PARAM_IMAGES		3
#define SI_PARAM_SHADER_BUFFERS	4

/* VS only parameters */
#define SI_PARAM_VERTEX_BUFFERS	5
#define SI_PARAM_BASE_VERTEX	6
#define SI_PARAM_START_INSTANCE	7
/* [0] = clamp vertex color */
#define SI_PARAM_VS_STATE_BITS	8
/* the other VS parameters are assigned dynamically */

/* Offsets where TCS outputs and TCS patch outputs live in LDS:
 *   [0:15] = TCS output patch0 offset / 16, max = NUM_PATCHES * 32 * 32
 *   [16:31] = TCS output patch0 offset for per-patch / 16, max = NUM_PATCHES*32*32* + 32*32
 */
#define SI_PARAM_TCS_OUT_OFFSETS 5 /* for TCS & TES */

/* Layout of TCS outputs / TES inputs:
 *   [0:12] = stride between output patches in dwords, num_outputs * num_vertices * 4, max = 32*32*4
 *   [13:20] = stride between output vertices in dwords = num_inputs * 4, max = 32*4
 *   [26:31] = gl_PatchVerticesIn, max = 32
 */
#define SI_PARAM_TCS_OUT_LAYOUT	6 /* for TCS & TES */

/* Layout of LS outputs / TCS inputs
 *   [0:12] = stride between patches in dwords = num_inputs * num_vertices * 4, max = 32*32*4
 *   [13:20] = stride between vertices in dwords = num_inputs * 4, max = 32*4
 */
#define SI_PARAM_TCS_IN_LAYOUT	7 /* TCS only */
#define SI_PARAM_LS_OUT_LAYOUT	8 /* same value as TCS_IN_LAYOUT, LS only */

/* TCS only parameters. */
#define SI_PARAM_TESS_FACTOR_OFFSET 8
#define SI_PARAM_PATCH_ID	9
#define SI_PARAM_REL_IDS	10

/* GS only parameters */
#define SI_PARAM_GS2VS_OFFSET	5
#define SI_PARAM_GS_WAVE_ID	6
#define SI_PARAM_VTX0_OFFSET	7
#define SI_PARAM_VTX1_OFFSET	8
#define SI_PARAM_PRIMITIVE_ID	9
#define SI_PARAM_VTX2_OFFSET	10
#define SI_PARAM_VTX3_OFFSET	11
#define SI_PARAM_VTX4_OFFSET	12
#define SI_PARAM_VTX5_OFFSET	13
#define SI_PARAM_GS_INSTANCE_ID	14

/* PS only parameters */
#define SI_PARAM_ALPHA_REF		5
#define SI_PARAM_PRIM_MASK		6
#define SI_PARAM_PERSP_SAMPLE		7
#define SI_PARAM_PERSP_CENTER		8
#define SI_PARAM_PERSP_CENTROID		9
#define SI_PARAM_PERSP_PULL_MODEL	10
#define SI_PARAM_LINEAR_SAMPLE		11
#define SI_PARAM_LINEAR_CENTER		12
#define SI_PARAM_LINEAR_CENTROID	13
#define SI_PARAM_LINE_STIPPLE_TEX	14
#define SI_PARAM_POS_X_FLOAT		15
#define SI_PARAM_POS_Y_FLOAT		16
#define SI_PARAM_POS_Z_FLOAT		17
#define SI_PARAM_POS_W_FLOAT		18
#define SI_PARAM_FRONT_FACE		19
#define SI_PARAM_ANCILLARY		20
#define SI_PARAM_SAMPLE_COVERAGE	21
#define SI_PARAM_POS_FIXED_PT		22

#define SI_NUM_PARAMS (SI_PARAM_POS_FIXED_PT + 9) /* +8 for COLOR[0..1] */

struct si_shader;

/* A shader selector is a gallium CSO and contains shader variants and
 * binaries for one TGSI program. This can be shared by multiple contexts.
 */
struct si_shader_selector {
	pipe_mutex		mutex;
	struct si_shader	*first_variant; /* immutable after the first variant */
	struct si_shader	*last_variant; /* mutable */

	/* The compiled TGSI shader expecting a prolog and/or epilog (not
	 * uploaded to a buffer).
	 */
	struct si_shader	*main_shader_part;

	struct tgsi_token       *tokens;
	struct pipe_stream_output_info  so;
	struct tgsi_shader_info		info;

	/* PIPE_SHADER_[VERTEX|FRAGMENT|...] */
	unsigned	type;

	/* GS parameters. */
	unsigned	esgs_itemsize;
	unsigned	gs_input_verts_per_prim;
	unsigned	gs_output_prim;
	unsigned	gs_max_out_vertices;
	unsigned	gs_num_invocations;
	unsigned	max_gs_stream; /* count - 1 */
	unsigned	gsvs_vertex_size;
	unsigned	max_gsvs_emit_size;

	/* PS parameters. */
	unsigned	color_attr_index[2];
	unsigned	db_shader_control;
	/* Set 0xf or 0x0 (4 bits) per each written output.
	 * ANDed with spi_shader_col_format.
	 */
	unsigned	colors_written_4bit;

	/* masks of "get_unique_index" bits */
	uint64_t	outputs_written;
	uint32_t	patch_outputs_written;
};

/* Valid shader configurations:
 *
 * API shaders       VS | TCS | TES | GS |pass| PS
 * are compiled as:     |     |     |    |thru|
 *                      |     |     |    |    |
 * Only VS & PS:     VS | --  | --  | -- | -- | PS
 * With GS:          ES | --  | --  | GS | VS | PS
 * With Tessel.:     LS | HS  | VS  | -- | -- | PS
 * With both:        LS | HS  | ES  | GS | VS | PS
 */

/* Common VS bits between the shader key and the prolog key. */
struct si_vs_prolog_bits {
	unsigned	instance_divisors[SI_NUM_VERTEX_BUFFERS];
};

/* Common VS bits between the shader key and the epilog key. */
struct si_vs_epilog_bits {
	unsigned	export_prim_id:1; /* when PS needs it and GS is disabled */
	/* TODO:
	 * - skip clipdist, culldist (including clipvertex code) exports based
	 *   on which clip_plane_enable bits are set
	 * - skip layer, viewport, clipdist, and culldist parameter exports
	 *   if PS doesn't read them
	 */
};

/* Common TCS bits between the shader key and the epilog key. */
struct si_tcs_epilog_bits {
	unsigned	prim_mode:3;
};

/* Common PS bits between the shader key and the prolog key. */
struct si_ps_prolog_bits {
	unsigned	color_two_side:1;
	/* TODO: add a flatshade bit that skips interpolation for colors */
	unsigned	poly_stipple:1;
	unsigned	force_persample_interp:1;
	/* TODO:
	 * - add force_center_interp if MSAA is disabled and centroid or
	 *   sample are present
	 * - add force_center_interp_bc_optimize to force center interpolation
	 *   based on the bc_optimize SGPR bit if MSAA is enabled, centroid is
	 *   present and sample isn't present.
	 */
};

/* Common PS bits between the shader key and the epilog key. */
struct si_ps_epilog_bits {
	unsigned	spi_shader_col_format;
	unsigned	color_is_int8:8;
	unsigned	last_cbuf:3;
	unsigned	alpha_func:3;
	unsigned	alpha_to_one:1;
	unsigned	poly_line_smoothing:1;
	unsigned	clamp_color:1;
};

union si_shader_part_key {
	struct {
		struct si_vs_prolog_bits states;
		unsigned	num_input_sgprs:5;
		unsigned	last_input:4;
	} vs_prolog;
	struct {
		struct si_vs_epilog_bits states;
		unsigned	prim_id_param_offset:5;
	} vs_epilog;
	struct {
		struct si_tcs_epilog_bits states;
	} tcs_epilog;
	struct {
		struct si_ps_prolog_bits states;
		unsigned	num_input_sgprs:5;
		unsigned	num_input_vgprs:5;
		/* Color interpolation and two-side color selection. */
		unsigned	colors_read:8; /* color input components read */
		unsigned	num_interp_inputs:5; /* BCOLOR is at this location */
		unsigned	face_vgpr_index:5;
		char		color_attr_index[2];
		char		color_interp_vgpr_index[2]; /* -1 == constant */
	} ps_prolog;
	struct {
		struct si_ps_epilog_bits states;
		unsigned	colors_written:8;
		unsigned	writes_z:1;
		unsigned	writes_stencil:1;
		unsigned	writes_samplemask:1;
	} ps_epilog;
};

union si_shader_key {
	struct {
		struct si_ps_prolog_bits prolog;
		struct si_ps_epilog_bits epilog;
	} ps;
	struct {
		struct si_vs_prolog_bits prolog;
		struct si_vs_epilog_bits epilog;
		unsigned	as_es:1; /* export shader */
		unsigned	as_ls:1; /* local shader */
	} vs;
	struct {
		struct si_tcs_epilog_bits epilog;
	} tcs; /* tessellation control shader */
	struct {
		struct si_vs_epilog_bits epilog; /* same as VS */
		unsigned	as_es:1; /* export shader */
	} tes; /* tessellation evaluation shader */
};

struct si_shader_config {
	unsigned			num_sgprs;
	unsigned			num_vgprs;
	unsigned			lds_size;
	unsigned			spi_ps_input_ena;
	unsigned			spi_ps_input_addr;
	unsigned			float_mode;
	unsigned			scratch_bytes_per_wave;
	unsigned			rsrc1;
	unsigned			rsrc2;
};

/* GCN-specific shader info. */
struct si_shader_info {
	ubyte			vs_output_param_offset[SI_MAX_VS_OUTPUTS];
	ubyte			num_input_sgprs;
	ubyte			num_input_vgprs;
	char			face_vgpr_index;
	bool			uses_instanceid;
	ubyte			nr_pos_exports;
	ubyte			nr_param_exports;
};

struct si_shader {
	struct si_shader_selector	*selector;
	struct si_shader		*next_variant;

	struct si_shader_part		*prolog;
	struct si_shader_part		*epilog;

	struct si_shader		*gs_copy_shader;
	struct si_pm4_state		*pm4;
	struct r600_resource		*bo;
	struct r600_resource		*scratch_bo;
	union si_shader_key		key;
	bool				is_binary_shared;
	unsigned			z_order;

	/* The following data is all that's needed for binary shaders. */
	struct radeon_shader_binary	binary;
	struct si_shader_config		config;
	struct si_shader_info		info;
};

struct si_shader_part {
	struct si_shader_part *next;
	union si_shader_part_key key;
	struct radeon_shader_binary binary;
	struct si_shader_config config;
};

static inline struct tgsi_shader_info *si_get_vs_info(struct si_context *sctx)
{
	if (sctx->gs_shader.cso)
		return &sctx->gs_shader.cso->info;
	else if (sctx->tes_shader.cso)
		return &sctx->tes_shader.cso->info;
	else if (sctx->vs_shader.cso)
		return &sctx->vs_shader.cso->info;
	else
		return NULL;
}

static inline struct si_shader* si_get_vs_state(struct si_context *sctx)
{
	if (sctx->gs_shader.current)
		return sctx->gs_shader.current->gs_copy_shader;
	else if (sctx->tes_shader.current)
		return sctx->tes_shader.current;
	else
		return sctx->vs_shader.current;
}

static inline bool si_vs_exports_prim_id(struct si_shader *shader)
{
	if (shader->selector->type == PIPE_SHADER_VERTEX)
		return shader->key.vs.epilog.export_prim_id;
	else if (shader->selector->type == PIPE_SHADER_TESS_EVAL)
		return shader->key.tes.epilog.export_prim_id;
	else
		return false;
}

/* si_shader.c */
int si_compile_tgsi_shader(struct si_screen *sscreen,
			   LLVMTargetMachineRef tm,
			   struct si_shader *shader,
			   bool is_monolithic,
			   struct pipe_debug_callback *debug);
int si_shader_create(struct si_screen *sscreen, LLVMTargetMachineRef tm,
		     struct si_shader *shader,
		     struct pipe_debug_callback *debug);
void si_dump_shader_key(unsigned shader, union si_shader_key *key, FILE *f);
int si_compile_llvm(struct si_screen *sscreen,
		    struct radeon_shader_binary *binary,
		    struct si_shader_config *conf,
		    LLVMTargetMachineRef tm,
		    LLVMModuleRef mod,
		    struct pipe_debug_callback *debug,
		    unsigned processor,
		    const char *name);
void si_shader_destroy(struct si_shader *shader);
unsigned si_shader_io_get_unique_index(unsigned semantic_name, unsigned index);
int si_shader_binary_upload(struct si_screen *sscreen, struct si_shader *shader);
void si_shader_dump(struct si_screen *sscreen, struct si_shader *shader,
		    struct pipe_debug_callback *debug, unsigned processor,
		    FILE *f);
void si_shader_apply_scratch_relocs(struct si_context *sctx,
			struct si_shader *shader,
			uint64_t scratch_va);
void si_shader_binary_read_config(struct radeon_shader_binary *binary,
				  struct si_shader_config *conf,
				  unsigned symbol_offset);

#endif
