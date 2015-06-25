/*
 *  VMM Bus channel files
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

/* Inspired by the fs/timerfd.c in the Linux kernel
 *
 * All we need is a file associated with ops. That's all.
 */

#include <linux/file.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/anon_inodes.h>

#include <vbus_api.h>

#include "linux_driver.h"

struct vbus_chnx_ctx {
	unsigned char prio;
	struct rt_vbus_data *datap;
	size_t pos;
	int fd;
	wait_queue_head_t wait;
};

static struct vbus_chnx_ctx  _ctxs[RT_VBUS_CHANNEL_NR];

static int vbus_chnx_open(struct inode *inode, struct file *filp)
{
	printk("chx: try to open inode %p, filp %p\n", inode, filp);
	return 0;
}

static int vbus_chnx_release(struct inode *inode, struct file *filp)
{
	unsigned char chnr = (unsigned int)filp->private_data;

	/*pr_info("chx: release chnr %d, fd %d\n", chnr, _ctxs[chnr].fd);*/

	rt_vbus_close_chn(chnr);

	return 0;
}

static ssize_t vbus_chnx_write(struct file *filp,
			       const char __user *buf, size_t size,
			       loff_t *offp)
{
	int res;
	unsigned long chnr = (unsigned long)(filp->private_data);
	char *kbuf = kmalloc(size, GFP_KERNEL);

	if (!kbuf)
		return -ENOMEM;

	res = copy_from_user(kbuf, buf, size);
	if (res < 0)
		return -EFAULT;

	res = rt_vbus_post(chnr, _ctxs[chnr].prio, kbuf, size);

	kfree(kbuf);

	/* We simplified the things by treating the signal as error. */
	if (unlikely(res)) {
		/* Make sure the error is returned as negative values. */
		if (res < 0)
			return res;
		else
			return -res;
	} else
		return size;
}

static ssize_t vbus_chnx_read(struct file *filp,
			      char __user *buf, size_t size,
			      loff_t *offp)
{
	unsigned long chnr = (unsigned long)filp->private_data;
	struct vbus_chnx_ctx *ctx = &_ctxs[chnr];
	size_t outsz = 0;

	if (ctx->datap == NULL) {
		ctx->datap = rt_vbus_data_pop(chnr);
		ctx->pos   = 0;
	}
	else if (ctx->pos == ctx->datap->size) {
		kfree(ctx->datap);
		ctx->datap = rt_vbus_data_pop(chnr);
		ctx->pos   = 0;
	}

	if (ctx->datap == NULL) {
		int err;

		if (!rt_vbus_connection_ok(chnr))
			return 0;

		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		err = wait_event_interruptible(_ctxs[chnr].wait,
					       !rt_vbus_data_empty(chnr) ||
					       !rt_vbus_connection_ok(chnr));
		if (err)
			return err;

		ctx->datap = rt_vbus_data_pop(chnr);
		if (ctx->datap == NULL) {
			return 0;
		}
		ctx->pos = 0;
	}

	if (IS_ERR(ctx->datap)) {
		ssize_t err = PTR_ERR(ctx->datap);
		/* Cleanup datap so we don't crash if we access it again. */
		ctx->datap = NULL;
		return err;
	}

	while (ctx->datap) {
		size_t cpysz;

		if (size - outsz > ctx->datap->size - ctx->pos)
			cpysz = ctx->datap->size - ctx->pos;
		else
			cpysz = size - outsz;

		if (copy_to_user(buf + outsz,
				 ((char*)(ctx->datap+1)) + ctx->pos,
				 cpysz))
			return -EFAULT;
		ctx->pos += cpysz;

		outsz += cpysz;
		if (outsz == size) {
			return outsz;
		}
		BUG_ON(outsz > size);

		/* Free the old, get the new. */
		kfree(ctx->datap);
		ctx->datap = rt_vbus_data_pop(chnr);
		if (IS_ERR(ctx->datap)) {
			ctx->datap = NULL;
		}
		ctx->pos   = 0;
		/*pr_info("get other data %p, %d copied\n", ctx->datap, outsz);*/
	}
	return outsz;
}

static unsigned int vbus_chnx_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	unsigned long chnr = (unsigned long)filp->private_data;

	poll_wait(filp, &_ctxs[chnr].wait, wait);

	if (_ctxs[chnr].datap != NULL || !rt_vbus_data_empty(chnr))
		mask |= POLLIN | POLLRDNORM;
	if (!rt_vbus_connection_ok(chnr))
		mask |= POLLHUP;

	return mask;
}

static long vbus_chnx_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = -ENOTTY;

	switch (cmd) {
#ifdef RT_VBUS_USING_FLOW_CONTROL
	case VBUS_IOCRECV_WM: {
		struct rt_vbus_wm_cfg cfg;
		unsigned long chnr = (unsigned long)filp->private_data;

		if (copy_from_user(&cfg, (struct rt_vbus_wm_cfg*)arg,
				   sizeof(cfg)))
			return -EFAULT;
		rt_vbus_set_recv_wm(chnr, cfg.low, cfg.high);
		return 0;

	}
		break;
	case VBUS_IOCPOST_WM: {
		struct rt_vbus_wm_cfg cfg;
		unsigned long chnr = (unsigned long)filp->private_data;

		if (copy_from_user(&cfg, (struct rt_vbus_wm_cfg*)arg,
				   sizeof(cfg)))
			return -EFAULT;
		rt_vbus_set_post_wm(chnr, cfg.low, cfg.high);
		return 0;

	}
		break;
#endif
	default:
		break;
	};
	return res;
}

static const struct file_operations vbus_chnx_fops = {
	.owner          = THIS_MODULE,
	.open           = vbus_chnx_open,
	.release        = vbus_chnx_release,
	.read           = vbus_chnx_read,
	.write          = vbus_chnx_write,
	.llseek         = noop_llseek,
	.poll           = vbus_chnx_poll,
	.unlocked_ioctl = vbus_chnx_ioctl,
};

int vbus_chnx_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_ctxs); i++) {
		init_waitqueue_head(&_ctxs[i].wait);
	}
	return 0;
}

/* get a file descriptor corresponding to the channel
 */
int vbus_chnx_get_fd(unsigned char chnr,
		     unsigned char prio,
		     int oflag)
{
	/* supress compiler waring: cast to pointer from integer of different
	 * size */
	unsigned long lchnr = chnr;
	int fd = anon_inode_getfd("[vbus_chnx]", &vbus_chnx_fops,
				  (void*)lchnr, oflag);
	if (fd > 0) {
		if (prio == 0)
			prio = 1;
		else if (prio > 255)
			prio = 255;
		_ctxs[chnr].prio  = prio;
		_ctxs[chnr].datap = NULL;
		_ctxs[chnr].pos   = 0;
	}
	_ctxs[chnr].fd = fd;

	pr_info("get fd: %d, prio: %d\n", fd, prio);

	return fd;
}

void vbus_chnx_callback(unsigned char chnr)
{
	BUG_ON(chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR);

	wake_up_interruptible(&_ctxs[chnr].wait);
}

