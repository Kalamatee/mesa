/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors: Marek Olšák <maraeo@gmail.com>
 *
 */

/**
 * This file contains common screen and context structures and functions
 * for r600g and radeonsi.
 */

#ifndef R600_PIPE_COMMON_H
#define R600_PIPE_COMMON_H

#include <stdio.h>

#include "radeon/radeon_winsys.h"

#include "util/u_blitter.h"
#include "util/list.h"
#include "util/u_range.h"
#include "util/u_slab.h"
#include "util/u_suballoc.h"
#include "util/u_transfer.h"

#define R600_RESOURCE_FLAG_TRANSFER		(PIPE_RESOURCE_FLAG_DRV_PRIV << 0)
#define R600_RESOURCE_FLAG_FLUSHED_DEPTH	(PIPE_RESOURCE_FLAG_DRV_PRIV << 1)
#define R600_RESOURCE_FLAG_FORCE_TILING		(PIPE_RESOURCE_FLAG_DRV_PRIV << 2)

#define R600_CONTEXT_STREAMOUT_FLUSH		(1u << 0)
#define R600_CONTEXT_PRIVATE_FLAG		(1u << 1)

/* special primitive types */
#define R600_PRIM_RECTANGLE_LIST	PIPE_PRIM_MAX

/* Debug flags. */
/* logging */
#define DBG_TEX			(1 << 0)
/* gap - reuse */
#define DBG_COMPUTE		(1 << 2)
#define DBG_VM			(1 << 3)
#define DBG_TRACE_CS		(1 << 4)
/* shader logging */
#define DBG_FS			(1 << 5)
#define DBG_VS			(1 << 6)
#define DBG_GS			(1 << 7)
#define DBG_PS			(1 << 8)
#define DBG_CS			(1 << 9)
#define DBG_TCS			(1 << 10)
#define DBG_TES			(1 << 11)
#define DBG_NO_IR		(1 << 12)
#define DBG_NO_TGSI		(1 << 13)
#define DBG_NO_ASM		(1 << 14)
/* Bits 21-31 are reserved for the r600g driver. */
/* features */
#define DBG_NO_ASYNC_DMA	(1llu << 32)
#define DBG_NO_HYPERZ		(1llu << 33)
#define DBG_NO_DISCARD_RANGE	(1llu << 34)
#define DBG_NO_2D_TILING	(1llu << 35)
#define DBG_NO_TILING		(1llu << 36)
#define DBG_SWITCH_ON_EOP	(1llu << 37)
#define DBG_FORCE_DMA		(1llu << 38)
#define DBG_PRECOMPILE		(1llu << 39)
#define DBG_INFO		(1llu << 40)
#define DBG_NO_WC		(1llu << 41)
#define DBG_CHECK_VM		(1llu << 42)
#define DBG_NO_DCC		(1llu << 43)
#define DBG_NO_DCC_CLEAR	(1llu << 44)
#define DBG_NO_RB_PLUS		(1llu << 45)

#define R600_MAP_BUFFER_ALIGNMENT 64

struct r600_common_context;
struct r600_perfcounters;

struct radeon_shader_reloc {
	char *name;
	uint64_t offset;
};

struct radeon_shader_binary {
	/** Shader code */
	unsigned char *code;
	unsigned code_size;

	/** Config/Context register state that accompanies this shader.
	 * This is a stream of dword pairs.  First dword contains the
	 * register address, the second dword contains the value.*/
	unsigned char *config;
	unsigned config_size;

	/** The number of bytes of config information for each global symbol.
	 */
	unsigned config_size_per_symbol;

	/** Constant data accessed by the shader.  This will be uploaded
	 * into a constant buffer. */
	unsigned char *rodata;
	unsigned rodata_size;

	/** List of symbol offsets for the shader */
	uint64_t *global_symbol_offsets;
	unsigned global_symbol_count;

	struct radeon_shader_reloc *relocs;
	unsigned reloc_count;

	/** Disassembled shader in a string. */
	char *disasm_string;
};

struct r600_resource {
	struct u_resource		b;

	/* Winsys objects. */
	struct pb_buffer		*buf;
	uint64_t			gpu_address;

	/* Resource state. */
	enum radeon_bo_domain		domains;

	/* The buffer range which is initialized (with a write transfer,
	 * streamout, DMA, or as a random access target). The rest of
	 * the buffer is considered invalid and can be mapped unsynchronized.
	 *
	 * This allows unsychronized mapping of a buffer range which hasn't
	 * been used yet. It's for applications which forget to use
	 * the unsynchronized map flag and expect the driver to figure it out.
         */
	struct util_range		valid_buffer_range;

	/* For buffers only. This indicates that a write operation has been
	 * performed by TC L2, but the cache hasn't been flushed.
	 * Any hw block which doesn't use or bypasses TC L2 should check this
	 * flag and flush the cache before using the buffer.
	 *
	 * For example, TC L2 must be flushed if a buffer which has been
	 * modified by a shader store instruction is about to be used as
	 * an index buffer. The reason is that VGT DMA index fetching doesn't
	 * use TC L2.
	 */
	bool				TC_L2_dirty;
};

struct r600_transfer {
	struct pipe_transfer		transfer;
	struct r600_resource		*staging;
	unsigned			offset;
};

struct r600_fmask_info {
	unsigned offset;
	unsigned size;
	unsigned alignment;
	unsigned pitch_in_pixels;
	unsigned bank_height;
	unsigned slice_tile_max;
	unsigned tile_mode_index;
};

struct r600_cmask_info {
	unsigned offset;
	unsigned size;
	unsigned alignment;
	unsigned pitch;
	unsigned height;
	unsigned xalign;
	unsigned yalign;
	unsigned slice_tile_max;
	unsigned base_address_reg;
};

struct r600_htile_info {
	unsigned pitch;
	unsigned height;
	unsigned xalign;
	unsigned yalign;
};

struct r600_texture {
	struct r600_resource		resource;

	unsigned			size;
	bool				is_depth;
	unsigned			dirty_level_mask; /* each bit says if that mipmap is compressed */
	unsigned			stencil_dirty_level_mask; /* each bit says if that mipmap is compressed */
	struct r600_texture		*flushed_depth_texture;
	boolean				is_flushing_texture;
	struct radeon_surf		surface;

	/* Colorbuffer compression and fast clear. */
	struct r600_fmask_info		fmask;
	struct r600_cmask_info		cmask;
	struct r600_resource		*cmask_buffer;
	struct r600_resource		*dcc_buffer;
	unsigned			cb_color_info; /* fast clear enable bit */
	unsigned			color_clear_value[2];

	/* Depth buffer compression and fast clear. */
	struct r600_htile_info		htile;
	struct r600_resource		*htile_buffer;
	bool				depth_cleared; /* if it was cleared at least once */
	float				depth_clear_value;
	bool				stencil_cleared; /* if it was cleared at least once */
	uint8_t				stencil_clear_value;

	bool				non_disp_tiling; /* R600-Cayman only */
};

struct r600_surface {
	struct pipe_surface		base;

	bool color_initialized;
	bool depth_initialized;

	/* Misc. color flags. */
	bool alphatest_bypass;
	bool export_16bpc;

	/* Color registers. */
	unsigned cb_color_info;
	unsigned cb_color_base;
	unsigned cb_color_view;
	unsigned cb_color_size;		/* R600 only */
	unsigned cb_color_dim;		/* EG only */
	unsigned cb_color_pitch;	/* EG and later */
	unsigned cb_color_slice;	/* EG and later */
	unsigned cb_dcc_base;		/* VI and later */
	unsigned cb_color_attrib;	/* EG and later */
	unsigned cb_dcc_control;	/* VI and later */
	unsigned cb_color_fmask;	/* CB_COLORn_FMASK (EG and later) or CB_COLORn_FRAG (r600) */
	unsigned cb_color_fmask_slice;	/* EG and later */
	unsigned cb_color_cmask;	/* CB_COLORn_TILE (r600 only) */
	unsigned cb_color_mask;		/* R600 only */
	unsigned sx_ps_downconvert;	/* Stoney only */
	unsigned sx_blend_opt_epsilon;	/* Stoney only */
	struct r600_resource *cb_buffer_fmask; /* Used for FMASK relocations. R600 only */
	struct r600_resource *cb_buffer_cmask; /* Used for CMASK relocations. R600 only */

	/* DB registers. */
	unsigned db_depth_info;		/* R600 only, then SI and later */
	unsigned db_z_info;		/* EG and later */
	unsigned db_depth_base;		/* DB_Z_READ/WRITE_BASE (EG and later) or DB_DEPTH_BASE (r600) */
	unsigned db_depth_view;
	unsigned db_depth_size;
	unsigned db_depth_slice;	/* EG and later */
	unsigned db_stencil_base;	/* EG and later */
	unsigned db_stencil_info;	/* EG and later */
	unsigned db_prefetch_limit;	/* R600 only */
	unsigned db_htile_surface;
	unsigned db_htile_data_base;
	unsigned db_preload_control;	/* EG and later */
	unsigned pa_su_poly_offset_db_fmt_cntl;
};

struct r600_tiling_info {
	unsigned num_channels;
	unsigned num_banks;
	unsigned group_bytes;
};

struct r600_common_screen {
	struct pipe_screen		b;
	struct radeon_winsys		*ws;
	enum radeon_family		family;
	enum chip_class			chip_class;
	struct radeon_info		info;
	struct r600_tiling_info		tiling_info;
	uint64_t			debug_flags;
	bool				has_cp_dma;
	bool				has_streamout;

	/* Auxiliary context. Mainly used to initialize resources.
	 * It must be locked prior to using and flushed before unlocking. */
	struct pipe_context		*aux_context;
	pipe_mutex			aux_context_lock;

	struct r600_resource		*trace_bo;
	uint32_t			*trace_ptr;
	unsigned			cs_count;

	/* This must be in the screen, because UE4 uses one context for
	 * compilation and another one for rendering.
	 */
	unsigned			num_compilations;
	/* Along with ST_DEBUG=precompile, this should show if applications
	 * are loading shaders on demand. This is a monotonic counter.
	 */
	unsigned			num_shaders_created;

	/* GPU load thread. */
	pipe_mutex			gpu_load_mutex;
	pipe_thread			gpu_load_thread;
	unsigned			gpu_load_counter_busy;
	unsigned			gpu_load_counter_idle;
	volatile unsigned		gpu_load_stop_thread; /* bool */

	char				renderer_string[64];

	/* Performance counters. */
	struct r600_perfcounters	*perfcounters;
};

/* This encapsulates a state or an operation which can emitted into the GPU
 * command stream. */
struct r600_atom {
	void (*emit)(struct r600_common_context *ctx, struct r600_atom *state);
	unsigned		num_dw;
	unsigned short		id;
};

struct r600_so_target {
	struct pipe_stream_output_target b;

	/* The buffer where BUFFER_FILLED_SIZE is stored. */
	struct r600_resource	*buf_filled_size;
	unsigned		buf_filled_size_offset;
	bool			buf_filled_size_valid;

	unsigned		stride_in_dw;
};

struct r600_streamout {
	struct r600_atom		begin_atom;
	bool				begin_emitted;
	unsigned			num_dw_for_end;

	unsigned			enabled_mask;
	unsigned			num_targets;
	struct r600_so_target		*targets[PIPE_MAX_SO_BUFFERS];

	unsigned			append_bitmask;
	bool				suspended;

	/* External state which comes from the vertex shader,
	 * it must be set explicitly when binding a shader. */
	unsigned			*stride_in_dw;
	unsigned			enabled_stream_buffers_mask; /* stream0 buffers0-3 in 4 LSB */

	/* The state of VGT_STRMOUT_BUFFER_(CONFIG|EN). */
	unsigned			hw_enabled_mask;

	/* The state of VGT_STRMOUT_(CONFIG|EN). */
	struct r600_atom		enable_atom;
	bool				streamout_enabled;
	bool				prims_gen_query_enabled;
	int				num_prims_gen_queries;
};

struct r600_ring {
	struct radeon_winsys_cs		*cs;
	void (*flush)(void *ctx, unsigned flags,
		      struct pipe_fence_handle **fence);
};

struct r600_common_context {
	struct pipe_context b; /* base class */

	struct r600_common_screen	*screen;
	struct radeon_winsys		*ws;
	struct radeon_winsys_ctx	*ctx;
	enum radeon_family		family;
	enum chip_class			chip_class;
	struct r600_ring		gfx;
	struct r600_ring		dma;
	struct pipe_fence_handle	*last_sdma_fence;
	unsigned			initial_gfx_cs_size;
	unsigned			gpu_reset_counter;

	struct u_upload_mgr		*uploader;
	struct u_suballocator		*allocator_so_filled_size;
	struct util_slab_mempool	pool_transfers;

	/* Current unaccounted memory usage. */
	uint64_t			vram;
	uint64_t			gtt;

	/* States. */
	struct r600_streamout		streamout;

	/* Additional context states. */
	unsigned flags; /* flush flags */

	/* Queries. */
	/* The list of active queries. Only one query of each type can be active. */
	int				num_occlusion_queries;
	/* Keep track of non-timer queries, because they should be suspended
	 * during context flushing.
	 * The timer queries (TIME_ELAPSED) shouldn't be suspended for blits,
	 * but they should be suspended between IBs. */
	struct list_head		active_nontimer_queries;
	struct list_head		active_timer_queries;
	unsigned			num_cs_dw_nontimer_queries_suspend;
	bool				nontimer_queries_suspended_by_flush;
	unsigned			num_cs_dw_timer_queries_suspend;
	/* Additional hardware info. */
	unsigned			backend_mask;
	unsigned			max_db; /* for OQ */
	/* Misc stats. */
	unsigned			num_draw_calls;

	/* Render condition. */
	struct r600_atom		render_cond_atom;
	struct pipe_query		*render_cond;
	unsigned			render_cond_mode;
	boolean				render_cond_invert;
	bool				render_cond_force_off; /* for u_blitter */

	/* MSAA sample locations.
	 * The first index is the sample index.
	 * The second index is the coordinate: X, Y. */
	float				sample_locations_1x[1][2];
	float				sample_locations_2x[2][2];
	float				sample_locations_4x[4][2];
	float				sample_locations_8x[8][2];
	float				sample_locations_16x[16][2];

	/* The list of all texture buffer objects in this context.
	 * This list is walked when a buffer is invalidated/reallocated and
	 * the GPU addresses are updated. */
	struct list_head		texture_buffers;

	/* Copy one resource to another using async DMA. */
	void (*dma_copy)(struct pipe_context *ctx,
			 struct pipe_resource *dst,
			 unsigned dst_level,
			 unsigned dst_x, unsigned dst_y, unsigned dst_z,
			 struct pipe_resource *src,
			 unsigned src_level,
			 const struct pipe_box *src_box);

	void (*clear_buffer)(struct pipe_context *ctx, struct pipe_resource *dst,
			     unsigned offset, unsigned size, unsigned value,
			     bool is_framebuffer);

	void (*blit_decompress_depth)(struct pipe_context *ctx,
				      struct r600_texture *texture,
				      struct r600_texture *staging,
				      unsigned first_level, unsigned last_level,
				      unsigned first_layer, unsigned last_layer,
				      unsigned first_sample, unsigned last_sample);

	/* Reallocate the buffer and update all resource bindings where
	 * the buffer is bound, including all resource descriptors. */
	void (*invalidate_buffer)(struct pipe_context *ctx, struct pipe_resource *buf);

	/* Enable or disable occlusion queries. */
	void (*set_occlusion_query_state)(struct pipe_context *ctx, bool enable);

	/* This ensures there is enough space in the command stream. */
	void (*need_gfx_cs_space)(struct pipe_context *ctx, unsigned num_dw,
				  bool include_draw_vbo);

	void (*set_atom_dirty)(struct r600_common_context *ctx,
			       struct r600_atom *atom, bool dirty);
};

/* r600_buffer.c */
boolean r600_rings_is_buffer_referenced(struct r600_common_context *ctx,
					struct pb_buffer *buf,
					enum radeon_bo_usage usage);
void *r600_buffer_map_sync_with_rings(struct r600_common_context *ctx,
                                      struct r600_resource *resource,
                                      unsigned usage);
bool r600_init_resource(struct r600_common_screen *rscreen,
			struct r600_resource *res,
			unsigned size, unsigned alignment,
			bool use_reusable_pool);
struct pipe_resource *r600_buffer_create(struct pipe_screen *screen,
					 const struct pipe_resource *templ,
					 unsigned alignment);
struct pipe_resource * r600_aligned_buffer_create(struct pipe_screen *screen,
						  unsigned bind,
						  unsigned usage,
						  unsigned size,
						  unsigned alignment);
struct pipe_resource *
r600_buffer_from_user_memory(struct pipe_screen *screen,
			     const struct pipe_resource *templ,
			     void *user_memory);

/* r600_common_pipe.c */
void r600_draw_rectangle(struct blitter_context *blitter,
			 int x1, int y1, int x2, int y2, float depth,
			 enum blitter_attrib_type type,
			 const union pipe_color_union *attrib);
bool r600_common_screen_init(struct r600_common_screen *rscreen,
			     struct radeon_winsys *ws);
void r600_destroy_common_screen(struct r600_common_screen *rscreen);
void r600_preflush_suspend_features(struct r600_common_context *ctx);
void r600_postflush_resume_features(struct r600_common_context *ctx);
bool r600_common_context_init(struct r600_common_context *rctx,
			      struct r600_common_screen *rscreen);
void r600_common_context_cleanup(struct r600_common_context *rctx);
void r600_context_add_resource_size(struct pipe_context *ctx, struct pipe_resource *r);
bool r600_can_dump_shader(struct r600_common_screen *rscreen,
			  const struct tgsi_token *tokens);
void r600_screen_clear_buffer(struct r600_common_screen *rscreen, struct pipe_resource *dst,
			      unsigned offset, unsigned size, unsigned value,
			      bool is_framebuffer);
struct pipe_resource *r600_resource_create_common(struct pipe_screen *screen,
						  const struct pipe_resource *templ);
const char *r600_get_llvm_processor_name(enum radeon_family family);
void r600_need_dma_space(struct r600_common_context *ctx, unsigned num_dw);

/* r600_gpu_load.c */
void r600_gpu_load_kill_thread(struct r600_common_screen *rscreen);
uint64_t r600_gpu_load_begin(struct r600_common_screen *rscreen);
unsigned r600_gpu_load_end(struct r600_common_screen *rscreen, uint64_t begin);

/* r600_perfcounters.c */
void r600_perfcounters_destroy(struct r600_common_screen *rscreen);

/* r600_query.c */
void r600_init_screen_query_functions(struct r600_common_screen *rscreen);
void r600_query_init(struct r600_common_context *rctx);
void r600_suspend_nontimer_queries(struct r600_common_context *ctx);
void r600_resume_nontimer_queries(struct r600_common_context *ctx);
void r600_suspend_timer_queries(struct r600_common_context *ctx);
void r600_resume_timer_queries(struct r600_common_context *ctx);
void r600_query_init_backend_mask(struct r600_common_context *ctx);

/* r600_streamout.c */
void r600_streamout_buffers_dirty(struct r600_common_context *rctx);
void r600_set_streamout_targets(struct pipe_context *ctx,
				unsigned num_targets,
				struct pipe_stream_output_target **targets,
				const unsigned *offset);
void r600_emit_streamout_end(struct r600_common_context *rctx);
void r600_update_prims_generated_query_state(struct r600_common_context *rctx,
					     unsigned type, int diff);
void r600_streamout_init(struct r600_common_context *rctx);

/* r600_texture.c */
void r600_texture_get_fmask_info(struct r600_common_screen *rscreen,
				 struct r600_texture *rtex,
				 unsigned nr_samples,
				 struct r600_fmask_info *out);
void r600_texture_get_cmask_info(struct r600_common_screen *rscreen,
				 struct r600_texture *rtex,
				 struct r600_cmask_info *out);
bool r600_init_flushed_depth_texture(struct pipe_context *ctx,
				     struct pipe_resource *texture,
				     struct r600_texture **staging);
void r600_print_texture_info(struct r600_texture *rtex, FILE *f);
struct pipe_resource *r600_texture_create(struct pipe_screen *screen,
					const struct pipe_resource *templ);
struct pipe_surface *r600_create_surface_custom(struct pipe_context *pipe,
						struct pipe_resource *texture,
						const struct pipe_surface *templ,
						unsigned width, unsigned height);
unsigned r600_translate_colorswap(enum pipe_format format);
void evergreen_do_fast_color_clear(struct r600_common_context *rctx,
				   struct pipe_framebuffer_state *fb,
				   struct r600_atom *fb_state,
				   unsigned *buffers, unsigned *dirty_cbufs,
				   const union pipe_color_union *color);
void r600_init_screen_texture_functions(struct r600_common_screen *rscreen);
void r600_init_context_texture_functions(struct r600_common_context *rctx);

/* cayman_msaa.c */
extern const uint32_t eg_sample_locs_2x[4];
extern const unsigned eg_max_dist_2x;
extern const uint32_t eg_sample_locs_4x[4];
extern const unsigned eg_max_dist_4x;
void cayman_get_sample_position(struct pipe_context *ctx, unsigned sample_count,
				unsigned sample_index, float *out_value);
void cayman_init_msaa(struct pipe_context *ctx);
void cayman_emit_msaa_sample_locs(struct radeon_winsys_cs *cs, int nr_samples);
void cayman_emit_msaa_config(struct radeon_winsys_cs *cs, int nr_samples,
			     int ps_iter_samples, int overrast_samples);


/* Inline helpers. */

static inline struct r600_resource *r600_resource(struct pipe_resource *r)
{
	return (struct r600_resource*)r;
}

static inline void
r600_resource_reference(struct r600_resource **ptr, struct r600_resource *res)
{
	pipe_resource_reference((struct pipe_resource **)ptr,
				(struct pipe_resource *)res);
}

static inline unsigned r600_tex_aniso_filter(unsigned filter)
{
	if (filter <= 1)   return 0;
	if (filter <= 2)   return 1;
	if (filter <= 4)   return 2;
	if (filter <= 8)   return 3;
	 /* else */        return 4;
}

static inline unsigned r600_wavefront_size(enum radeon_family family)
{
	switch (family) {
	case CHIP_RV610:
	case CHIP_RS780:
	case CHIP_RV620:
	case CHIP_RS880:
		return 16;
	case CHIP_RV630:
	case CHIP_RV635:
	case CHIP_RV730:
	case CHIP_RV710:
	case CHIP_PALM:
	case CHIP_CEDAR:
		return 32;
	default:
		return 64;
	}
}

static inline enum radeon_bo_priority
r600_get_sampler_view_priority(struct r600_resource *res)
{
	if (res->b.b.target == PIPE_BUFFER)
		return RADEON_PRIO_SAMPLER_BUFFER;

	if (res->b.b.nr_samples > 1)
		return RADEON_PRIO_SAMPLER_TEXTURE_MSAA;

	return RADEON_PRIO_SAMPLER_TEXTURE;
}

#define COMPUTE_DBG(rscreen, fmt, args...) \
	do { \
		if ((rscreen->b.debug_flags & DBG_COMPUTE)) fprintf(stderr, fmt, ##args); \
	} while (0);

#define R600_ERR(fmt, args...) \
	fprintf(stderr, "EE %s:%d %s - "fmt, __FILE__, __LINE__, __func__, ##args)

/* For MSAA sample positions. */
#define FILL_SREG(s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y)  \
	(((s0x) & 0xf) | (((s0y) & 0xf) << 4) |		   \
	(((s1x) & 0xf) << 8) | (((s1y) & 0xf) << 12) |	   \
	(((s2x) & 0xf) << 16) | (((s2y) & 0xf) << 20) |	   \
	 (((s3x) & 0xf) << 24) | (((s3y) & 0xf) << 28))

#endif
