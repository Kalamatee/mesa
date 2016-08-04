/*
    Copyright © 2015, The AROS Development Team. All rights reserved.
    $Id$
*/

#ifndef INTELGMA_DISPLAY_H_
#define INTELGMA_DISPLAY_H_

#define CLID_Hidd_Display_IntelGMA       "hidd.display.intelgma"
#define IID_Hidd_Display_IntelGMA        "hidd.display.intelgma"

struct HiddDisplayIntelGMAData {
    char *name;
    OOP_Object *overlay;
};

enum
{
    aoHidd_DisplayIntelGMA_Overlay,
    num_Hidd_DisplayIntelGMA_Attrs
};

#define HiddDisplayIntelGMAAttrBase              __IHidd_DisplayIntelGMA

#if !defined(__OOP_NOATTRBASES__) && !defined(__Hidd_DisplayIntelGMA_NOATTRBASE__)
extern OOP_AttrBase HiddDisplayIntelGMAAttrBase;
#endif

#define aHidd_DisplayIntelGMA_Overlay           (HiddDisplayIntelGMAAttrBase + aoHidd_DisplayIntelGMA_Overlay)

#define Hidd_DisplayIntelGMA_Switch(attr, idx) \
if (((idx) = (attr) - HiddDisplayIntelGMAAttrBase) < num_Hidd_DisplayIntelGMA_Attrs) \
switch (idx)

#endif /* INTELGMA_DISPLAY_H_ */
