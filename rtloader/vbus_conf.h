/*
 *  RT-Thread VBUS configuration
 *
 * COPYRIGHT (C) 2013, Shanghai Real-Thread Technology Co., Ltd
 *
 *  This file is part of RT-Thread (http://www.rt-thread.org)
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2013-09-11     Grissiom     the first verion
 */

#ifndef VBUS_CONFIG_H__
#define VBUS_CONFIG_H__

/* This is configures where the rings are located. */

#define _RT_VBUS_RING_BASE (0x70000000 - 8 * 1024 * 1024)
#define _RT_VBUS_RING_SZ   (2 * 1024 * 1024)

#define RT_VBUS_OUT_RING   ((struct rt_vbus_ring*)(_RT_VBUS_RING_BASE))
#define RT_VBUS_IN_RING    ((struct rt_vbus_ring*)(_RT_VBUS_RING_BASE + _RT_VBUS_RING_SZ))

#define RT_VBUS_GUEST_VIRQ   14
#define RT_VBUS_HOST_VIRQ    15

#define RT_VBUS_SHELL_DEV_NAME "vbser0"
#define RT_VBUS_RFS_DEV_NAME   "rfs"

#define RT_BASE_ADDR    0x6FC00000
#define RT_MEM_SIZE     0x400000

/* Number of blocks in VBus. The total size of VBus is
 * RT_VMM_RB_BLK_NR * 64byte * 2. */
#define RT_VMM_RB_BLK_NR     (_RT_VBUS_RING_SZ / 64 - 1) 

#endif
