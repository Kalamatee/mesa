#ifndef _DISPLAY_H_
#define _DISPLAY_H_

#include <exec/nodes.h>
#include <utility/tagitem.h>

#define MAX_MODE_NAME_LEN 30

struct DisplayDevice
{
    struct Node dd_Node;
    struct TagItem *dd_DisplayTags;
    struct TagItem *dd_SyncModes;
};

extern void EDIDParse(APTR edidpool, UBYTE *edid_data, struct TagItem **tagsptr, struct TagItem *poolptr);

#endif /* _DISPLAY_H_ */
