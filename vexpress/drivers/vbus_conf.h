#ifndef __VBUS_CONF_H__
#define __VBUS_CONF_H__

/* This is configures where the rings are located. */

#define _RT_VBUS_RING_BASE (0x6f800000)
#define _RT_VBUS_RING_SZ   (2 * 1024 * 1024)

/* Number of blocks in VBus. The total size of VBus is
 * RT_VMM_RB_BLK_NR * 64byte * 2. */
#define RT_VMM_RB_BLK_NR     (_RT_VBUS_RING_SZ / 64 - 1)

/* We don't use the IRQ number to trigger IRQ in this BSP. */
#define RT_VBUS_GUEST_VIRQ    14
#define RT_VBUS_HOST_VIRQ     15

#define RT_VBUS_SHELL_DEV_NAME "vbser0"
#define RT_VBUS_RFS_DEV_NAME   "rfs"

#endif /* end of include guard: __VBUS_CONF_H__ */

