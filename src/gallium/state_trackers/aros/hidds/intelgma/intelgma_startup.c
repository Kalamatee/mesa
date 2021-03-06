/*
    Copyright � 2011-2015, The AROS Development Team. All rights reserved.
    $Id: startup.c 50703 2015-05-18 00:54:09Z neil $
*/

#define DEBUG 0
#include <aros/debug.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/oop.h>

#include <dos/dosextens.h>
#include <hidd/gfx.h>
#include <hidd/hidd.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <stdlib.h>
#include <string.h>

#include "intelgma_hidd.h"
#if defined(INTELGMA_COMPOSIT)
#include "intelgma_compositor.h"
#endif
#include "intelgma_gallium.h"

struct Library *OOPBase;
struct Library *UtilityBase;
struct Library *StdCBase;
struct Library *StdCIOBase;

OOP_AttrBase HiddPCIDeviceAttrBase;
OOP_AttrBase HiddBitMapIntelGMAAttrBase;
OOP_AttrBase HiddI2CAttrBase;
OOP_AttrBase HiddI2CDeviceAttrBase;
OOP_AttrBase HiddGCAttrBase;
#if defined(INTELGMA_COMPOSIT)
OOP_AttrBase HiddCompositorAttrBase;
#endif
OOP_AttrBase MetaAttrBase;
OOP_AttrBase HiddGfxAttrBase;
OOP_AttrBase HiddAttrBase;
OOP_AttrBase HiddPCIDeviceAttrBase;
OOP_AttrBase HiddBitMapIntelGMAAttrBase;
OOP_AttrBase HiddPixFmtAttrBase;
OOP_AttrBase HiddBitMapAttrBase;
OOP_AttrBase HiddColorMapAttrBase;
OOP_AttrBase HiddSyncAttrBase;
OOP_AttrBase HiddDisplayAttrBase;
OOP_AttrBase HiddDisplayIntelGMAAttrBase;
OOP_AttrBase HiddDMEnumAttrBase;
OOP_AttrBase __IHidd_PlanarBM;

/* 
 * Class static data is really static now. :)
 * If the driver would be compiled as a ROM resident, this structure 
 * needs to be allocated using AllocMem()
 */
struct g45staticdata sd;

static const struct OOP_ABDescr attrbases[] =
{
    {IID_Meta,                  &MetaAttrBase                   },
    {IID_Hidd_Gfx,              &HiddGfxAttrBase                  },
    {IID_Hidd,                  &HiddAttrBase                   },
    {IID_Hidd_PCIDevice,        &HiddPCIDeviceAttrBase          },
    {IID_Hidd_BitMap,           &HiddBitMapAttrBase             },
    {IID_Hidd_PixFmt,           &HiddPixFmtAttrBase             },
    {IID_Hidd_Sync,             &HiddSyncAttrBase               },
    {IID_Hidd_Display,          &HiddDisplayAttrBase            },
    {IID_Hidd_Display_IntelGMA, &HiddDisplayIntelGMAAttrBase },
    {IID_Hidd_DMEnum,           &HiddDMEnumAttrBase             },
    {IID_Hidd_BitMap_IntelGMA,  &HiddBitMapIntelGMAAttrBase     },
    {IID_Hidd_I2C,              &HiddI2CAttrBase                },
    {IID_Hidd_I2CDevice,        &HiddI2CDeviceAttrBase          },
    {IID_Hidd_PlanarBM,         &__IHidd_PlanarBM               },
    {IID_Hidd_GC,               &HiddGCAttrBase                 },
#if defined(INTELGMA_COMPOSIT)
    {IID_Hidd_Compositor,       &HiddCompositorAttrBase         },
#endif
    {NULL,                      NULL                            }
};

const TEXT version_string[] = "$VER: IntelGMA 42.1 (5.9.2015)\n";

extern struct WBStartup *WBenchMsg;
int __nocommandline = 1;

int main(void)
{
    BPTR olddir = BNULL;
    STRPTR myname;
    struct DiskObject *icon;
    struct RDArgs *rdargs = NULL;
    IPTR args[2] = {0};
    int ret = RETURN_FAIL;
    BOOL success = TRUE;

 D(bug("[IntelGMA] starting up ...\n"));
    /* 
     * Open libraries manually, otherwise they will be closed
     * when this subroutine exits. Driver needs them.
     */
    OOPBase = OpenLibrary("oop.library", 42);
    if (OOPBase == NULL)
        success = FALSE;

    /*
     * If our class is already registered, the user attempts to run us twice.
     * Just ignore this.
     */
    if (success)
    {
        if (OOP_FindClass(CLID_Hidd_Gfx_IntelGMA))
        {
            success = FALSE;
            ret = RETURN_OK;
        }
    }

    UtilityBase = OpenLibrary("utility.library", 36);
    StdCBase = OpenLibrary("stdc.library", 0);
    StdCIOBase = OpenLibrary("stdcio.library", 0);
    if (UtilityBase == NULL || StdCBase == NULL || StdCIOBase == NULL)
        success = FALSE;

    if (success)
    {
        memset(&sd, 0, sizeof(sd));

        /* We don't open dos.library and icon.library manually because only startup code
           needs them and these libraries can be closed even upon successful exit */
        if (WBenchMsg)
        {
            olddir = CurrentDir(WBenchMsg->sm_ArgList[0].wa_Lock);
	    myname = WBenchMsg->sm_ArgList[0].wa_Name;
        }
        else
        {
	    struct Process *me = (struct Process *)FindTask(NULL);

	    if (me->pr_CLI)
	    {
                struct CommandLineInterface *cli = BADDR(me->pr_CLI);

                myname = AROS_BSTR_ADDR(cli->cli_CommandName);
            }
            else
                myname = me->pr_Task.tc_Node.ln_Name;
        }

        icon = GetDiskObject(myname);

        if (icon)
        {
            STRPTR str;

            str = FindToolType(icon->do_ToolTypes, "FORCEGMA");
            args[0] = str ? TRUE : FALSE;

            str = FindToolType(icon->do_ToolTypes, "FORCEGALLIUM");
            args[1] = str ? TRUE : FALSE;
        }

        if (!WBenchMsg)
            rdargs = ReadArgs("FORCEGMA/S,FORCEGALLIUM/S", args, NULL);

        sd.forced  = args[0];
        sd.force_gallium  = args[1];

        if (rdargs)
            FreeArgs(rdargs);
        if (icon)
            FreeDiskObject(icon);
        if (olddir)
            CurrentDir(olddir);
    }

    /* Obtain attribute bases first */
    if (success)
    {
        D(bug("[IntelGMA] Obtaining AttrBases... \n"));
        if (!OOP_ObtainAttrBases(attrbases))
            success = FALSE;
        else
        {
            sd.basegc = OOP_FindClass(CLID_Hidd_GC);
            sd.basebm = OOP_FindClass(CLID_Hidd_BitMap);
            D(bug("[IntelGMA] base GC class @ 0x%p\n", sd.basegc));
            D(bug("[IntelGMA] base BitMap class @ 0x%p\n", sd.basebm));
            
            sd.mid_Dispose = OOP_GetMethodID((STRPTR)IID_Root, moRoot_Dispose);
            sd.mid_GetMode = OOP_GetMethodID((STRPTR)IID_Hidd_DMEnum, moHidd_DMEnum_GetMode);
        }
    }

    if (success)
    {
	struct TagItem IntelGMA_tags[] =
	{
	    {aMeta_SuperID	 , (IPTR)CLID_Hidd_Gfx         },
	    {aMeta_InterfaceDescr, (IPTR)IntelGMA_ifdescr      },
	    {aMeta_InstSize	 , sizeof(struct HiddGfxIntelGMAData)      },
	    {aMeta_ID		 , (IPTR)CLID_Hidd_Gfx_IntelGMA},
	    {TAG_DONE, 0}
	};

	/* Create classes */
	sd.IntelGMAClass = OOP_NewObject(NULL, CLID_HiddMeta, IntelGMA_tags);
	if (sd.IntelGMAClass)
	{
            struct TagItem IntelGMADisplay_tags[] =
            {
                {aMeta_SuperID	 , (IPTR)CLID_Hidd_Display         },
                {aMeta_InterfaceDescr, (IPTR)IntelGMADisplay_ifdescr      },
                {aMeta_InstSize	 , sizeof(struct HiddDisplayIntelGMAData)      },
                {aMeta_ID		 , (IPTR)CLID_Hidd_Display_IntelGMA},
                {TAG_DONE, 0}
            };
            /* According to a tradition, we store a pointer to static data in class' UserData */
            sd.IntelGMAClass->UserData = &sd;

            sd.displayclass = OOP_NewObject(NULL, CLID_HiddMeta, IntelGMADisplay_tags);
            if (sd.displayclass)
            {
                struct TagItem BitMapIntelGMA_tags[] =
                {
                    {aMeta_SuperID       , (IPTR)CLID_Hidd_BitMap},
                    {aMeta_InterfaceDescr, (IPTR)BitMapIntelGMA_ifdescr   },
                    {aMeta_InstSize      , sizeof(struct HiddBitMapIntelGMAData)   },
                    {aMeta_ID		 , (IPTR)CLID_Hidd_BitMap_IntelGMA},
                    {TAG_DONE, 0}
                };
                sd.displayclass->UserData = &sd;
                
                sd.BMClass = OOP_NewObject(NULL, CLID_HiddMeta, BitMapIntelGMA_tags);
                if (sd.BMClass)
                {
                    struct TagItem INTELI2C_tags[] =
                    {
                        {aMeta_SuperID       , (IPTR)CLID_Hidd_I2C   },
                        {aMeta_InterfaceDescr, (IPTR)INTELI2C_ifdescr},
                        {aMeta_InstSize      , sizeof(struct HiddI2CIntelGMAData)},
                        {TAG_DONE, 0}
                    };

                    sd.BMClass->UserData = &sd;

                    sd.IntelI2C = OOP_NewObject(NULL, CLID_HiddMeta, INTELI2C_tags);
                    if (sd.IntelI2C)
                    {
#if defined(INTELGMA_COMPOSIT)
                        struct TagItem Compositor_tags[] =
                        {
                            {aMeta_SuperID       , (IPTR)CLID_Hidd},
                            {aMeta_InterfaceDescr, (IPTR)Compositor_ifdescr},
                            {aMeta_InstSize      , sizeof(struct HIDDCompositorData)},
                            {TAG_DONE, 0}
                        };
#endif
                        sd.IntelI2C->UserData = &sd;

#if defined(INTELGMA_COMPOSIT)
                        sd.compositingclass = OOP_NewObject(NULL, CLID_HiddMeta, Compositor_tags);
                        if (sd.compositingclass)
                        {
#endif

#ifndef GALLIUM_SIMULATION
                            /* Init internal stuff */
                            if (G45_Init(&sd))
#endif
                            {
                                struct Process *me = (struct Process *)FindTask(NULL);

#ifndef GALLIUM_SIMULATION
                                /* 
                                 * Register our gfx class as public, we use it as a
                                 * protection against double start
                                 */
                                OOP_AddClass(sd.IntelGMAClass);
#endif

                                /* Init Galliumclass */
                                InitGalliumClass();

                                /* Everything is okay, stay resident and exit */

                                D(bug("[IntelGMA] Staying resident, process 0x%p\n", me));
                                if (me->pr_CLI)
                                {
                                    struct CommandLineInterface *cli = BADDR(me->pr_CLI);

                                    cli->cli_Module = BNULL;
                                }
                                else  
                                    me->pr_SegList = BNULL;

                                /* 
                                 * Note also that we don't close needed libraries and
                                 * don't free attribute bases
                                 */
                                return RETURN_OK;
                            }
#if defined(INTELGMA_COMPOSIT)
                            OOP_DisposeObject((OOP_Object *)sd.compositingclass);
                        }
#endif
                        OOP_DisposeObject((OOP_Object *)sd.IntelI2C);
                    }
                    OOP_DisposeObject((OOP_Object *)sd.BMClass);
                }
                OOP_DisposeObject((OOP_Object *)sd.displayclass);
            }
	    OOP_DisposeObject((OOP_Object *)sd.IntelGMAClass);
	}
	OOP_ReleaseAttrBases(attrbases);
    }

    if (StdCIOBase != NULL)
        CloseLibrary(StdCIOBase);
    if (StdCBase != NULL)
        CloseLibrary(StdCBase);
    if (UtilityBase != NULL)
        CloseLibrary(UtilityBase);
    if (OOPBase != NULL)
        CloseLibrary(OOPBase);

    return ret;
}
