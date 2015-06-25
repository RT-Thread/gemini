/*
 *  RT-Thread loader
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

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cpu.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>

#include <vbus_api.h>
#include <vbus_layout.h>

#include "linux_driver.h"

#define BUFF_SZ		(4 * 1024)

static int _do_startup(unsigned long start_addr)
{
    extern int vexpress_cpun_start(u32 address, int cpu);
    vexpress_cpun_start(start_addr, 1);

	return 0;
}

int _do_load_fw(const char* filename,
		unsigned long base_addr,
		size_t mem_size)
{
	mm_segment_t oldfs = {0};
	ssize_t len;
	unsigned long file_sz;
	loff_t pos = 0;
	struct file *flp = NULL;
	unsigned long buf_ptr = base_addr;

	printk("loading binary file:%s to %08lx....\n",
	       filename, buf_ptr);

	flp = filp_open(filename, O_RDONLY, S_IRWXU);
	if (IS_ERR(flp)) {
		printk("rtloader: open file failed. "
		       "Return 0x%p\n", flp);
		return -1;
	}

	/* get file size */
	file_sz = vfs_llseek(flp, 0, SEEK_END);
	if (file_sz > mem_size) {
		printk("rtloader: bin file too big. "
		       "mem size: 0x%08x, bin file size: %ld (0x%08lx)\n",
		       mem_size, file_sz, file_sz);
		filp_close(flp, NULL);
		return -1;
	}
	printk("rtloader: bin file size: %ld\n", file_sz);
	vfs_llseek(flp, 0, SEEK_SET);

	oldfs = get_fs();
	set_fs(get_ds());
	while (file_sz > 0) {
		len = vfs_read(flp, (void __user __force*)buf_ptr, BUFF_SZ, &pos);
		if (len < 0) {
			pr_err("read %08lx error: %d\n", buf_ptr, len);
			set_fs(oldfs);
			filp_close(flp, NULL);
			return -1;
		}
		file_sz -= len;
		buf_ptr += len;
	}
	set_fs(oldfs);

	printk("done!\n");

	/* flush RT-Thread memory */
	flush_cache_vmap(base_addr, mem_size);

	return 0;
}

static void __iomem *out_ring;

static int __init rtloader_init(void)
{
	int ret;
	unsigned long va;

	/* No need to cache the code as we don't run it on this CPU. Also,
	 * nocache means we don't need to flush it as well. */
	// va = ioremap_nocache(RT_BASE_ADDR, RT_MEM_SIZE);
	va = __phys_to_virt(0x6FB00000);
	_do_load_fw("/root/boot.bin", (unsigned long)va, 128 * 1024);

	va = __phys_to_virt(0x6FC00000);
	pr_info("get mapping :%08lx -> %08x, size: %08x\n",
		va, RT_BASE_ADDR, RT_MEM_SIZE);

	if (_do_load_fw("/root/rtthread.bin",
			(unsigned long)va,
			RT_MEM_SIZE) == 0) {
		int res;

		/* We have to down the CPU before loading the code because cpu_down
		 * will flush the cache. It will corrupt the code we just loaded some
		 * times. */
		ret = cpu_down(1);
		/* EBUSY means CPU is already released */
		if (ret && (ret != -EBUSY)) {
			pr_err("Can't release cpu1: %d\n", ret);
			return -ENOMEM;
		}

		res = _do_startup(0x6FB00000);
		pr_info("startup return %d\n", res);

		out_ring = (void*)__phys_to_virt(_RT_VBUS_RING_BASE);
		res = driver_load(out_ring, out_ring + _RT_VBUS_RING_SZ);
		pr_info("driver_load return %d\n", res);
	}

	return 0;
}

static void __exit rtloader_exit(void)
{
    driver_unload();
}

module_init(rtloader_init);
module_exit(rtloader_exit);

MODULE_AUTHOR("Grissiom <chaos.proton@gmail.com>");
MODULE_DESCRIPTION("RTLOADER");
MODULE_LICENSE("GPL");
