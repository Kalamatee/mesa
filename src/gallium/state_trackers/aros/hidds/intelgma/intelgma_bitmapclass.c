/*
    Copyright � 1995-2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#define DEBUG 0
#include <aros/debug.h>

#include <proto/oop.h>
#include <proto/exec.h>
#include <proto/utility.h>

#include <aros/libcall.h>
#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <utility/tagitem.h>
#include <hidd/gfx.h>
#include <hidd/i2c.h>

#include <stdint.h>
#include <stdlib.h>

#include "intelgma_hidd.h"
#include "intelgma_overlay.h"
#include "intelG45_regs.h"

#if defined(INTELGMA_COMPOSIT)
#include <hidd/compositor.h>
#endif

struct __ROP ROP_table[] = {
    { ROP3_ZERO, ROP3_ZERO }, /* GXclear        */
    { ROP3_DSa,  ROP3_DPa  }, /* Gxand          */
    { ROP3_SDna, ROP3_PDna }, /* GXandReverse   */
    { ROP3_S,    ROP3_P    }, /* GXcopy         */
    { ROP3_DSna, ROP3_DPna }, /* GXandInverted  */
    { ROP3_D,    ROP3_D    }, /* GXnoop         */
    { ROP3_DSx,  ROP3_DPx  }, /* GXxor          */
    { ROP3_DSo,  ROP3_DPo  }, /* GXor           */
    { ROP3_DSon, ROP3_DPon }, /* GXnor          */
    { ROP3_DSxn, ROP3_PDxn }, /* GXequiv        */
    { ROP3_Dn,   ROP3_Dn   }, /* GXinvert       */
    { ROP3_SDno, ROP3_PDno }, /* GXorReverse    */
    { ROP3_Sn,   ROP3_Pn   }, /* GXcopyInverted */
    { ROP3_DSno, ROP3_DPno }, /* GXorInverted   */
    { ROP3_DSan, ROP3_DPan }, /* GXnand         */
    { ROP3_ONE,  ROP3_ONE  }  /* GXset          */
};

#define sd ((struct g45staticdata*)SD(cl))

#define POINT_OUTSIDE_CLIP(gc, x, y)	\
	(  (x) < GC_CLIPX1(gc)		\
	|| (x) > GC_CLIPX2(gc)		\
	|| (y) < GC_CLIPY1(gc)		\
	|| (y) > GC_CLIPY2(gc) )

static inline BOOL IntelGMA_HasBLTEngine(UWORD product_id)
{
    return ((product_id >= 0x2582)
        && (product_id <= 0x27ae));
}

OOP_Object *METHOD(BitMapIntelGMA, Root, New)
{
    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    if (o)
    {
        GMABitMap_t *bm = OOP_INST_DATA(cl, o);

        IPTR width, height, depth;
        IPTR displayable, pixfmt;
        UBYTE bytesPerPixel;
        OOP_Object *pf;

        InitSemaphore(&bm->bmLock);

        D(bug("[IntelGMA:BitMap] Super called. o=%p\n", o));

        /* cache the bitmaps display, and its enumerator */
        OOP_GetAttr(o, aHidd_BitMap_Display, (IPTR *)&bm->display);
        OOP_GetAttr(o, aHidd_BitMap_StdPixFmt, &pixfmt);
        OOP_GetAttr(o, aHidd_BitMap_Width,  &width);
        OOP_GetAttr(o, aHidd_BitMap_Height, &height);

        /* is it an overlay mode ? */
        if ((pixfmt > vHidd_StdPixFmt_Plane) && (pixfmt < num_Hidd_AllPf))
        {
            if ((width <= 2048) && (height < 1024))
            {
                IPTR dispoverlay;

                OOP_GetAttr(bm->display, aHidd_DisplayIntelGMA_Overlay, &dispoverlay);

                if (!dispoverlay)
                {
                    OOP_SetAttrsTags(bm->display, aHidd_DisplayIntelGMA_Overlay, o, TAG_DONE);
                    IntelGMA_OverlayEnable();
                    return o;
                }
            }
            else
            {
                D(bug("[IntelGMA:BitMap] Illegal overlay dimensions %dx%d\n", width, height));
            }
            OOP_MethodID disp_mid = sd->mid_Dispose;
            OOP_CoerceMethod(cl, o, (OOP_Msg) &disp_mid);
            return NULL;
        }

        OOP_GetAttr(bm->display, aHidd_Display_DMEnumerator, (IPTR *)&bm->dmenum);

        OOP_GetAttr(o, aHidd_BitMap_PixFmt, (IPTR *)&pf);
        OOP_GetAttr(pf, aHidd_PixFmt_Depth, &depth);
        OOP_GetAttr(o, aHidd_BitMap_Displayable, &displayable);

        bm->onbm = displayable;

        D(bug("[IntelGMA:BitMap] width=%d height=%d depth=%d\n", width, height, depth));

        if ((width == 0) || (height == 0) || (depth == 0))
        {
            bug("[IntelGMA:BitMap] size mismatch!\n");
        }

        if (depth == 24)
                depth = 32;

        if (depth <= 8)
            bytesPerPixel = 1;
        else if (depth <= 16)
            bytesPerPixel = 2;
        else
            bytesPerPixel = 4;

        bm->width = width;
        bm->height = height;
        bm->pitch = (width * bytesPerPixel + 63) & ~63;
        bm->depth = depth;
        bm->bpp = bytesPerPixel;
        bm->framebuffer = AllocBitmapArea(sd, bm->width, bm->height, bm->bpp);
        bm->fbgfx = TRUE;
        bm->state = NULL;
        bm->bitmap = o;
        bm->usecount = 0;
        bm->fbid = 0; /* Default value */

#if defined(INTELGMA_COMPOSIT)
        bm->xoffset = 0;
        bm->yoffset = 0;

                //bm->compositing = sd->compositing;
        bm->compositing = (OOP_Object *)
            GetTagData(aHidd_BitMap_IntelGMA_CompositorHidd, 0, msg->attrList);
        /* FIXME: check if compositing hidd was passed */

#endif

        if (displayable)
        {
            bm->displayable = TRUE;

            if ((bm->framebuffer != -1))
            {
                HIDDT_ModeID modeid;
                OOP_Object *sync;

                bm->fbgfx = TRUE;

                /* We should be able to get modeID from the bitmap */
                OOP_GetAttr(o, aHidd_BitMap_ModeID, &modeid);

                D(bug("[IntelGMA:BitMap] BM_ModeID=%x\n", modeid));

                if (modeid != vHidd_ModeID_Invalid)
                {
                    IPTR pixel;
                    IPTR hdisp, vdisp, hstart, hend, htotal, vstart, vend, vtotal, flags;
                    OOP_Object *dmenum;

                    /* Get Sync and PixelFormat properties */
                    struct pHidd_DMEnum_GetMode __getmodemsg = {
                        mID:  sd->mid_GetMode,
                        modeID: modeid,
                        syncPtr:    &sync,
                        pixFmtPtr:  &pf,
                    }, *getmodemsg = &__getmodemsg;

                    D(bug("[IntelGMA:BitMap] Querying Mode...\n"));
                    OOP_DoMethod(bm->dmenum, (OOP_Msg)getmodemsg);

                    D(bug("[IntelGMA:BitMap] sync @ 0x%p, pixfmt @ 0x%p\n", sync, pf));

                    OOP_GetAttr(sync, aHidd_Sync_PixelClock,    &pixel);
                    OOP_GetAttr(sync, aHidd_Sync_HDisp,         &hdisp);
                    OOP_GetAttr(sync, aHidd_Sync_VDisp,         &vdisp);
                    OOP_GetAttr(sync, aHidd_Sync_HSyncStart,    &hstart);
                    OOP_GetAttr(sync, aHidd_Sync_VSyncStart,    &vstart);
                    OOP_GetAttr(sync, aHidd_Sync_HSyncEnd,      &hend);
                    OOP_GetAttr(sync, aHidd_Sync_VSyncEnd,      &vend);
                    OOP_GetAttr(sync, aHidd_Sync_HTotal,        &htotal);
                    OOP_GetAttr(sync, aHidd_Sync_VTotal,        &vtotal);
                    OOP_GetAttr(sync, aHidd_Sync_Flags,			&flags);

                    bm->state = (GMAState_t *)AllocVecPooled(sd->MemPool,
                            sizeof(GMAState_t));

                    D(bug("[IntelGMA:BitMap] allocated state data @ 0x%p\n", bm->state));

                    pixel /= 1000;

                    if (bm->state)
                    {
                        G45_InitMode(sd, bm->state, width, height, depth, pixel, bm->framebuffer,
                                    hdisp, vdisp,
                                    hstart, hend, htotal,
                                    vstart, vend, vtotal, flags);

                        if (!sd->flags & SDFLAG_INITIALISED)
                        {
                            sd->VisibleBitmap = bm;
                            G45_LoadState(sd, bm->state);
                        }

                        D(bug("[IntelGMA] displayable Bitmap::new = %p\n", o));

                        return o;
                    }
                }
                else
                {
                    D(bug("[IntelGMA] invalid mode\n"));
                }
            }
            else
            {
                    bm->framebuffer = (IPTR)AllocMem(bm->pitch * bm->height,
                                    MEMF_PUBLIC | MEMF_CLEAR);
                    bm->fbgfx = FALSE;

                    return o;
            }
        }
        else
        {
            bm->displayable = FALSE;

            if (bm->framebuffer == -1)
            {
                bm->framebuffer = (IPTR)AllocMem(bm->pitch * bm->height,
                            MEMF_PUBLIC | MEMF_CLEAR);
                bm->fbgfx = FALSE;
            }

            if (bm->framebuffer != 0)
            {
                D(bug("[IntelGMA] not displayable Bitmap::new = %p\n", o));
                return o;
            }
        }

        OOP_MethodID disp_mid = sd->mid_Dispose;
        OOP_CoerceMethod(cl, o, (OOP_Msg) &disp_mid);
    }

    return NULL;
}


VOID METHOD(BitMapIntelGMA, Root, Dispose)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP
    {
        LOCK_HW
        {
            if (bm->fbgfx)
            {
                DO_FLUSH();

                FreeBitmapArea(sd, bm->framebuffer, bm->width, bm->height, bm->bpp);

                if (sd->VisibleBitmap == bm)
                {
                        sd->VisibleBitmap = NULL;
                }

                bm->framebuffer = -1;
                bm->fbgfx = 0;
            }
            else
            {
                if (bm->framebuffer)
                    FreeMem((APTR)bm->framebuffer, bm->pitch * bm->height);
            }

            if (bm->state)
                FreeVecPooled(sd->MemPool, bm->state);

            bm->state = NULL;

#if 0
            RADEONWaitForIdleMMIO(sd);
            FreeVecPooled(sd->memPool, bm->addresses);
#endif
        }
        UNLOCK_HW
    }
    UNLOCK_BITMAP

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(BitMapIntelGMA, Root, Get)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    ULONG idx;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    if (IS_BitMapIntelGMA_ATTR(msg->attrID, idx))
    {
        switch (idx)
        {
        case aoHidd_BitMap_IntelGMA_Drawable:
            if (bm->fbgfx)
                *msg->storage = bm->framebuffer + (IPTR)sd->Card.Framebuffer;
            else
                *msg->storage = bm->framebuffer;
            break;
        default:
            OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
        }
    }
    else
    {
#if defined(INTELGMA_COMPOSIT)
        if (IS_BM_ATTR(msg->attrID, idx))
        {
            switch (idx)
            {
            case aoHidd_BitMap_LeftEdge:
                    *msg->storage = bm->xoffset;
                    return;
            case aoHidd_BitMap_TopEdge:
                    *msg->storage = bm->yoffset;
                    return;
            }
        }
#endif
        OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    }
}

VOID METHOD(BitMapIntelGMA, Root, Set)
{
    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

#if defined(INTELGMA_COMPOSIT)
    struct TagItem  *tag, *tstate;
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, o);
    ULONG idx;
    LONG newxoffset = bmdata->xoffset;
    LONG newyoffset = bmdata->yoffset;
    tstate = msg->attrList;

    while((tag = NextTagItem(&tstate)))
    {
        if(IS_BITMAP_ATTR(tag->ti_Tag, idx))
        {
            switch(idx)
            {
            case aoHidd_BitMap_LeftEdge:
                newxoffset = tag->ti_Data;
                break;
            case aoHidd_BitMap_TopEdge:
                newyoffset = tag->ti_Data;
                break;
            }
        }
    }

    /* If there was a change requested, validate it */
    struct pHidd_Compositor_ValidateBitMapPositionChange vbpcmsg =
    {
        mID : SD(cl)->mid_ValidateBitMapPositionChange,
        bm : o,
        newxoffset : &newxoffset,
        newyoffset : &newyoffset
    };
    
    OOP_DoMethod(bmdata->compositing, (OOP_Msg)&vbpcmsg);

    if ((newxoffset != bmdata->xoffset) || (newyoffset != bmdata->yoffset))
    {
        /* If change passed validation, execute it */
        struct pHidd_Compositor_BitMapPositionChanged bpcmsg =
        {
            mID : SD(cl)->mid_BitMapPositionChanged,
            bm : o
        };
    
        bmdata->xoffset = newxoffset;
        bmdata->yoffset = newyoffset;
    
        OOP_DoMethod(bmdata->compositing, (OOP_Msg)&bpcmsg);
    }
#endif

    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutPixel)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    void *ptr;

#if (0)
    D(bug("[IntelGMA:BitMap] %s()\n", __func__));
#endif

//	if (msg->x >= 0 && msg->x < bm->width && msg->y >= 0 && msg->y < bm->height)
    {
        LOCK_BITMAP
        {
            if (bm->fbgfx)
            {
                LOCK_HW
                {
                    DO_FLUSH();
                }
                UNLOCK_HW
            }

            ptr = (void *)(bm->framebuffer + msg->y * bm->pitch);
            if (bm->fbgfx)
                ptr = (void *)((IPTR)ptr + (IPTR)sd->Card.Framebuffer);

            switch (bm->bpp)
            {
            case 1:
                ((UBYTE *)ptr)[msg->x] = msg->pixel;
                break;
            case 2:
                ((UWORD *)ptr)[msg->x] = msg->pixel;
                break;
            case 4:
                ((ULONG *)ptr)[msg->x] = msg->pixel;
                break;
            }
        }
        UNLOCK_BITMAP
    }
}

HIDDT_Pixel METHOD(BitMapIntelGMA, Hidd_BitMap, GetPixel)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    HIDDT_Pixel pixel = 0;
    void *ptr;

#if (0)
    D(bug("[IntelGMA:BitMap] %s()\n", __func__));
#endif

    LOCK_BITMAP
    {
        if (bm->fbgfx)
        {
            LOCK_HW
            {
                DO_FLUSH();
            }
            UNLOCK_HW
        }

        ptr = (void *)(bm->framebuffer + msg->y * bm->pitch);
        if (bm->fbgfx)
            ptr = (void *)((IPTR)ptr + (IPTR)sd->Card.Framebuffer);

        switch (bm->bpp)
        {
        case 1:
                pixel = ((UBYTE *)ptr)[msg->x];
                break;
        case 2:
                pixel = ((UWORD *)ptr)[msg->x];
                break;
        case 4:
                pixel = ((ULONG *)ptr)[msg->x];
                break;
        }
    }
    UNLOCK_BITMAP

    return pixel;
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, DrawPixel)
{
    GMABitMap_t         *bm = OOP_INST_DATA(cl, o);
    OOP_Object          *gc = msg->gc;
    HIDDT_Pixel     	src, dest = 0, val;
    HIDDT_DrawMode  	mode;
    HIDDT_Pixel     	writeMask;
    void                *ptr;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    ptr = (void *)(bm->framebuffer + msg->y * bm->pitch);
    if (bm->fbgfx)
        ptr = (void *)((IPTR)ptr + (IPTR)sd->Card.Framebuffer);

    src       = GC_FG(gc);
    mode      = GC_DRMD(gc);

    LOCK_BITMAP
    {
	if (bm->fbgfx)
	{
            LOCK_HW
            {
                DO_FLUSH();
            }
            UNLOCK_HW
	}

        if (vHidd_GC_DrawMode_Copy == mode && GC_COLMASK(gc) == ~0)
        {
            val = src;
        }
        else
        {
            switch (bm->bpp)
            {
                case 1:
                    dest = ((UBYTE *)ptr)[msg->x];
                    break;
                case 2:
                    dest = ((UWORD *)ptr)[msg->x];
                    break;
                case 4:
                    dest = ((ULONG *)ptr)[msg->x];
                    break;
            }

            writeMask = ~GC_COLMASK(gc) & dest;

            val = 0;

            if(mode & 1)
                val = ( src &  dest);
            if(mode & 2)
                val = ( src & ~dest) | val;
            if(mode & 4)
                val = (~src &  dest) | val;
            if(mode & 8)
                val = (~src & ~dest) | val;

            val = (val & (writeMask | GC_COLMASK(gc) )) | writeMask;
        }

        if (bm->fbgfx)
        {
            LOCK_HW
            {
                DO_FLUSH();
            }
            UNLOCK_HW
        }

        switch (bm->bpp)
        {
            case 1:
                ((UBYTE *)ptr)[msg->x] = val;
                break;
            case 2:
                ((UWORD *)ptr)[msg->x] = val;
                break;
            case 4:
                ((ULONG *)ptr)[msg->x] = val;
                break;
        }
    }
    UNLOCK_BITMAP
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, DrawEllipse)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    OOP_Object *gc = msg->gc;
    WORD    	x = msg->rx, y = 0;     /* ellipse points */
    HIDDT_Pixel     	    	    src;
    HIDDT_DrawMode  	    	    mode;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    /* intermediate terms to speed up loop */
    LONG    	t1 = msg->rx * msg->rx, t2 = t1 << 1, t3 = t2 << 1;
    LONG    	t4 = msg->ry * msg->ry, t5 = t4 << 1, t6 = t5 << 1;
    LONG    	t7 = msg->rx * t5, t8 = t7 << 1, t9 = 0L;
    LONG    	d1 = t2 - t7 + (t4 >> 1);    /* error terms */
    LONG    	d2 = (t1 >> 1) - t8 + t5;

    APTR    	doclip = GC_DOCLIP(gc);

    src       = GC_FG(gc);
    mode      = GC_DRMD(gc);

    void _drawpixel(int x, int y)
    {
            OUT_RING((2 << 29) | (0x24 << 22) );
            OUT_RING((y << 16) | x);
    }

    LOCK_BITMAP

    if (bm->fbgfx)
    {
    	LOCK_HW

    	uint32_t br00, br01, br24, br25, br09, br05, br06, br07;

    	br00 = (2 << 29) | (0x1 << 22) | 6;

    	if (bm->bpp == 4)
    		br00 |= (3 << 20);

    	br01 = ROP_table[mode].pattern | (bm->pitch);
    	if (bm->bpp == 4)
    		br01 |= 3 << 24;
    	else if (bm->bpp == 2)
    		br01 |= 1 << 24;

    	if (doclip)
    		br01 |= (1 << 30);

    	br24 = GC_CLIPX1(gc) | (GC_CLIPY1(gc) << 16);
    	br25 = (GC_CLIPX2(gc)+1) | ((GC_CLIPY2(gc)+1) << 16);
    	br09 = bm->framebuffer;
    	br05 = src;
    	br06 = 0;
    	br07 = 0;

    	START_RING(8);

    	OUT_RING(br00);
    	OUT_RING(br01);
    	OUT_RING(br24);
    	OUT_RING(br25);
    	OUT_RING(br09);
    	OUT_RING(br05);
    	OUT_RING(br06);
    	OUT_RING(br07);

    	while (d2 < 0)                  /* til slope = -1 */
    	{
    		/* draw 4 points using symmetry */

    		{
    			START_RING(2*4);

    			_drawpixel(msg->x + x, msg->y + y);
    			_drawpixel(msg->x + x, msg->y - y);
    			_drawpixel(msg->x - x, msg->y + y);
    			_drawpixel(msg->x - x, msg->y - y);

    			ADVANCE_RING();
    		}

    		y++;            /* always move up here */
    		t9 = t9 + t3;
    		if (d1 < 0)     /* move straight up */
    		{
    			d1 = d1 + t9 + t2;
    			d2 = d2 + t9;
    		}
    		else            /* move up and left */
    		{
    			x--;
    			t8 = t8 - t6;
    			d1 = d1 + t9 + t2 - t8;
    			d2 = d2 + t9 + t5 - t8;
    		}
    	}

    	do                              /* rest of top right quadrant */
    	{
    		/* draw 4 points using symmetry */
    		{
    			START_RING(2*4);

    			_drawpixel(msg->x + x, msg->y + y);
    			_drawpixel(msg->x + x, msg->y - y);
    			_drawpixel(msg->x - x, msg->y + y);
    			_drawpixel(msg->x - x, msg->y - y);

    			ADVANCE_RING();
    		}

    		x--;            /* always move left here */
    		t8 = t8 - t6;
    		if (d2 < 0)     /* move up and left */
    		{
    			y++;
    			t9 = t9 + t3;
    			d2 = d2 + t9 + t5 - t8;
    		}
    		else            /* move straight left */
    		{
    			d2 = d2 + t5 - t8;
    		}

    	} while (x >= 0);

    	UNLOCK_HW
    }
    else
    	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    UNLOCK_BITMAP
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, DrawLine)
{
	GMABitMap_t *bm = OOP_INST_DATA(cl, o);
	OOP_Object *gc = msg->gc;
	HIDDT_Pixel     	    	    src;
	HIDDT_DrawMode  	    	    mode;

	WORD        dx, dy, incrE, incrNE, d, x, y, s1, s2, t, i;
	LONG        x1, y1, x2, y2;

	APTR    	doclip = GC_DOCLIP(gc);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    src       = GC_FG(gc);
    mode      = GC_DRMD(gc);

	void _drawpixel(int x, int y)
	{
		OUT_RING((2 << 29) | (0x24 << 22) );
		OUT_RING((y << 16) | x);
	}

    if (doclip)
    {
        /* If line is not inside cliprect, then just return */
        /* Normalize coords */
        if (msg->x1 > msg->x2)
        {
            x1 = msg->x2; x2 = msg->x1;
        }
        else
        {
            x1 = msg->x1; x2 = msg->x2;
        }

        if (msg->y1 > msg->y2)
        {
            y1 = msg->y2; y2 = msg->y1;
        }
        else
        {
            y1 = msg->y1; y2 = msg->y2;
        }

        if (    x1 > GC_CLIPX2(gc)
             || x2 < GC_CLIPX1(gc)
             || y1 > GC_CLIPY2(gc)
             || y2 < GC_CLIPY1(gc) )
        {

             /* Line is not inside cliprect, so just return */
             return;

        }
    }

    x1 = msg->x1;
    y1 = msg->y1;
    x2 = msg->x2;
    y2 = msg->y2;

	LOCK_BITMAP

	if ((GC_LINEPAT(gc) != (UWORD)~0) || !bm->fbgfx)
	{
		OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
	}
	else
	{
		LOCK_HW

		uint32_t br00, br01, br24, br25, br09, br05, br06, br07;
		uint32_t ring_space;

		br00 = (2 << 29) | (0x1 << 22) | 6;

		if (bm->bpp == 4)
			br00 |= (3 << 20);

		br01 = ROP_table[mode].pattern | (bm->pitch);
		if (bm->bpp == 4)
			br01 |= 3 << 24;
		else if (bm->bpp == 2)
			br01 |= 1 << 24;

		if (doclip)
			br01 |= (1 << 30);

		br24 = GC_CLIPX1(gc) | (GC_CLIPY1(gc) << 16);
		br25 = (GC_CLIPX2(gc)+1) | ((GC_CLIPY2(gc)+1) << 16);
		br09 = bm->framebuffer;
		br05 = src;
		br06 = 0;
		br07 = 0;

		START_RING(8);

		OUT_RING(br00);
		OUT_RING(br01);
		OUT_RING(br24);
		OUT_RING(br25);
		OUT_RING(br09);
		OUT_RING(br05);
		OUT_RING(br06);
		OUT_RING(br07);

		if (y1 == y2)
		{
			/*
	            Horizontal line drawing code.
			 */
			y = y1;

			/* Don't swap coordinates if x2 < x1! Because of linepattern! */

			if (x1 < x2)
			{
				x2++;
				dx = 1;
			}
			else
			{
				x2--;
				dx = -1;
			}

			ring_space = x2-x1+1;
			if (ring_space > 500)
				ring_space = 500;

			START_RING(ring_space);

			for(i = x1; i != x2; i += dx)
			{
				_drawpixel(i, y);

				ring_space-=2;
				if (!ring_space)
				{
					ADVANCE_RING();
					ring_space = x2-i+1;
					if (ring_space > 500)
						ring_space=500;
					START_RING(ring_space);
				}
			}
		}
	    else if (x1 == x2)
	    {
	        /*
	            Vertical line drawing code.
	        */
	        x = x1;

	        /* Don't swap coordinates if y2 < y1! Because of linepattern! */

	        if (y1 < y2)
	        {
	            y2++;
	            dy = 1;
	        }
	        else
	        {
	            y2--;
	            dy = -1;
	        }

			ring_space = y2-y1+1;
			if (ring_space > 500)
				ring_space = 500;

			START_RING(ring_space);

	        for(i = y1; i != y2; i += dy)
	        {
	        	_drawpixel(x, i);

				ring_space-=2;
				if (!ring_space)
				{
					ADVANCE_RING();
					ring_space = y2-i+1;
					if (ring_space > 500)
						ring_space=500;
					START_RING(ring_space);
				}
	        }
	    }
	    else
	    {
	        /*
	            Generic line drawing code.
	        */
	        /* Calculate slope */
	        dx = abs(x2 - x1);
	        dy = abs(y2 - y1);

	        /* which direction? */
	        if((x2 - x1) > 0) s1 = 1; else s1 = - 1;
	        if((y2 - y1) > 0) s2 = 1; else s2 = - 1;

	        /* change axes if dx < dy */
	        if(dx < dy)
	        {
	            d = dx;
	            dx = dy;
	            dy = d;
	            t = 0;
	        }
	        else
	        {
	           t = 1;
	        }

	        d  = 2 * dy - dx;        /* initial value of d */

	        incrE  = 2 * dy;         /* Increment use for move to E  */
	        incrNE = 2 * (dy - dx);  /* Increment use for move to NE */

	        x = x1; y = y1;

			ring_space = dx+1;
			if (ring_space > 500)
				ring_space = 500;

			START_RING(ring_space);

	        for(i = 0; i <= dx; i++)
	        {
	            /* Pixel inside ? */
	        	_drawpixel(x, y);

				ring_space-=2;
				if (!ring_space)
				{
					ADVANCE_RING();
					ring_space = dx-i+1;
					if (ring_space > 500)
						ring_space=500;
					START_RING(ring_space);
				}

	             if(d <= 0)
	             {
	                 if(t == 1)
	                 {
	                     x = x + s1;
	                 }
	                 else
	                 {
	                     y = y + s2;
	                 }

	                 d = d + incrE;
	             }
	             else
	             {
	                 x = x + s1;
	                 y = y + s2;
	                 d = d + incrNE;
	             }
	         }
	     }

		ADVANCE_RING();

		DO_FLUSH();
		UNLOCK_HW
	}

	UNLOCK_BITMAP
}

ULONG METHOD(BitMapIntelGMA, Hidd_BitMap, BytesPerLine)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    return (bm->pitch);
}

BOOL METHOD(BitMapIntelGMA, Hidd_BitMap, ObtainDirectAccess)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    IPTR VideoData;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

    VideoData = bm->framebuffer;
    if (bm->fbgfx)
    {
        VideoData += (IPTR)sd->Card.Framebuffer;
        LOCK_HW
        {
            DO_FLUSH();
        }
        UNLOCK_HW
    }
    *msg->addressReturn = (UBYTE*)VideoData;
    *msg->widthReturn = bm->pitch / bm->bpp;
    *msg->heightReturn = bm->height;
    *msg->bankSizeReturn = *msg->memSizeReturn = bm->pitch * bm->height;

    return TRUE;
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, ReleaseDirectAccess)
{

    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    UNLOCK_BITMAP

#if defined(INTELGMA_COMPOSIT)
	if (bm->displayable)
    {
        struct pHidd_Compositor_BitMapRectChanged brcmsg =
        {
            mID : SD(cl)->mid_BitMapRectChanged,
            bm : o,
            x : 0,
            y : 0,
            width : bm->width,
            height : bm->height
        };
        OOP_DoMethod(bm->compositing, (OOP_Msg)&brcmsg);    
    }
#endif
}

#define pHidd_BitMap_FillRect pHidd_BitMap_DrawRect

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, FillRect)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

    if (bm->fbgfx)
    {
        LOCK_HW

        uint32_t br00,br13,br14,br09,br16;

        br00 = (2 << 29) | (0x40 << 22) | 3;

        if (bm->bpp == 4)
            br00 |= 3 << 20;

        br13 = (ROP_table[GC_DRMD(msg->gc)].pattern) | (bm->pitch);
        switch (bm->bpp)
        {
        case 4:
            br13 |= (3 << 24);
            break;
        case 2:
            br13 |= 1 << 24;
            break;
        default:
            break;
        }

        br14 = ((msg->maxY - msg->minY+1) << 16) | ((msg->maxX - msg->minX+1) * bm->bpp);

        br09 = bm->framebuffer + msg->minX * bm->bpp + msg->minY * bm->pitch;
        br16 = GC_FG(msg->gc);

        START_RING(6);
        OUT_RING(br00);
        OUT_RING(br13);
        OUT_RING(br14);
        OUT_RING(br09);
        OUT_RING(br16);
        OUT_RING(0);
        ADVANCE_RING();

        DO_FLUSH();
        UNLOCK_HW
    }
    else
        OOP_DoSuperMethod(cl, o, (OOP_Msg) msg);

    UNLOCK_BITMAP
}

/* Unaccelerated functions */

static inline int do_alpha(int a, int v)
{
    int tmp = a*v;
    return ((tmp << 8) + tmp + 32768) >> 16;
}

/*
    the BLT engine (the blitter) can not do alpha blending,
    however we may utilise the 3d engine in future ..
*/
VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutAlphaImage)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    IPTR VideoData;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

    VideoData = bm->framebuffer;

    if ((bm->fbgfx) && ((bm->bpp == 2) || (bm->bpp == 4)))
    {
        ULONG *pixarray, *pasrc = (ULONG *)msg->pixels;
        ULONG srcpix, x, y = msg->y;
        LONG src_red, src_green, src_blue, src_alpha;
        LONG dst_red, dst_green, dst_blue;
    	UWORD height = msg->height;
    	UWORD bw = msg->width;

        /* Since we wont use the BLT engine now, flush the chip */
        LOCK_HW
        DO_FLUSH();
        UNLOCK_HW

        /*
         * Treat each depth case separately
         */
        if (bm->bpp == 4)
        {
            ULONG       *xbuf = (ULONG *)(VideoData + sd->Card.Framebuffer + (msg->x << 2) + (y * bm->pitch));
            ULONG       destpix;

            while(height--)
            {
                pixarray = pasrc;
                for (x=0; x < bw; x++)
                {
                    /* Read RGBA pixel from input array */
                    srcpix = *pixarray++;
#if AROS_BIG_ENDIAN
                    src_red   = (srcpix & 0x00FF0000) >> 16;
                    src_green = (srcpix & 0x0000FF00) >> 8;
                    src_blue  = (srcpix & 0x000000FF);
                    src_alpha = (srcpix & 0xFF000000) >> 24;
#else
                    src_red   = (srcpix & 0x0000FF00) >> 8;
                    src_green = (srcpix & 0x00FF0000) >> 16;
                    src_blue  = (srcpix & 0xFF000000) >> 24;
                    src_alpha = (srcpix & 0x000000FF);
#endif
                    /*
                     * If the pixel is Opaque (alpha=0), skip unnecessary
                     * reads and writes to VRAM.
                     */
                    if (src_alpha != 0)
                    {
                        if (src_alpha == 0xff)
                        {
                            /* Fully Transparent, just copy the source pixel */
                            dst_red = src_red;
                            dst_green = src_green;
                            dst_blue = src_blue;
                        }
                        else
                        {
                            /* Alpha blend the source and destination pixels */
                            destpix = xbuf[x];
//#if AROS_BIG_ENDIAN
//                            dst_red   = (destpix & 0x0000FF00) >> 8;
//                            dst_green = (destpix & 0x00FF0000) >> 16;
//                            dst_blue  = (destpix & 0xFF000000) >> 24;
//#else
                            dst_red   = (destpix & 0x00FF0000) >> 16;
                            dst_green = (destpix & 0x0000FF00) >> 8;
                            dst_blue  = (destpix & 0x000000FF);
//#endif

                            dst_red   += do_alpha(src_alpha, src_red - dst_red);
                            dst_green += do_alpha(src_alpha, src_green - dst_green);
                            dst_blue  += do_alpha(src_alpha, src_blue - dst_blue);

                        }
//#if AROS_BIG_ENDIAN
//                        destpix = (dst_blue << 24) + (dst_green << 16) + (dst_red << 8);
//#else
                        destpix = (dst_red << 16) + (dst_green << 8) + (dst_blue);
//#endif
                        /* Store the new pixel */
                        xbuf[x] = destpix;
                    }
                }

                y++;
                pasrc = (ULONG *)((IPTR)pasrc + msg->modulo);
                xbuf = (ULONG *)((IPTR)xbuf + bm->pitch);
            }
        }
        else
        {
            UWORD *xbuf = (UWORD *)(VideoData + sd->Card.Framebuffer + (msg->x << 1) + (y * bm->pitch));
            UWORD       destpix;

            while(height--)
            {
                pixarray = pasrc;
                for (x=0; x < bw; x++)
                {
                    srcpix = *pixarray++;
#if AROS_BIG_ENDIAN
                    src_red   = (srcpix & 0x00FF0000) >> 16;
                    src_green = (srcpix & 0x0000FF00) >> 8;
                    src_blue  = (srcpix & 0x000000FF);
                    src_alpha = (srcpix & 0xFF000000) >> 24;
#else
                    src_red   = (srcpix & 0x0000FF00) >> 8;
                    src_green = (srcpix & 0x00FF0000) >> 16;
                    src_blue  = (srcpix & 0xFF000000) >> 24;
                    src_alpha = (srcpix & 0x000000FF);
#endif
                    /* If Opaque, skip unnecessary reads and writes to VRAM. */
                    if (src_alpha != 0)
                    {
                        if (src_alpha == 0xff)
                        {
                            /* Fully Transparent */
                            dst_red = src_red;
                            dst_green = src_green;
                            dst_blue = src_blue;
                        }
                        else
                        {
                            /* Alpha blend */

                            destpix = xbuf[x];

                            dst_red   = (destpix & 0x0000F800) >> 8;
                            dst_green = (destpix & 0x000007e0) >> 3;
                            dst_blue  = (destpix & 0x0000001f) << 3;

                            dst_red   += do_alpha(src_alpha, src_red - dst_red);
                            dst_green += do_alpha(src_alpha, src_green - dst_green);
                            dst_blue  += do_alpha(src_alpha, src_blue - dst_blue);
                        }

                        destpix = (((dst_red << 8) & 0xf800) | ((dst_green << 3) & 0x07e0) | ((dst_blue >> 3) & 0x001f));

                        xbuf[x] = destpix;
                    }
                }
                y++;
                pasrc = (ULONG *)((IPTR)pasrc + msg->modulo);
                xbuf = (UWORD *)((IPTR)xbuf + bm->pitch);
            }
        }
    }
    else
    	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    UNLOCK_BITMAP
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutImage)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    BOOL done = FALSE;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

    IPTR VideoData = bm->framebuffer;

    /* In 32bpp mode the StdPixFmt_Native format bitmap can be drawn using the 2D engine.
     * This is done by mapping the source range via GTT into video space and issuing the blit.
     *
     * Unfortunately, AROS likes to re-use the same scratch line many times during one single bitmap
     * draw (many blits of height=1 instead of one single blit).
     * Therefore, many many the cache flushes kill the expected performance significantly. */

    if (IntelGMA_HasBLTEngine(sd->ProductID) && bm->fbgfx && bm->bpp == 4
        && (msg->pixFmt == vHidd_StdPixFmt_Native
        || msg->pixFmt == vHidd_StdPixFmt_Native32
        || msg->pixFmt == vHidd_StdPixFmt_BGRA32
        || msg->pixFmt == vHidd_StdPixFmt_BGR032))
    {
    	UBYTE *src = msg->pixels;
    	ULONG x_add = msg->modulo;
    	UWORD height = msg->height;

    	if (x_add == 0)
    		x_add = bm->pitch;

    	LOCK_HW

    	intptr_t length = x_add * height;
    	intptr_t phys = ((intptr_t)src) & 0xfffff000;
    	length += (intptr_t)src - phys;

    	length = (length + 4095) & ~4095;

    	if (length <= 16*1024*1024)
    	{
    		intptr_t virt = sd->ScratchArea + ((intptr_t)src - phys);
    		done = TRUE;

    	    //D(bug("[IntelGMA] PutImage(%d, %d, fmt=%d) with buffer at %p\n", msg->width, msg->height, msg->pixFmt,phys));

    		if (sd->AttachedMemory != phys || sd->AttachedSize != length)
    		{
    			G45_AttachCacheableMemory(sd, phys, sd->ScratchArea, length);
    			sd->AttachedMemory = phys;
    			sd->AttachedSize = length;
    		}

    		writel(0, &sd->HardwareStatusPage[16]);

    		uint32_t br00, br13, br14, br09, br11, br12;

    		br00 = (2 << 29) | (0x43 << 22) | (4);
  			br00 |= 3 << 20;

    		br13 = bm->pitch | ROP3_S;
   			br13 |= 3 << 24;

    		br14 = (msg->width * bm->bpp) | (msg->height) << 16;
    		br09 = bm->framebuffer + bm->pitch * msg->y + bm->bpp * msg->x;
    		br11 = x_add;
    		br12 = virt;

    		START_RING(12);

    		OUT_RING(br00);
    		OUT_RING(br13);
    		OUT_RING(br14);
    		OUT_RING(br09);
    		OUT_RING(br11);
    		OUT_RING(br12);

    		OUT_RING((4 << 23));
    		OUT_RING(0);

    		OUT_RING((0x21 << 23) | 1);
    		OUT_RING(16 << 2);
    		OUT_RING(1);
    		OUT_RING(0);

    		ADVANCE_RING();
    	}
    	UNLOCK_HW

    	/* Wait until HW is ready with blit and flush */
		while(readl(&sd->HardwareStatusPage[16]) == 0);
    }

    if (!done)
    {
    	D(bug("[IntelGMA] PutImage on unknown pixfmt %d\n",msg->pixFmt));

    	if (bm->fbgfx)
        {
			VideoData += (IPTR)sd->Card.Framebuffer;

			LOCK_HW
			DO_FLUSH();
			UNLOCK_HW
        }

    	switch(msg->pixFmt)
		{
			case vHidd_StdPixFmt_Native:
				switch(bm->bpp)
				{
					case 1:
					{
						struct pHidd_BitMap_CopyMemBox8 __m = {
								sd->mid_CopyMemBox8,
								msg->pixels,
								0,
								0,
								(APTR)VideoData,
								msg->x,
								msg->y,
								msg->width,
								msg->height,
								msg->modulo,
								bm->pitch
						}, *m = &__m;

						OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

				case 2:
					{
						struct pHidd_BitMap_CopyMemBox16 __m = {
								sd->mid_CopyMemBox16,
								msg->pixels,
								0,
								0,
								(APTR)VideoData,
								msg->x,
								msg->y,
								msg->width,
								msg->height,
								msg->modulo,
								bm->pitch
						}, *m = &__m;

						OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

				case 4:
					{
						struct pHidd_BitMap_CopyMemBox32 __m = {
								sd->mid_CopyMemBox32,
								msg->pixels,
								0,
								0,
								(APTR)VideoData,
								msg->x,
								msg->y,
								msg->width,
								msg->height,
								msg->modulo,
								bm->pitch
						}, *m = &__m;

						OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

					} /* switch(data->bytesperpix) */
				break;

			case vHidd_StdPixFmt_Native32:
				switch(bm->bpp)
				{
					case 1:
					{
					struct pHidd_BitMap_PutMem32Image8 __m = {
								sd->mid_PutMem32Image8,
								msg->pixels,
								(APTR)VideoData,
								msg->x,
								msg->y,
								msg->width,
								msg->height,
								msg->modulo,
								bm->pitch
						}, *m = &__m;
					OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

				case 2:
					{
					struct pHidd_BitMap_PutMem32Image16 __m = {
								sd->mid_PutMem32Image16,
								msg->pixels,
								(APTR)VideoData,
								msg->x,
								msg->y,
								msg->width,
								msg->height,
								msg->modulo,
								bm->pitch
						}, *m = &__m;
					OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

				case 4:
					{
					struct pHidd_BitMap_CopyMemBox32 __m = {
							sd->mid_CopyMemBox32,
							msg->pixels,
							0,
							0,
							(APTR)VideoData,
							msg->x,
							msg->y,
							msg->width,
							msg->height,
							msg->modulo,
							bm->pitch
					}, *m = &__m;

					OOP_DoMethod(o, (OOP_Msg)m);
					}
					break;

				} /* switch(data->bytesperpix) */
				break;

			default:
				if (IntelGMA_HasBLTEngine(sd->ProductID) && bm->bpp == 4)
				{
			    	/* Get image width aligned to 4K page boundary */
			    	uint32_t line_width = (msg->width * bm->bpp + 4095) & ~4095;
			    	void *pages = AllocVecPooled(sd->MemPool, line_width * 5);

			    	if (pages)
			    	{
                                    LOCK_HW

                                    /* Get two buffers in different GTT regions and _surely_ in different CPU cache lines */
                                    uint32_t *buffer_1 = (uint32_t *)(((intptr_t)pages + 4095) & ~4095);
                                    uint32_t *buffer_2 = &buffer_1[line_width / 4];
                                    uint32_t *buffer_3 = &buffer_2[line_width / 4];
                                    uint32_t *buffer_4 = &buffer_3[line_width / 4];

                                    uint32_t y;
                                    const uint32_t height = msg->height;
                                    uint8_t *src = msg->pixels;
                                    uint32_t x_add = msg->modulo;

                                    D(bug("[IntelGMA] Unknown PutImage(%d, %d) with buffers at %p\n", msg->width, msg->height, buffer_1));

                                    uint32_t *buffer[4] = { buffer_1, buffer_2, buffer_3, buffer_4 };
                                    intptr_t virt[4] = { sd->ScratchArea, sd->ScratchArea + line_width,
                                                                                     sd->ScratchArea + 2*line_width, sd->ScratchArea + 3*line_width  };

                                    HIDDT_PixelFormat *srcpf, *dstpf;

                                    srcpf = (HIDDT_PixelFormat *)HIDD_DMEnum_GetPixFmt(
                                        bm->dmenum, msg->pixFmt);
                                    OOP_GetAttr(o, aHidd_BitMap_PixFmt, (APTR)&dstpf);

                                    /* Attach memory, if necessary */
                                    if (sd->AttachedMemory != (intptr_t)buffer_1 || sd->AttachedSize != 4 * line_width)
                                    {
                                            G45_AttachCacheableMemory(sd, (intptr_t)buffer_1, sd->ScratchArea, 4 * line_width);
                                            sd->AttachedMemory = (intptr_t)buffer_1;
                                            sd->AttachedSize = 4 * line_width;
                                    }

                                    /* Both buffers are not busy */
                                    writel(1, &sd->HardwareStatusPage[17]);
                                    writel(1, &sd->HardwareStatusPage[18]);
                                    writel(1, &sd->HardwareStatusPage[19]);
                                    writel(1, &sd->HardwareStatusPage[20]);

                                    for (y=0; y < height; y++)
                                    {
                                        const uint8_t current = y & 3;
                                        uint32_t *dst = buffer[current];
                                        APTR _src = src;

                                        /* Wait until dst buffer is ready */
                                        while(readl(&sd->HardwareStatusPage[17 + current]) == 0);

                                        /* Convert! */
                                        HIDD_BM_ConvertPixels(o, &_src, srcpf,
                                            msg->modulo, (APTR *)&dst, dstpf,
                                            msg->modulo, msg->width, 1, NULL);

                                        /* Mark buffer as busy */
                                        writel(0, &sd->HardwareStatusPage[17 + current]);

                                        /* Prepare the Blit command */
                                        uint32_t br00, br13, br14, br09, br11, br12;

                                        br00 = (2 << 29) | (0x43 << 22) | (4);
                                        br00 |= 3 << 20;

                                        br13 = bm->pitch | ROP3_S;
                                        br13 |= 3 << 24;

                                        br14 = (msg->width * bm->bpp) | (1) << 16;
                                        br09 = bm->framebuffer + bm->pitch * (msg->y + y) + bm->bpp * msg->x;
                                        br11 = msg->width * bm->bpp;
                                        br12 = virt[current];

                                        START_RING(12);

                                        OUT_RING(br00);
                                        OUT_RING(br13);
                                        OUT_RING(br14);
                                        OUT_RING(br09);
                                        OUT_RING(br11);
                                        OUT_RING(br12);

                                        OUT_RING((4 << 23));
                                        OUT_RING(0);

                                        OUT_RING((0x21 << 23) | 1);
                                        OUT_RING((17 + current) << 2);
                                        OUT_RING(1);
                                        OUT_RING(0);

                                        ADVANCE_RING();

                                        /*
                                         * Right now the buffer is busy. The commands will flush buffer and set the proper flag (17+current) back to 1.
                                         * During that time it is fully safe to advance the loop and work on another buffer with CPU.
                                         */
                                        src += x_add;
                                    }

                                    /* Wait until both buffer are ready */
                                    while(readl(&sd->HardwareStatusPage[17]) == 0);
                                    while(readl(&sd->HardwareStatusPage[18]) == 0);
                                    while(readl(&sd->HardwareStatusPage[19]) == 0);
                                    while(readl(&sd->HardwareStatusPage[20]) == 0);

                                    UNLOCK_HW

                                    FreeVecPooled(sd->MemPool, pages);
			    	}
			    	else
                                    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
				}
				else
                                    OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
				break;
		} /* switch(msg->pixFmt) */
    }

    UNLOCK_BITMAP
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutImageLUT)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

    if (bm->fbgfx)
    {
    	/* Get image width aligned to 4K page boundary */
    	uint32_t line_width = (msg->width * bm->bpp + 4095) & ~4095;
    	void *pages = AllocVecPooled(sd->MemPool, line_width * 3);
    	HIDDT_Pixel *colmap = msg->pixlut->pixels;

    	if (pages)
    	{
    		LOCK_HW

            if (bm->bpp == 4 && IntelGMA_HasBLTEngine(sd->ProductID))
    		{
    			/* Get two buffers in different GTT regions and _surely_ in different CPU cache lines */
    			uint32_t *buffer_1 = (uint32_t *)(((intptr_t)pages + 4095) & ~4095);
    			uint32_t *buffer_2 = &buffer_1[line_width / 4];
    			uint32_t y;
    			const uint32_t height = msg->height;
    			uint8_t *src = msg->pixels;
    			uint32_t x_add = msg->modulo;

        	    D(bug("[IntelGMA] PutImageLUT(%d, %d) with buffers at %p\n", msg->width, msg->height, buffer_1));

    			uint32_t *buffer[2] = { buffer_1, buffer_2};
    			intptr_t virt[2] = { sd->ScratchArea, sd->ScratchArea + line_width };

    			/* Attach memory, if necessary */
        		if (sd->AttachedMemory != (intptr_t)buffer_1 || sd->AttachedSize != 2 * line_width)
        		{
        			G45_AttachCacheableMemory(sd, (intptr_t)buffer_1, sd->ScratchArea, 2 * line_width);
        			sd->AttachedMemory = (intptr_t)buffer_1;
        			sd->AttachedSize = 2 * line_width;
        		}

        		/* Both buffers are not busy */
        		writel(1, &sd->HardwareStatusPage[17]);
        		writel(1, &sd->HardwareStatusPage[18]);

        		for (y=0; y < height; y++)
        		{
        			const uint8_t current = y & 1;
					uint32_t *dst = buffer[current];
					uint32_t x;
					uint8_t *line = (uint8_t *)src;
					const uint32_t width = msg->width;

					/* Wait until dst buffer is ready */
					while(readl(&sd->HardwareStatusPage[17 + current]) == 0);

					/* Do LUT lookup */
					for (x=0; x < width; x++)
						dst[x] = colmap[*line++];

					/* Mark buffer as busy */
					writel(0, &sd->HardwareStatusPage[17 + current]);

					/* Prepare the Blit command */
					uint32_t br00, br13, br14, br09, br11, br12;

					br00 = (2 << 29) | (0x43 << 22) | (4);
					if (bm->bpp == 4)
						br00 |= 3 << 20;

					br13 = bm->pitch | ROP3_S;
					if (bm->bpp == 4)
						br13 |= 3 << 24;
					else if (bm->bpp == 2)
						br13 |= 1 << 24;

					br14 = (msg->width * bm->bpp) | (1) << 16;
					br09 = bm->framebuffer + bm->pitch * (msg->y + y) + bm->bpp * msg->x;
					br11 = msg->width * bm->bpp;
					br12 = virt[current];

					START_RING(12);

					OUT_RING(br00);
					OUT_RING(br13);
					OUT_RING(br14);
					OUT_RING(br09);
					OUT_RING(br11);
					OUT_RING(br12);

					OUT_RING((4 << 23));
					OUT_RING(0);

					OUT_RING((0x21 << 23) | 1);
					OUT_RING((17 + current) << 2);
					OUT_RING(1);
					OUT_RING(0);

					ADVANCE_RING();

					/*
					 * Right now the buffer is busy. The commands will flush buffer and set the proper flag (17+current) back to 1.
					 * During that time it is fully safe to advance the loop and work on another buffer with CPU.
					 */
					src += x_add;
        		}

				/* Wait until both buffer are ready */
				while(readl(&sd->HardwareStatusPage[17]) == 0);
				while(readl(&sd->HardwareStatusPage[18]) == 0);
    		}
        	else
            	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    		UNLOCK_HW

    		FreeVecPooled(sd->MemPool, pages);
    	}
    	else
        	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    }
    else
    	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

	UNLOCK_BITMAP
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, GetImage)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);
    int done = 0;
    IPTR VideoData;

    D(bug("[IntelGMA:BitMap] %s()\n", __func__));

    LOCK_BITMAP

//    bug("[IntelGMA] GetImage(%d, %d, fmt %d)\n", msg->width, msg->height, msg->pixFmt);

     if (!(VideoData = bm->framebuffer))
         return;

    if (bm->fbgfx)
    {
			LOCK_HW
			DO_FLUSH();
			UNLOCK_HW

        if (bm->bpp == 4 && IntelGMA_HasBLTEngine(sd->ProductID)
            && (msg->pixFmt == vHidd_StdPixFmt_Native
            || msg->pixFmt == vHidd_StdPixFmt_Native32))
		{
	    	UBYTE *dst = msg->pixels;
	    	ULONG x_add = msg->modulo;
	    	UWORD height = msg->height;

	    	if (x_add == 0)
	    		x_add = bm->bpp * msg->width;

	    	LOCK_HW

	    	intptr_t length = x_add * height;
	    	intptr_t phys = ((intptr_t)dst) & 0xfffff000;
	    	length += (intptr_t)dst - phys;

	    	length = (length + 4095) & ~4095;

	    	if (length <= 16*1024*1024)
	    	{
	    		intptr_t virt = sd->ScratchArea + ((intptr_t)dst - phys);
	    		done = 1;

	    	    D(bug("[IntelGMA] GetImage(%d, %d) with buffer at %p\n", msg->width, msg->height, phys));

	    		if (sd->AttachedMemory != phys || sd->AttachedSize != length)
	    		{
	    			G45_AttachCacheableMemory(sd, phys, sd->ScratchArea, length);
	    			sd->AttachedMemory = phys;
	    			sd->AttachedSize = length;
	    		}

	    		writel(0, &sd->HardwareStatusPage[16]);

	    		uint32_t br00, br13, br14, br09, br11, br12;

	    		br00 = (2 << 29) | (0x43 << 22) | (4);
	    		if (bm->bpp == 4)
	    			br00 |= 3 << 20;

	    		br13 = x_add | ROP3_S;
	    		if (bm->bpp == 4)
	    			br13 |= 3 << 24;
	    		else if (bm->bpp == 2)
	    			br13 |= 1 << 24;

	    		br14 = (x_add) | (msg->height << 16);
	    		br09 = virt;
	    		br11 = bm->pitch;
	    		br12 = bm->framebuffer + bm->pitch * msg->y + bm->bpp * msg->x;

	    		D(bug("[IntelGMA] %08x %08x %08x %08x %08x %08x\n", br00, br13, br14, br09, br11, br12));

	    		START_RING(12);

	    		OUT_RING(br00);
	    		OUT_RING(br13);
	    		OUT_RING(br14);
	    		OUT_RING(br09);
	    		OUT_RING(br11);
	    		OUT_RING(br12);

	    		OUT_RING((4 << 23));
	    		OUT_RING(0);

	    		OUT_RING((0x21 << 23) | 1);
	    		OUT_RING(16 << 2);
	    		OUT_RING(1);
	    		OUT_RING(0);

	    		ADVANCE_RING();

	    	}
	    	UNLOCK_HW

	    	/* Wait until HW is ready with blit and flush */
			while(readl(&sd->HardwareStatusPage[16]) == 0);

		}
		else
	        VideoData += (IPTR)sd->Card.Framebuffer;
    }

    if (!done)
    {
    	switch(msg->pixFmt)
    	{
    	case vHidd_StdPixFmt_Native:
    		switch(bm->bpp)
    		{
    		case 1:
    		{
    			struct pHidd_BitMap_CopyMemBox8 __m = {
    					sd->mid_CopyMemBox8,
    					(APTR)VideoData,
    					msg->x,
    					msg->y,
    					msg->pixels,
    					0,
    					0,
    					msg->width,
    					msg->height,
    					bm->pitch,
    					msg->modulo
    			}, *m = &__m;

    			OOP_DoMethod(o, (OOP_Msg)m);
    		}
    		break;

    		case 2:
    		{
    			struct pHidd_BitMap_CopyMemBox16 __m = {
    					sd->mid_CopyMemBox16,
    					(APTR)VideoData,
    					msg->x,
    					msg->y,
    					msg->pixels,
    					0,
    					0,
    					msg->width,
    					msg->height,
    					bm->pitch,
    					msg->modulo
    			}, *m = &__m;

    			OOP_DoMethod(o, (OOP_Msg)m);
    		}
    		break;

    		case 4:
    			//            	D(bug("[IntelGMA]  Native GetImage(%d, %d)\n", msg->width, msg->height));
    			{
    				struct pHidd_BitMap_CopyMemBox32 __m = {
    						sd->mid_CopyMemBox32,
    						(APTR)VideoData,
    						msg->x,
    						msg->y,
    						msg->pixels,
    						0,
    						0,
    						msg->width,
    						msg->height,
    						bm->pitch,
    						msg->modulo
    				}, *m = &__m;

    				OOP_DoMethod(o, (OOP_Msg)m);
    			}
    			break;

    		} /* switch(data->bytesperpix) */
    		break;

    		case vHidd_StdPixFmt_Native32:
    			switch(bm->bpp)
    			{
    			case 1:
    			{
    				struct pHidd_BitMap_GetMem32Image8 __m = {
    						sd->mid_GetMem32Image8,
    						(APTR)VideoData,
    						msg->x,
    						msg->y,
    						msg->pixels,
    						msg->width,
    						msg->height,
    						bm->pitch,
    						msg->modulo
    				}, *m = &__m;

    				OOP_DoMethod(o, (OOP_Msg)m);
    			}
    			break;

    			case 2:
    			{
    				struct pHidd_BitMap_GetMem32Image16 __m = {
    						sd->mid_GetMem32Image16,
    						(APTR)VideoData,
    						msg->x,
    						msg->y,
    						msg->pixels,
    						msg->width,
    						msg->height,
    						bm->pitch,
    						msg->modulo
    				}, *m = &__m;

    				OOP_DoMethod(o, (OOP_Msg)m);
    			}
    			break;

    			case 4:
    			{
    				struct pHidd_BitMap_CopyMemBox32 __m = {
    						sd->mid_CopyMemBox32,
    						(APTR)VideoData,
    						msg->x,
    						msg->y,
    						msg->pixels,
    						0,
    						0,
    						msg->width,
    						msg->height,
    						bm->pitch,
    						msg->modulo
    				}, *m = &__m;

    				OOP_DoMethod(o, (OOP_Msg)m);
    			}
    			break;

    			} /* switch(data->bytesperpix) */
    			break;

    			default:
    				OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    				break;

    	} /* switch(msg->pixFmt) */

    }

    UNLOCK_BITMAP

}


VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutTemplate)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA] NO-ACCEL: BitMap::PutTemplate\n"));

    LOCK_BITMAP

    IPTR VideoData = bm->framebuffer;
    D(bug("[IntelGMA] videoram @ 0x%p\n", VideoData));

    if (bm->fbgfx)
    {
        VideoData += (IPTR)sd->Card.Framebuffer;
			LOCK_HW
			DO_FLUSH();
			UNLOCK_HW

//        if (sd->Card.Busy)
//        {
//            LOCK_HW
//#warning TODO: NVSync(sd)
//            RADEONWaitForIdleMMIO(sd);
//            DO_FLUSH();
//UNLOCK_HW
//        }
    }


    switch(bm->bpp)
    {
        case 1:
            {
            struct pHidd_BitMap_PutMemTemplate8 __m = {
                    sd->mid_PutMemTemplate8,
                    msg->gc,
                    msg->masktemplate,
                    msg->modulo,
                    msg->srcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height,
                    msg->inverttemplate
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;

        case 2:
            {
            struct pHidd_BitMap_PutMemTemplate16 __m = {
                    sd->mid_PutMemTemplate16,
                    msg->gc,
                    msg->masktemplate,
                    msg->modulo,
                    msg->srcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height,
                    msg->inverttemplate
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;

        case 4:
            {
            struct pHidd_BitMap_PutMemTemplate32 __m = {
                    sd->mid_PutMemTemplate32,
                    msg->gc,
                    msg->masktemplate,
                    msg->modulo,
                    msg->srcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height,
                    msg->inverttemplate
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
    } /* switch(bm->bpp) */

    UNLOCK_BITMAP
    D(bug("[IntelGMA] PutTemplate: done\n"));
}

VOID METHOD(BitMapIntelGMA, Hidd_BitMap, PutPattern)
{
    GMABitMap_t *bm = OOP_INST_DATA(cl, o);

    D(bug("[IntelGMA] NO-ACCEL: BitMap::PutPattern\n"));

    LOCK_BITMAP

    IPTR VideoData = bm->framebuffer;

    if (bm->fbgfx)
    {
        VideoData += (IPTR)sd->Card.Framebuffer;
			LOCK_HW
			DO_FLUSH();
			UNLOCK_HW

//        if (sd->Card.Busy)
//        {
//            LOCK_HW
//#warning TODO: NVSync(sd)
//            RADEONWaitForIdleMMIO(sd);
            //DO_FLUSH();
//			UNLOCK_HW
//        }
    }


    switch(bm->bpp)
    {
        case 1:
            {
            struct pHidd_BitMap_PutMemPattern8 __m = {
                    sd->mid_PutMemPattern8,
                    msg->gc,
                    msg->pattern,
                    msg->patternsrcx,
                    msg->patternsrcy,
                    msg->patternheight,
                    msg->patterndepth,
                    msg->patternlut,
                    msg->invertpattern,
                    msg->mask,
                    msg->maskmodulo,
                    msg->masksrcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;

        case 2:
            {
            struct pHidd_BitMap_PutMemPattern16 __m = {
                    sd->mid_PutMemPattern16,
                    msg->gc,
                    msg->pattern,
                    msg->patternsrcx,
                    msg->patternsrcy,
                    msg->patternheight,
                    msg->patterndepth,
                    msg->patternlut,
                    msg->invertpattern,
                    msg->mask,
                    msg->maskmodulo,
                    msg->masksrcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;

        case 4:
            {
            struct pHidd_BitMap_PutMemPattern32 __m = {
                    sd->mid_PutMemPattern32,
                    msg->gc,
                    msg->pattern,
                    msg->patternsrcx,
                    msg->patternsrcy,
                    msg->patternheight,
                    msg->patterndepth,
                    msg->patternlut,
                    msg->invertpattern,
                    msg->mask,
                    msg->maskmodulo,
                    msg->masksrcx,
                    (APTR)VideoData,
                    bm->pitch,
                    msg->x,
                    msg->y,
                    msg->width,
                    msg->height
            }, *m = &__m;

            OOP_DoMethod(o, (OOP_Msg)m);
            }
            break;
    } /* switch(bm->bpp) */

    UNLOCK_BITMAP
    D(bug("[IntelGMA] PutPattern: done\n"));
}

#if defined(INTELGMA_COMPOSIT)
VOID METHOD(BitMapIntelGMA, Hidd_BitMap, UpdateRect)
{
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, o);
    D(bug("[IntelGMA] BitMap::UpdateRect %d,%d-%d,%d o=%p\n",msg->x,msg->y,msg->width,msg->height,o));
    if (bmdata->displayable)
    {
        struct pHidd_Compositor_BitMapRectChanged brcmsg =
        {
            mID : SD(cl)->mid_BitMapRectChanged,
            bm : o,
            x : msg->x,
            y : msg->y,
            width : msg->width,
            height : msg->height
        };
        OOP_DoMethod(bmdata->compositing, (OOP_Msg)&brcmsg);
    }
}
#endif

static const struct OOP_MethodDescr BitMapIntelGMA_Root_descr[] =
{
    {(OOP_MethodFunc)BitMapIntelGMA__Root__New	 , moRoot_New	 },
    {(OOP_MethodFunc)BitMapIntelGMA__Root__Dispose, moRoot_Dispose},
    {(OOP_MethodFunc)BitMapIntelGMA__Root__Get	 , moRoot_Get	 },
    {(OOP_MethodFunc)BitMapIntelGMA__Root__Set	 , moRoot_Set	 },
    {NULL				 , 0		 }
};
#define NUM_BitMapIntelGMA_Root_METHODS 4

static const struct OOP_MethodDescr BitMapIntelGMA_Hidd_BitMap_descr[] =
{
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutPixel	    , moHidd_BitMap_PutPixel	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__GetPixel	    , moHidd_BitMap_GetPixel	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__DrawPixel	    , moHidd_BitMap_DrawPixel	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__DrawLine	    , moHidd_BitMap_DrawLine	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__DrawEllipse	    , moHidd_BitMap_DrawEllipse	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__FillRect	    , moHidd_BitMap_FillRect	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__BytesPerLine	    , moHidd_BitMap_BytesPerLine       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__ObtainDirectAccess , moHidd_BitMap_ObtainDirectAccess },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__ReleaseDirectAccess, moHidd_BitMap_ReleaseDirectAccess},
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutImage           , moHidd_BitMap_PutImage	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutImageLUT	    , moHidd_BitMap_PutImageLUT	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutAlphaImage	    , moHidd_BitMap_PutAlphaImage      },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__GetImage	    , moHidd_BitMap_GetImage	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutTemplate	    , moHidd_BitMap_PutTemplate	       },
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__PutPattern	    , moHidd_BitMap_PutPattern	       },
#if defined(INTELGMA_COMPOSIT)
    {(OOP_MethodFunc)BitMapIntelGMA__Hidd_BitMap__UpdateRect	    , moHidd_BitMap_UpdateRect	       },
#endif
    {NULL					   	    , 0				       }
};
#if defined(INTELGMA_COMPOSIT)
#define NUM_BitMapIntelGMA_Hidd_BitMap_METHODS 16
#else
#define NUM_BitMapIntelGMA_Hidd_BitMap_METHODS 15
#endif

const struct OOP_InterfaceDescr BitMapIntelGMA_ifdescr[] =
{
    {BitMapIntelGMA_Root_descr       , IID_Root       , NUM_BitMapIntelGMA_Root_METHODS       },
    {BitMapIntelGMA_Hidd_BitMap_descr, IID_Hidd_BitMap, NUM_BitMapIntelGMA_Hidd_BitMap_METHODS},
    {NULL		    , NULL	     , 0			    }
};
