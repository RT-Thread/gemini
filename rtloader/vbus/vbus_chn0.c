/*
 *  RT-Thread/Linux VBUS channel 0
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

#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <asm/uaccess.h>

#include <vbus_api.h>

#include "linux_driver.h"
#include "vbus_chnx.h"

static struct cdev _chn0_dev;
static atomic_t _device_count = ATOMIC_INIT(1);

static int _open(struct inode *inode, struct file *filp)
{
	if (atomic_dec_return(&_device_count) < 0) {
		atomic_inc(&_device_count);
		return -EBUSY;
	}

	filp->private_data = &_chn0_dev;
	return 0;
}

static int _release(struct inode *inode, struct file *filp)
{
	BUG_ON(atomic_read(&_device_count) != 0);

	atomic_inc(&_device_count);

	return 0;
}

static long _ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int chnr, nlen;
	char chname[RT_VBUS_CHN_NAME_MAX];
	int res = -ENOTTY;
	struct rt_vbus_request req;

	switch (cmd) {
	case VBUS_IOCREQ:
		if (copy_from_user(&req, (struct rt_vbus_request*)arg,
				   sizeof(req)))
			return -EFAULT;

		nlen = strnlen_user(req.name, sizeof(chname));
		res = copy_from_user(chname, req.name, nlen);
		if (res < 0) {
			return -EFAULT;
		}
		/* Let the name point to kernel space. */
		req.name = chname;

		if (req.recv_wm.low > req.recv_wm.high)
			return -EINVAL;
		if (req.post_wm.low > req.post_wm.high)
			return -EINVAL;
		chnr = rt_vbus_request_chn(&req,
					   !!req.is_server,
					   vbus_chnx_callback);
		if (chnr < 0)
			return chnr;
		res = vbus_chnx_get_fd(chnr, req.prio, req.oflag);
		if (res < 0) {
			rt_vbus_close_chn(chnr);
			return res;
		}
	default:
		break;
	};
	return res;
}

static struct file_operations _chn0_ops = {
	.owner          = THIS_MODULE,
	.open           = _open,
	.release        = _release,
	.unlocked_ioctl = _ioctl,
};

static struct class *_chn0_cls;

int chn0_load(void)
{
	int res;

	cdev_init(&_chn0_dev, &_chn0_ops);
	_chn0_dev.owner = THIS_MODULE;
	alloc_chrdev_region(&_chn0_dev.dev, 0, 1, "vbus_ctl");

	res = cdev_add(&_chn0_dev, _chn0_dev.dev, 1);
	if (res) {
		pr_err("err adding chn0 device: %d\n", res);
		unregister_chrdev_region(_chn0_dev.dev, 1);
		return res;
	}

	pr_info("chn0 dev nr: %d\n", MAJOR(_chn0_dev.dev));

	_chn0_cls = class_create(THIS_MODULE, "rtvbus");
	if (IS_ERR(_chn0_cls)) {
		pr_err("err creating chn0 device class: %p\n", _chn0_cls);
		_chn0_cls = NULL;
	} else {
		struct device *p = device_create(_chn0_cls, NULL,
						 MKDEV(MAJOR(_chn0_dev.dev), 0),
						 NULL, "rtvbus");
		if (IS_ERR(p)) {
			pr_err("err creating chn0 device node: %p\n", p);
			pr_err("You have create it with "
			       "`mknod /dev/rtvbus c %d 0`\n",
			       MAJOR(_chn0_dev.dev));
			class_destroy(_chn0_cls);
			_chn0_cls = NULL;
		}
	}

	return vbus_chnx_init();
}

void chn0_unload(void)
{
	unregister_chrdev_region(_chn0_dev.dev, 1);
	cdev_del(&_chn0_dev);
	if (_chn0_cls) {
		device_destroy(_chn0_cls, MKDEV(MAJOR(_chn0_dev.dev), 0));
		class_destroy(_chn0_cls);
	}
	pr_info("unload chn0\n");
}
