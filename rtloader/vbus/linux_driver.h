/*
 *  RT-Thread/Linux driver
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

#ifndef __LINUX_DRIVER_H__
#define __LINUX_DRIVER_H__

#include "rt_vbus_user.h"

int driver_load(void __iomem *outr, void __iomem *inr);
void driver_unload(void);

int rt_vbus_connection_ok(unsigned char chnr);

typedef void (*rt_vbus_callback)(unsigned char chn);

int rt_vbus_request_chn(struct rt_vbus_request *req,
			int is_server,
			rt_vbus_callback cb);
void rt_vbus_close_chn(unsigned char);

int rt_vbus_post(unsigned char id, unsigned char prio,
		 const void *data, size_t len);

void rt_vbus_set_post_wm(unsigned char chnr,
			 unsigned int low, unsigned int high);
void rt_vbus_set_recv_wm(unsigned char chnr,
			 unsigned int low, unsigned int high);

struct rt_vbus_data {
	size_t size;
	struct rt_vbus_data *next;
	/* data follows */
};

struct rt_vbus_data* rt_vbus_data_pop(unsigned char chnr);
int rt_vbus_data_empty(unsigned char id);

void rt_vmm_clear_emuint(unsigned int nr);
void rt_vmm_trigger_emuint(unsigned int irqnr);
int rt_vmm_get_int_offset(void);

int chn0_load(void);
void chn0_unload(void);

#define RT_VBUS_USING_FLOW_CONTROL

#endif /* end of include guard: __LINUX_DRIVER_H__ */
