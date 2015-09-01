/*
    Copyright © 2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#ifndef INTELGMA_STATE_H_
#define INTELGMA_STATE_H_

typedef struct {
    uint32_t	fp;			        // G45_FPA0
    uint32_t	dpll;			        // G45_DPLL_A
    uint32_t	dpll_md_reg;	                // G45_DPLL_A_MD
    uint32_t	pipeconf;		        // G45_PIPEACONF
    uint32_t	pipesrc;		        // G45_PIPEASRC
    uint32_t	dspcntr;		        // G45_DSPACNTR
    uint32_t	dspsurf;		        // G45_DSPASURF
    uint32_t	dsplinoff;		        // G45_DSPALINOFF
    uint32_t	dspstride;		        // G45_DSPASTRIDE
    uint32_t	htotal;			        // G45_HTOTAL_A
    uint32_t	hblank;			        // G45_HBLANK_A
    uint32_t	hsync;			        // G45_HSYNC_A
    uint32_t	vtotal;			        // G45_VTOTAL_A
    uint32_t	vblank;			        // G45_VBLANK_A
    uint32_t	vsync;			        // G45_VSYNC_A
    uint32_t	adpa;			        // G45_ADPA
} GMAState_t;

#endif /* INTELGMA_STATE_H_ */
