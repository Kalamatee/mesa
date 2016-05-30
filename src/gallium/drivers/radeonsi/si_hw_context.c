/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
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
 *      Jerome Glisse
 */

#include "si_pipe.h"

static unsigned si_descriptor_list_cs_space(unsigned count, unsigned element_size)
{
	/* Ensure we have enough space to start a new range in a hole */
	assert(element_size >= 3);

	/* 5 dwords for possible load to reinitialize when we have no preamble
	 * IB + 5 dwords for write to L2 + 3 bytes for every range written to
	 * CE RAM.
	 */
	return 5 + 5 + 3 + count * element_size;
}

static unsigned si_ce_needed_cs_space(void)
{
	unsigned space = 0;

	space += si_descriptor_list_cs_space(SI_NUM_CONST_BUFFERS, 4);
	space += si_descriptor_list_cs_space(SI_NUM_SHADER_BUFFERS, 4);
	space += si_descriptor_list_cs_space(SI_NUM_SAMPLERS, 16);
	space += si_descriptor_list_cs_space(SI_NUM_IMAGES, 8);
	space *= SI_NUM_SHADERS;

	space += si_descriptor_list_cs_space(SI_NUM_RW_BUFFERS, 4);

	/* Increment CE counter packet */
	space += 2;

	return space;
}

/* initialize */
void si_need_cs_space(struct si_context *ctx)
{
	struct radeon_winsys_cs *cs = ctx->b.gfx.cs;
	struct radeon_winsys_cs *ce_ib = ctx->ce_ib;
	struct radeon_winsys_cs *dma = ctx->b.dma.cs;

	/* Flush the DMA IB if it's not empty. */
	if (radeon_emitted(dma, 0))
		ctx->b.dma.flush(ctx, RADEON_FLUSH_ASYNC, NULL);

	/* There are two memory usage counters in the winsys for all buffers
	 * that have been added (cs_add_buffer) and two counters in the pipe
	 * driver for those that haven't been added yet.
	 */
	if (unlikely(!ctx->b.ws->cs_memory_below_limit(ctx->b.gfx.cs,
						       ctx->b.vram, ctx->b.gtt))) {
		ctx->b.gtt = 0;
		ctx->b.vram = 0;
		ctx->b.gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
		return;
	}
	ctx->b.gtt = 0;
	ctx->b.vram = 0;

	/* If the CS is sufficiently large, don't count the space needed
	 * and just flush if there is not enough space left.
	 */
	if (unlikely(cs->cdw > cs->max_dw - 2048 ||
                     (ce_ib && ce_ib->max_dw - ce_ib->cdw <
                      si_ce_needed_cs_space())))
		ctx->b.gfx.flush(ctx, RADEON_FLUSH_ASYNC, NULL);
}

void si_context_gfx_flush(void *context, unsigned flags,
			  struct pipe_fence_handle **fence)
{
	struct si_context *ctx = context;
	struct radeon_winsys_cs *cs = ctx->b.gfx.cs;
	struct radeon_winsys *ws = ctx->b.ws;

	if (ctx->gfx_flush_in_progress)
		return;

	ctx->gfx_flush_in_progress = true;

	if (!radeon_emitted(cs, ctx->b.initial_gfx_cs_size) &&
	    (!fence || ctx->last_gfx_fence)) {
		if (fence)
			ws->fence_reference(fence, ctx->last_gfx_fence);
		if (!(flags & RADEON_FLUSH_ASYNC))
			ws->cs_sync_flush(cs);
		ctx->gfx_flush_in_progress = false;
		return;
	}

	r600_preflush_suspend_features(&ctx->b);

	ctx->b.flags |= SI_CONTEXT_CS_PARTIAL_FLUSH |
			SI_CONTEXT_PS_PARTIAL_FLUSH;
	/* The kernel doesn't flush TC for VI correctly (need TC_WB_ACTION_ENA). */
	if (ctx->b.chip_class == VI)
		ctx->b.flags |= SI_CONTEXT_INV_GLOBAL_L2 |
				SI_CONTEXT_INV_VMEM_L1;

	si_emit_cache_flush(ctx, NULL);

	/* force to keep tiling flags */
	flags |= RADEON_FLUSH_KEEP_TILING_FLAGS;

	if (ctx->trace_buf)
		si_trace_emit(ctx);

	if (ctx->is_debug) {
		unsigned i;

		/* Save the IB for debug contexts. */
		free(ctx->last_ib);
		ctx->last_ib_dw_size = cs->cdw;
		ctx->last_ib = malloc(cs->cdw * 4);
		memcpy(ctx->last_ib, cs->buf, cs->cdw * 4);
		r600_resource_reference(&ctx->last_trace_buf, ctx->trace_buf);
		r600_resource_reference(&ctx->trace_buf, NULL);

		/* Save the buffer list. */
		if (ctx->last_bo_list) {
			for (i = 0; i < ctx->last_bo_count; i++)
				pb_reference(&ctx->last_bo_list[i].buf, NULL);
			free(ctx->last_bo_list);
		}
		ctx->last_bo_count = ws->cs_get_buffer_list(cs, NULL);
		ctx->last_bo_list = calloc(ctx->last_bo_count,
					   sizeof(ctx->last_bo_list[0]));
		ws->cs_get_buffer_list(cs, ctx->last_bo_list);
	}

	/* Flush the CS. */
	ws->cs_flush(cs, flags, &ctx->last_gfx_fence);

	if (fence)
		ws->fence_reference(fence, ctx->last_gfx_fence);

	/* Check VM faults if needed. */
	if (ctx->screen->b.debug_flags & DBG_CHECK_VM)
		si_check_vm_faults(ctx);

	si_begin_new_cs(ctx);
	ctx->gfx_flush_in_progress = false;
}

void si_begin_new_cs(struct si_context *ctx)
{
	if (ctx->is_debug) {
		uint32_t zero = 0;

		/* Create a buffer used for writing trace IDs and initialize it to 0. */
		assert(!ctx->trace_buf);
		ctx->trace_buf = (struct r600_resource*)
				 pipe_buffer_create(ctx->b.b.screen, PIPE_BIND_CUSTOM,
						    PIPE_USAGE_STAGING, 4);
		if (ctx->trace_buf)
			pipe_buffer_write_nooverlap(&ctx->b.b, &ctx->trace_buf->b.b,
						    0, sizeof(zero), &zero);
		ctx->trace_id = 0;
	}

	if (ctx->trace_buf)
		si_trace_emit(ctx);

	/* Flush read caches at the beginning of CS not flushed by the kernel. */
	if (ctx->b.chip_class >= CIK)
		ctx->b.flags |= SI_CONTEXT_INV_SMEM_L1 |
				SI_CONTEXT_INV_ICACHE;

	ctx->b.flags |= R600_CONTEXT_START_PIPELINE_STATS;

	/* set all valid group as dirty so they get reemited on
	 * next draw command
	 */
	si_pm4_reset_emitted(ctx);

	/* The CS initialization should be emitted before everything else. */
	si_pm4_emit(ctx, ctx->init_config);
	if (ctx->init_config_gs_rings)
		si_pm4_emit(ctx, ctx->init_config_gs_rings);

	if (ctx->ce_preamble_ib)
		si_ce_enable_loads(ctx->ce_preamble_ib);
	else if (ctx->ce_ib)
		si_ce_enable_loads(ctx->ce_ib);

	ctx->framebuffer.dirty_cbufs = (1 << 8) - 1;
	ctx->framebuffer.dirty_zsbuf = true;
	si_mark_atom_dirty(ctx, &ctx->framebuffer.atom);

	si_mark_atom_dirty(ctx, &ctx->clip_regs);
	si_mark_atom_dirty(ctx, &ctx->clip_state.atom);
	si_mark_atom_dirty(ctx, &ctx->msaa_sample_locs);
	si_mark_atom_dirty(ctx, &ctx->msaa_config);
	si_mark_atom_dirty(ctx, &ctx->sample_mask.atom);
	si_mark_atom_dirty(ctx, &ctx->cb_render_state);
	si_mark_atom_dirty(ctx, &ctx->blend_color.atom);
	si_mark_atom_dirty(ctx, &ctx->db_render_state);
	si_mark_atom_dirty(ctx, &ctx->stencil_ref.atom);
	si_mark_atom_dirty(ctx, &ctx->spi_map);
	si_mark_atom_dirty(ctx, &ctx->b.streamout.enable_atom);
	si_mark_atom_dirty(ctx, &ctx->b.render_cond_atom);
	si_all_descriptors_begin_new_cs(ctx);

	ctx->b.scissors.dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
	ctx->b.viewports.dirty_mask = (1 << R600_MAX_VIEWPORTS) - 1;
	si_mark_atom_dirty(ctx, &ctx->b.scissors.atom);
	si_mark_atom_dirty(ctx, &ctx->b.viewports.atom);

	r600_postflush_resume_features(&ctx->b);

	ctx->b.initial_gfx_cs_size = ctx->b.gfx.cs->cdw;

	/* Invalidate various draw states so that they are emitted before
	 * the first draw call. */
	si_invalidate_draw_sh_constants(ctx);
	ctx->last_primitive_restart_en = -1;
	ctx->last_restart_index = SI_RESTART_INDEX_UNKNOWN;
	ctx->last_gs_out_prim = -1;
	ctx->last_prim = -1;
	ctx->last_multi_vgt_param = -1;
	ctx->last_ls_hs_config = -1;
	ctx->last_rast_prim = -1;
	ctx->last_sc_line_stipple = ~0;
	ctx->emit_scratch_reloc = true;
	ctx->last_ls = NULL;
	ctx->last_tcs = NULL;
	ctx->last_tes_sh_base = -1;
	ctx->last_num_tcs_input_cp = -1;

	ctx->cs_shader_state.initialized = false;
}
