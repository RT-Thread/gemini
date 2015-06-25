/*
 *  RT-Thread/Linux VBUS user header
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

#ifndef __RT_VBUS_USER_H__
#define __RT_VBUS_USER_H__

struct rt_vbus_wm_cfg {
	unsigned int low, high;
};

struct rt_vbus_request {
	const char *name;
	int is_server;
	unsigned char prio;
	/* flags of the opened fd */
	int oflag;
	struct rt_vbus_wm_cfg recv_wm, post_wm;
};

/* find a spare magic in Documentation/ioctl/ioctl-number.txt */
#define VBUS_IOC_MAGIC     0xE1
#define VBUS_IOCREQ        _IOWR(VBUS_IOC_MAGIC, 0xE2, struct rt_vbus_request)
/* Water marks can be controled on the fly. */
#define VBUS_IOCRECV_WM    _IOWR(VBUS_IOC_MAGIC, 0xE3, struct rt_vbus_request)
#define VBUS_IOCPOST_WM    _IOWR(VBUS_IOC_MAGIC, 0xE4, struct rt_vbus_request)

/* keep consistent with beaglebone/components/vmm/share_hdr/rtt_api.h */
#define RT_VBUS_SHELL_DEV_NAME "vbser0"
#define RT_VBUS_RFS_DEV_NAME   "rfs"

#endif /* end of include guard: __RT_VBUS_USER_H__ */
