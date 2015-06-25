/*
 *  GIC interrupt 
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

#include <asm/io.h>
#include <linux/irq.h>
#include <asm/smp.h>

#include "linux_driver.h"

void __iomem *_aintc_base;

void rt_vmm_clear_emuint(unsigned int irqnr)
{
    /* Do nothing, SGI is cleared by EOI. */
}

void rt_vmm_trigger_emuint(unsigned int irqnr)
{
	extern void arch_send_ipi(int cpu, int ipi);
	arch_send_ipi(1, irqnr);
}

int rt_vmm_get_int_offset(void)
{
    return 0;
}

