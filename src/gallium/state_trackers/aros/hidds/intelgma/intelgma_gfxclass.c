/*
    Copyright © 1995-2015, The AROS Development Team. All rights reserved.
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
#include <hidd/graphics.h>
#if defined(INTELGMA_COMPOSIT)
#include <hidd/compositor.h>
#endif
#include <hidd/gallium.h>
#include <hidd/i2c.h>
#include <graphics/displayinfo.h>

#include <stdio.h>
#include <stdint.h>

#include "intelgma_gfx.h"
#include "intelgma_gallium.h"
#include "intelG45_regs.h"

#include "edid/display.h"

#define sd ((struct g45staticdata*)SD(cl))

#define SYNC_TAG_COUNT 16
#define SYNC_LIST_COUNT (15 + 8 + 4)

#define MAKE_SYNC(name,clock,hdisp,hstart,hend,htotal,vdisp,vstart,vend,vtotal,descr)	\
    struct TagItem sync_ ## name[]={ \
        { aHidd_Sync_PixelClock,  clock*1000 }, \
        { aHidd_Sync_HDisp,       hdisp }, \
        { aHidd_Sync_HSyncStart,  hstart }, \
        { aHidd_Sync_HSyncEnd,    hend }, \
        { aHidd_Sync_HTotal,      htotal }, \
        { aHidd_Sync_VDisp,       vdisp }, \
        { aHidd_Sync_VSyncStart,  vstart }, \
        { aHidd_Sync_VSyncEnd,    vend }, \
        { aHidd_Sync_VTotal,      vtotal }, \
        { aHidd_Sync_Description, (IPTR)descr }, \
        { TAG_DONE, 0UL }}

static VOID IntelGMA_DDCParse(OOP_Class *cl, struct TagItem **tagsptr,
    struct TagItem *poolptr, OOP_Object *obj)
{
    struct pHidd_I2CDevice_WriteRead msg;
    uint8_t edid[128];
    char wb[2] = {0, 0};

    D(bug("[IntelGMA] Trying to parse the DDC data\n"));

    msg.mID = OOP_GetMethodID((STRPTR)IID_Hidd_I2CDevice, moHidd_I2CDevice_WriteRead);
    msg.readBuffer = &edid[0];
    msg.readLength = 128;
    msg.writeBuffer = &wb[0];
    msg.writeLength = 1;

    OOP_DoMethod(obj, &msg.mID);

    EDIDParse(sd->MemPool, edid, tagsptr, poolptr);
}

OOP_Object *METHOD(IntelGMA, Root, New)
{
    struct TagItem pftags_24bpp[] = {
        { aHidd_PixFmt_RedShift,    8   }, /* 0 */
        { aHidd_PixFmt_GreenShift,  16  }, /* 1 */
        { aHidd_PixFmt_BlueShift,   24  }, /* 2 */
        { aHidd_PixFmt_AlphaShift,  0   }, /* 3 */
        { aHidd_PixFmt_RedMask,     0x00ff0000 }, /* 4 */
        { aHidd_PixFmt_GreenMask,   0x0000ff00 }, /* 5 */
        { aHidd_PixFmt_BlueMask,    0x000000ff }, /* 6 */
        { aHidd_PixFmt_AlphaMask,   0x00000000 }, /* 7 */
        { aHidd_PixFmt_ColorModel,  vHidd_ColorModel_TrueColor }, /* 8 */
        { aHidd_PixFmt_Depth,       24  }, /* 9 */
        { aHidd_PixFmt_BytesPerPixel,   4   }, /* 10 */
        { aHidd_PixFmt_BitsPerPixel,    24  }, /* 11 */
        { aHidd_PixFmt_StdPixFmt,   vHidd_StdPixFmt_BGR032 }, /* 12 Native */
        { aHidd_PixFmt_BitMapType,  vHidd_BitMapType_Chunky }, /* 15 */
        { TAG_DONE, 0UL }
    };

    struct TagItem pftags_16bpp[] = {
        { aHidd_PixFmt_RedShift,    16  }, /* 0 */
        { aHidd_PixFmt_GreenShift,  21  }, /* 1 */
        { aHidd_PixFmt_BlueShift,   27  }, /* 2 */
        { aHidd_PixFmt_AlphaShift,  0   }, /* 3 */
        { aHidd_PixFmt_RedMask,     0x0000f800 }, /* 4 */
        { aHidd_PixFmt_GreenMask,   0x000007e0 }, /* 5 */
        { aHidd_PixFmt_BlueMask,    0x0000001f }, /* 6 */
        { aHidd_PixFmt_AlphaMask,   0x00000000 }, /* 7 */
        { aHidd_PixFmt_ColorModel,  vHidd_ColorModel_TrueColor }, /* 8 */
        { aHidd_PixFmt_Depth,       16  }, /* 9 */
        { aHidd_PixFmt_BytesPerPixel,   2   }, /* 10 */
        { aHidd_PixFmt_BitsPerPixel,    16  }, /* 11 */
        { aHidd_PixFmt_StdPixFmt,   vHidd_StdPixFmt_RGB16_LE }, /* 12 */
        { aHidd_PixFmt_BitMapType,  vHidd_BitMapType_Chunky }, /* 15 */
        { TAG_DONE, 0UL }
    };

#if defined(INTELGMA_15BIT)
    struct TagItem pftags_15bpp[] = {
        { aHidd_PixFmt_RedShift,    17  }, /* 0 */
        { aHidd_PixFmt_GreenShift,  22  }, /* 1 */
        { aHidd_PixFmt_BlueShift,   27  }, /* 2 */
        { aHidd_PixFmt_AlphaShift,  0   }, /* 3 */
        { aHidd_PixFmt_RedMask,     0x00007c00 }, /* 4 */
        { aHidd_PixFmt_GreenMask,   0x000003e0 }, /* 5 */
        { aHidd_PixFmt_BlueMask,    0x0000001f }, /* 6 */
        { aHidd_PixFmt_AlphaMask,   0x00000000 }, /* 7 */
        { aHidd_PixFmt_ColorModel,  vHidd_ColorModel_TrueColor }, /* 8 */
        { aHidd_PixFmt_Depth,       15  }, /* 9 */
        { aHidd_PixFmt_BytesPerPixel,   2   }, /* 10 */
        { aHidd_PixFmt_BitsPerPixel,    15  }, /* 11 */
        { aHidd_PixFmt_StdPixFmt,   vHidd_StdPixFmt_RGB15_LE }, /* 12 */
        { aHidd_PixFmt_BitMapType,  vHidd_BitMapType_Chunky }, /* 15 */
        { TAG_DONE, 0UL }
    };
#endif

    struct TagItem i2c_attrs[] = {
#if (0)
        { aHidd_I2C_HoldTime,   	40 },
        { aHidd_I2C_RiseFallTime,   40 },
#endif
        { TAG_DONE, 0UL }
    };

    struct TagItem gfxTags[] = {
        { aHidd_Gfx_ModeTags,   0  },
        { TAG_MORE, (IPTR)msg->attrList }
    };

    struct pRoot_New newGfxMsg;
    struct TagItem *modetags, *tags;
    struct TagItem *poolptr;
    OOP_Object *i2c;

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    modetags = tags = AllocVecPooled(sd->MemPool,
        sizeof (struct TagItem) * (3 + SYNC_LIST_COUNT + 1));
    poolptr = AllocVecPooled(sd->MemPool,
        sizeof(struct TagItem) * SYNC_TAG_COUNT * SYNC_LIST_COUNT);

    tags->ti_Tag = aHidd_Gfx_PixFmtTags;
    tags->ti_Data = (IPTR)pftags_24bpp;
    tags++;

    tags->ti_Tag = aHidd_Gfx_PixFmtTags;
    tags->ti_Data = (IPTR)pftags_16bpp;
    tags++;

#if defined(INTELGMA_15BIT)
    tags->ti_Tag = aHidd_Gfx_PixFmtTags;
    tags->ti_Data = (IPTR)pftags_15bpp;
    tags++;
#endif

    if (sd->pipe == PIPE_B)
    {
        char *description = AllocVecPooled(sd->MemPool, MAX_MODE_NAME_LEN + 1);
        snprintf(description, MAX_MODE_NAME_LEN, "GMA_LVDS:%dx%d",
                sd->lvds_fixed.hdisp, sd->lvds_fixed.vdisp);

        //native lcd mode
        struct TagItem sync_native[]={
        { aHidd_Sync_PixelClock,        sd->lvds_fixed.pixelclock*1000000 },
        { aHidd_Sync_HDisp,             sd->lvds_fixed.hdisp },
        { aHidd_Sync_HSyncStart,        sd->lvds_fixed.hstart },
        { aHidd_Sync_HSyncEnd,          sd->lvds_fixed.hend },
        { aHidd_Sync_HTotal,            sd->lvds_fixed.htotal },
        { aHidd_Sync_VDisp,             sd->lvds_fixed.vdisp },
        { aHidd_Sync_VSyncStart,        sd->lvds_fixed.vstart },
        { aHidd_Sync_VSyncEnd,          sd->lvds_fixed.vend },
        { aHidd_Sync_VTotal,            sd->lvds_fixed.vtotal },
        { aHidd_Sync_VMin,              sd->lvds_fixed.vdisp},
        { aHidd_Sync_VMax,              sd->lvds_fixed.vtotal},
        { aHidd_Sync_HMin,              sd->lvds_fixed.hdisp},
        { aHidd_Sync_HMax,              sd->lvds_fixed.htotal},
        { aHidd_Sync_Description, (IPTR)description },
        { TAG_DONE, 0UL }};

        MAKE_SYNC(640x480_60,   25174,
            640,  656,  752,  800,
            480,  490,  492,  525,
            "GMA_LVDS:640x480");

        tags->ti_Tag =  aHidd_Gfx_SyncTags;
        tags->ti_Data = (IPTR)sync_640x480_60;
        tags++;

        tags->ti_Tag =  aHidd_Gfx_SyncTags;
        tags->ti_Data = (IPTR)sync_native;
        tags++;
    }
    else
    {
        i2c = OOP_NewObject(sd->IntelI2C, NULL, i2c_attrs);

        if (i2c)
        {
            if (HIDD_I2C_ProbeAddress(i2c, 0xa0))
            {
                struct TagItem attrs[] = {
                    { aHidd_I2CDevice_Driver,   (IPTR)i2c       },
                    { aHidd_I2CDevice_Address,  0xa0            },
                    { aHidd_I2CDevice_Name,     (IPTR)"Display" },
                    { TAG_DONE, 0UL }
                };

                D(bug("[IntelGMA] I2C device found\n"));

                OOP_Object *obj = OOP_NewObject(NULL, CLID_Hidd_I2CDevice, attrs);

                if (obj)
                {
                    IntelGMA_DDCParse(cl, &tags, poolptr, obj);

                    OOP_DisposeObject(obj);
                }
            }
            OOP_DisposeObject(i2c);
        }
    }

    tags->ti_Tag = TAG_DONE;
    tags->ti_Data = 0;

    gfxTags[0].ti_Data = (IPTR)modetags;

    newGfxMsg.mID = msg->mID;
    newGfxMsg.attrList = gfxTags;

    o = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)&newGfxMsg);
    {
        struct HiddGfxIntelGMAData *data = OOP_INST_DATA(cl, o);

#if defined(INTELGMA_COMPOSIT)
        /* Create compositor object */
        {
            struct TagItem comptags [] =
            {
                { aHidd_Compositor_GfxHidd, (IPTR)o },
                { TAG_DONE, TAG_DONE }
            };
            sd->compositor = OOP_NewObject(sd->compositorclass, NULL, comptags);
            /* TODO: Check if object was created, how to handle ? */
        }
#endif
    }

    FreeVecPooled(sd->MemPool, modetags);
    FreeVecPooled(sd->MemPool, poolptr);

    D(bug("[IntelGMA] IntelGMA::New() = %p\n", o));

    return o;
}

void METHOD(IntelGMA, Root, Get)
{
    uint32_t idx;
    BOOL found = FALSE;
    if (IS_GFX_ATTR(msg->attrID, idx))
    {
    	switch (idx)
    	{
#if (0)
            case aoHidd_Gfx_MemorySize:
                *msg->storage = (IPTR)0;
                found = TRUE;
                break;
#endif

    	case aoHidd_Gfx_SupportsHWCursor:
    		*msg->storage = (IPTR)TRUE;
    		found = TRUE;
    		break;

#if defined(INTELGMA_COMPOSIT)
        case aoHidd_Gfx_NoFrameBuffer:
            *msg->storage = (IPTR)TRUE;
            found = TRUE;
            break;
#endif

        case aoHidd_Gfx_HWSpriteTypes:
            *msg->storage = vHidd_SpriteType_DirectColor;
            found = TRUE;
            return;

    	case aoHidd_Gfx_DPMSLevel:
    		*msg->storage = SD(cl)->dpms;
    		found = TRUE;
    		break;
    	}
    }

    if (!found)
    	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    return;
}

void METHOD(IntelGMA, Root, Set)
{
    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));;

	ULONG idx;
	struct TagItem *tag;
	struct TagItem *tags = msg->attrList;

	while ((tag = NextTagItem(&tags)))
	{
		if (IS_GFX_ATTR(tag->ti_Tag, idx))
		{
			switch(idx)
			{
			case aoHidd_Gfx_DPMSLevel:
				LOCK_HW
				uint32_t adpa = readl(sd->Card.MMIO + G45_ADPA) & ~G45_ADPA_DPMS_MASK;
				switch (tag->ti_Data)
				{
				case vHidd_Gfx_DPMSLevel_On:
					adpa |= G45_ADPA_DPMS_ON;
					break;
				case vHidd_Gfx_DPMSLevel_Off:
					adpa |= G45_ADPA_DPMS_OFF;
					break;
				case vHidd_Gfx_DPMSLevel_Standby:
					adpa |= G45_ADPA_DPMS_STANDBY;
					break;
				case vHidd_Gfx_DPMSLevel_Suspend:
					adpa |= G45_ADPA_DPMS_SUSPEND;
					break;
				}
				writel(adpa, sd->Card.MMIO + G45_ADPA);
				sd->dpms = tag->ti_Data;

				UNLOCK_HW
				break;
			}
		}
	}

	OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
}

VOID METHOD(IntelGMA, Hidd_Gfx, NominalDimensions)
{
    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    if (msg->width)
        *(msg->width) = 1360;
    if (msg->height)
        *(msg->height) = 768;
    if (msg->depth)
        *(msg->depth) = 24;
}

OOP_Object * METHOD(IntelGMA, Hidd_Gfx, CreateObject)
{
    struct HiddGfxIntelGMAData *data = OOP_INST_DATA(cl, o);
    OOP_Object      *object = NULL;

    D(bug("[IntelGMA] %s(0x%p)\n", __PRETTY_FUNCTION__, msg->cl));

    if (msg->cl == SD(cl)->basebm)
    {
        struct pHidd_Gfx_CreateObject coMsg;
        HIDDT_ModeID modeid;
        HIDDT_StdPixFmt stdpf;

        struct TagItem coTags [] =
        {
            { TAG_IGNORE, TAG_IGNORE }, /* Placeholder for aHidd_BitMap_ClassPtr */
            { TAG_IGNORE, TAG_IGNORE }, /* Placeholder for aHidd_BitMap_Align */
#if defined(INTELGMA_COMPOSIT)
            { aHidd_BitMap_IntelGMA_CompositorHidd, (IPTR)sd->compositor },
#endif
            { TAG_MORE, (IPTR)msg->attrList }
        };

        /* Check if user provided valid ModeID */
        /* Check for framebuffer - not needed as IntelGMA is a NoFramebuffer driver */
        /* Check for displayable - not needed - displayable has ModeID and we don't
           distinguish between on-screen and off-screen bitmaps */
        modeid = (HIDDT_ModeID)GetTagData(aHidd_BitMap_ModeID, vHidd_ModeID_Invalid, msg->attrList);
        if (vHidd_ModeID_Invalid != modeid) 
        {
            /* User supplied a valid modeid. We can use our bitmap class */
            coTags[0].ti_Tag	= aHidd_BitMap_ClassPtr;
            coTags[0].ti_Data	= (IPTR)SD(cl)->BMClass;
        } 

        /* Check if bitmap is a planar bitmap */
        stdpf = (HIDDT_StdPixFmt)GetTagData(aHidd_BitMap_StdPixFmt, vHidd_StdPixFmt_Unknown, msg->attrList);
        if (vHidd_StdPixFmt_Plane == stdpf)
        {
            coTags[1].ti_Tag    = aHidd_BitMap_Align;
            coTags[1].ti_Data   = 32;
        }

        /* We init a new message struct */
        coMsg.mID       = msg->mID;
        coMsg.cl	= msg->cl;
        coMsg.attrList	= coTags;

        /* Pass the new message to the superclass */
        object = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)&coMsg);
    }
    else if (SD(cl)->basegallium && (msg->cl == SD(cl)->basegallium))
    {
        object = OOP_NewObject(NULL, CLID_Hidd_Gallium_IntelGMA, msg->attrList);
    }
    else
        object = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);

    return object;
}

void METHOD(IntelGMA, Hidd_Gfx, SetCursorVisible)
{
    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    sd->CursorVisible = msg->visible;
    if (msg->visible)
    {
        writel( (sd->pipe == PIPE_A ? G45_CURCNTR_PIPE_A : G45_CURCNTR_PIPE_B ) | G45_CURCNTR_TYPE_ARGB ,
            sd->Card.MMIO +  (sd->pipe == PIPE_A ? G45_CURACNTR:G45_CURBCNTR));
    }
    else
    {
        writel( (sd->pipe == PIPE_A ? G45_CURCNTR_PIPE_A : G45_CURCNTR_PIPE_B ) | G45_CURCNTR_TYPE_OFF ,
            sd->Card.MMIO +  (sd->pipe == PIPE_A ? G45_CURACNTR:G45_CURBCNTR));
    }
    UpdateCursor(sd);
}

void METHOD(IntelGMA, Hidd_Gfx, SetCursorPos)
{
    SetCursorPosition(sd,msg->x,msg->y);
    sd->pointerx = msg->x;
    sd->pointery = msg->y;
}

BOOL METHOD(IntelGMA, Hidd_Gfx, SetCursorShape)
{
    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    if (msg->shape == NULL)
    {
        sd->CursorVisible = 0;
        writel( (sd->pipe == PIPE_A ? G45_CURCNTR_PIPE_A : G45_CURCNTR_PIPE_B ) | G45_CURCNTR_TYPE_OFF ,
            sd->Card.MMIO +  (sd->pipe == PIPE_A ? G45_CURACNTR:G45_CURBCNTR));
    }
    else
    {
        IPTR       width, height, x;

        ULONG       *curimg = (ULONG*)((IPTR)sd->CursorImage + (IPTR)sd->Card.Framebuffer);

        OOP_GetAttr(msg->shape, aHidd_BitMap_Width, &width);
        OOP_GetAttr(msg->shape, aHidd_BitMap_Height, &height);

        if (width > 64) width = 64;
        if (height > 64) height = 64;

        for (x = 0; x < 64*64; x++)
           curimg[x] = 0;

        HIDD_BM_GetImage(msg->shape, (UBYTE *)curimg, 64*4, 0, 0, width, height, vHidd_StdPixFmt_BGRA32);
        writel( (sd->pipe == PIPE_A ? G45_CURCNTR_PIPE_A : G45_CURCNTR_PIPE_B ) | G45_CURCNTR_TYPE_ARGB ,
            sd->Card.MMIO +  (sd->pipe == PIPE_A ? G45_CURACNTR:G45_CURBCNTR));
    }
    UpdateCursor(sd);

    return TRUE;
}

void METHOD(IntelGMA, Hidd_Gfx, CopyBox)
{
    ULONG mode = GC_DRMD(msg->gc);
    IPTR src=0, dst=0;

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    /* Check whether we can get Drawable attribute of our GMA class */
    OOP_GetAttr(msg->src,   aHidd_BitMap_IntelGMA_Drawable,   &src);
    OOP_GetAttr(msg->dest,  aHidd_BitMap_IntelGMA_Drawable,   &dst);

    if (!dst || !src)
    {
        /* No. One of the bitmaps is not a GMA bitmap */
        OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    }
    else
    {
        /* Yes. Get the instance data of both bitmaps */
        GMABitMap_t *bm_src = OOP_INST_DATA(OOP_OCLASS(msg->src), msg->src);
        GMABitMap_t *bm_dst = OOP_INST_DATA(OOP_OCLASS(msg->dest), msg->dest);

        D(bug("[IntelGMA] %s: src(%p,%d:%d@%d), dst(%p,%d:%d@%d), %d:%d\n",
                    __PRETTY_FUNCTION__,
                    bm_src->framebuffer,msg->srcX,msg->srcY,bm_src->depth,
                    bm_dst->framebuffer,msg->destX,msg->destY,bm_dst->depth,
                    msg->width, msg->height));

        /* Case -1: (To be fixed) one of the bitmaps have chunky outside GFX mem */
        if (!bm_src->fbgfx || !bm_dst->fbgfx)
        {
            D(bug("[IntelGMA] %s: one of bitmaps outside VRAM!\n", __PRETTY_FUNCTION__));

            OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
        }
        /* Case 0: one of bitmaps is 8bpp, whereas the other is TrueColor one */
        else if ((bm_src->depth <= 8 || bm_dst->depth <= 8) &&
            (bm_src->depth != bm_dst->depth))
        {
            /* Unsupported case */
            OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
            return;
        }
        else
        {
            BOOL done = FALSE;

            LOCK_MULTI_BITMAP
            LOCK_BITMAP_BM(bm_src)
            LOCK_BITMAP_BM(bm_dst)
            UNLOCK_MULTI_BITMAP

#if (0)
            /* First, just try to use 3d hardware. */
            D(bug("[IntelGMA] CopyBox(src(%p,%d:%d@%d),dst(%p,%d:%d@%d),%d:%d\n",
                    bm_src->framebuffer,msg->srcX,msg->srcY,bm_src->depth,
                    bm_dst->framebuffer,msg->destX,msg->destY,bm_dst->depth,
                    msg->width, msg->height));

            done = copybox3d( bm_dst, bm_src,
                           msg->destX, msg->destY, msg->width, msg->height,
                           msg->srcX, msg->srcY, msg->width, msg->height );
#endif
            /*
                   handle the case where copy3d failed, but both bitmaps have the same depth
                   - use Blit engine
                  */
            if ((!done) && (bm_src->depth == bm_dst->depth))
            {
                LOCK_HW

                uint32_t br00, br13, br22, br23, br09, br11, br26, br12;

                br00 = (2 << 29) | (0x53 << 22) | (6);
                if (bm_dst->bpp == 4)
                    br00 |= 3 << 20;

                br13 = bm_dst->pitch | ROP_table[mode].rop;
                if (bm_dst->bpp == 4)
                    br13 |= 3 << 24;
                else if (bm_dst->bpp == 2)
                    br13 |= 1 << 24;

                br22 = msg->destX | (msg->destY << 16);
                br23 = (msg->destX + msg->width) | (msg->destY + msg->height) << 16;
                br09 = bm_dst->framebuffer;
                br11 = bm_src->pitch;
                br26 = msg->srcX | (msg->srcY << 16);
                br12 = bm_src->framebuffer;

                START_RING(8);

                OUT_RING(br00);
                OUT_RING(br13);
                OUT_RING(br22);
                OUT_RING(br23);
                OUT_RING(br09);
                OUT_RING(br26);
                OUT_RING(br11);
                OUT_RING(br12);

                ADVANCE_RING();

                DO_FLUSH();
                UNLOCK_HW
            }

            UNLOCK_BITMAP_BM(bm_src)
            UNLOCK_BITMAP_BM(bm_dst)

            if( ! done ) OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
        }
    }
}

ULONG METHOD(IntelGMA, Hidd_Gfx, ShowViewPorts)
{
#if defined(INTELGMA_COMPOSIT)

    struct pHidd_Compositor_BitMapStackChanged bscmsg =
    {
        mID : OOP_GetMethodID(IID_Hidd_Compositor, moHidd_Compositor_BitMapStackChanged),
        data : msg->Data
    };

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    D(bug("[IntelGMA] ShowViewPorts enter TopLevelBM %x\n", msg->Data->Bitmap));

    OOP_DoMethod(sd->compositor, (OOP_Msg)&bscmsg);

    return TRUE; /* Indicate driver supports this method */
#else
    return FALSE;
#endif
}

BOOL HIDD_IntelGMA_SwitchToVideoMode(OOP_Object * bm)
{
    OOP_Class * cl = OOP_OCLASS(bm);
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, bm);
    OOP_Object * gfx = NULL;
    HIDDT_ModeID modeid;
    OOP_Object * sync;
    OOP_Object * pf;
    IPTR pixel, e;
    IPTR hdisp, vdisp, hstart, hend, htotal, vstart, vend, vtotal;

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    OOP_GetAttr(bm, aHidd_BitMap_GfxHidd, &e);
    gfx = (OOP_Object *)e;

    D(bug("[IntelGMA] HIDD_IntelGMA_SwitchToVideoMode bitmap:%x\n",bmdata));

    /* We should be able to get modeID from the bitmap */
    OOP_GetAttr(bm, aHidd_BitMap_ModeID, &modeid);

    if (modeid == vHidd_ModeID_Invalid)
    {
        D(bug("[IntelGMA] Invalid ModeID\n"));
        return FALSE;
    }

    /* Get Sync and PixelFormat properties */
    struct pHidd_Gfx_GetMode __getmodemsg = 
    {
        modeID:	modeid,
        syncPtr:	&sync,
        pixFmtPtr:	&pf,
    }, *getmodemsg = &__getmodemsg;

    getmodemsg->mID = OOP_GetMethodID(IID_Hidd_Gfx, moHidd_Gfx_GetMode);
    OOP_DoMethod(gfx, (OOP_Msg)getmodemsg);

    OOP_GetAttr(sync, aHidd_Sync_PixelClock,    &pixel);
    OOP_GetAttr(sync, aHidd_Sync_HDisp,         &hdisp);
    OOP_GetAttr(sync, aHidd_Sync_VDisp,         &vdisp);
    OOP_GetAttr(sync, aHidd_Sync_HSyncStart,    &hstart);
    OOP_GetAttr(sync, aHidd_Sync_VSyncStart,    &vstart);
    OOP_GetAttr(sync, aHidd_Sync_HSyncEnd,      &hend);
    OOP_GetAttr(sync, aHidd_Sync_VSyncEnd,      &vend);
    OOP_GetAttr(sync, aHidd_Sync_HTotal,        &htotal);
    OOP_GetAttr(sync, aHidd_Sync_VTotal,        &vtotal);    

    D(bug("[IntelGMA] Sync: %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
    pixel, hdisp, hstart, hend, htotal, vdisp, vstart, vend, vtotal));

    if (bmdata->state && sd->VisibleBitmap != bmdata)
    {
        /* Suppose bm has properly allocated state structure */
        if (bmdata->fbgfx)
        {
            bmdata->usecount++;
            SetCursorPosition(sd,0,0);
            LOCK_HW
            {
                sd->VisibleBitmap = bmdata;
                G45_LoadState(sd, bmdata->state);
            }
            UNLOCK_HW
            SetCursorPosition(sd,sd->pointerx,sd->pointery);
            return TRUE;
        }
    }
    
    return FALSE;     
}

BOOL HIDD_IntelGMA_SetFramebuffer(OOP_Object * bm)
{
    OOP_Class * cl = OOP_OCLASS(bm);
    GMABitMap_t * bmdata = OOP_INST_DATA(cl, bm);

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    //bug("[IntelGMA] HIDD_IntelGMA_SetFramebuffer %x %d,%d\n",bmdata,bmdata->xoffset,bmdata->yoffset);
    if (bmdata->fbgfx)
    {
        char *linoff_reg = sd->Card.MMIO + ((sd->pipe == PIPE_A) ? G45_DSPALINOFF : G45_DSPBLINOFF);
        char *stride_reg = sd->Card.MMIO + ((sd->pipe == PIPE_A) ? G45_DSPASTRIDE : G45_DSPBSTRIDE);
        
        // bitmap width in bytes
        writel( bmdata->state->dspstride , stride_reg );
        
        // framebuffer address + possible xy offset  
        writel(	bmdata->framebuffer - ( bmdata->yoffset * bmdata->pitch +
                                                                        bmdata->xoffset * bmdata->bpp ) ,linoff_reg );
        readl( linoff_reg );	
        return TRUE;
    }
    //bug("[IntelGMA] HIDD_IntelGMA_SetFramebuffer: not Framebuffer Bitmap!\n");
    return FALSE;
}


static struct HIDD_ModeProperties modeprops = 
{
    DIPF_IS_SPRITES,
    1,
#if defined(INTELGMA_COMPOSIT)
    COMPF_ABOVE
#else
    0
#endif
};

ULONG METHOD(IntelGMA, Hidd_Gfx, ModeProperties)
{
    ULONG len = msg->propsLen;

    D(bug("[IntelGMA] %s()\n", __PRETTY_FUNCTION__));

    if (len > sizeof(modeprops))
        len = sizeof(modeprops);
    CopyMem(&modeprops, msg->props, len);

    return len;
}

static const struct OOP_MethodDescr IntelGMA_Root_descr[] =
{
    {(OOP_MethodFunc)IntelGMA__Root__New,                       moRoot_New                      },
    {(OOP_MethodFunc)IntelGMA__Root__Get,                       moRoot_Get                      },
    {(OOP_MethodFunc)IntelGMA__Root__Set,                       moRoot_Set                      },
    {NULL,                                                      0                               }
};
#define NUM_IntelGMA_Root_METHODS 3

static const struct OOP_MethodDescr IntelGMA_Hidd_Gfx_descr[] =
{
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__NominalDimensions,     moHidd_Gfx_NominalDimensions    },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__CreateObject,          moHidd_Gfx_CreateObject         },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__CopyBox,               moHidd_Gfx_CopyBox              },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__SetCursorVisible,      moHidd_Gfx_SetCursorVisible     },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__SetCursorPos,          moHidd_Gfx_SetCursorPos         },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__SetCursorShape,        moHidd_Gfx_SetCursorShape       },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__ShowViewPorts,         moHidd_Gfx_ShowViewPorts        },
    {(OOP_MethodFunc)IntelGMA__Hidd_Gfx__ModeProperties,        moHidd_Gfx_ModeProperties       },
    {NULL,                                                      0                               }
};
#define NUM_IntelGMA_Hidd_Gfx_METHODS 8

const struct OOP_InterfaceDescr IntelGMA_ifdescr[] =
{
    {IntelGMA_Root_descr,               IID_Root,               NUM_IntelGMA_Root_METHODS       },
    {IntelGMA_Hidd_Gfx_descr,           IID_Hidd_Gfx,           NUM_IntelGMA_Hidd_Gfx_METHODS   },
    {NULL,                              NULL,                   0				}
};
