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

#include <asm/uaccess.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/export.h>

#include <vbus_api.h>

#include "linux_driver.h"
#include "prio_queue.h"

static struct rt_vbus_ring *OUT_RING;
static struct rt_vbus_ring *IN_RING;

/* 4 bytes for the head */
#define LEN2BNR(len)    ((len + RT_VBUS_BLK_HEAD_SZ \
			  + sizeof(struct rt_vbus_blk) - 1) \
			 / sizeof(struct rt_vbus_blk))

static char* dump_cmd_pkt(unsigned char *dp, size_t dsize);

static unsigned int _irq_offset;

static enum rt_vbus_chn_status _chn_status[RT_VBUS_CHANNEL_NR];

static inline int _chn_connected(unsigned char chnr)
{
    return _chn_status[chnr] == RT_VBUS_CHN_ST_ESTABLISHED ||
           _chn_status[chnr] == RT_VBUS_CHN_ST_SUSPEND;
}

/* {head, tail} */
#define _DT_HEAD 0
#define _DT_TAIL 1
static struct rt_vbus_data *_bus_data[RT_VBUS_CHANNEL_NR][2];
static DEFINE_MUTEX(_bus_data_lock);

#ifdef RT_VBUS_USING_FLOW_CONTROL
#include "watermark_queue.h"
struct rt_watermark_queue _chn_wm_que[RT_VBUS_CHANNEL_NR];
void rt_vbus_set_post_wm(unsigned char chnr, unsigned int low, unsigned int high)
{
    BUG_ON(!((0 < chnr) && (chnr < ARRAY_SIZE(_chn_wm_que))));
    rt_wm_que_set_mark(&_chn_wm_que[chnr], low, high);
}
EXPORT_SYMBOL(rt_vbus_set_post_wm);

static wait_queue_head_t _chn_suspended_threads[RT_VBUS_CHANNEL_NR];

struct
{
    unsigned int level;
    unsigned int high_mark;
    unsigned int low_mark;
    /* The suspend command does not have ACK. So if the other side still
     * sending pkg after SUSPEND, warn it again. Also use it as a flag that
     * tell me whether are we dropping from the high mark or not when reaching
     * the low mark. */
    unsigned int last_warn;
} _chn_recv_wm[RT_VBUS_CHANNEL_NR];

void rt_vbus_set_recv_wm(unsigned char chnr, unsigned int low, unsigned int high)
{
    BUG_ON(!((0 < chnr) && (chnr < ARRAY_SIZE(_chn_recv_wm))));
    _chn_recv_wm[chnr].low_mark = low;
    _chn_recv_wm[chnr].high_mark = high;
}
EXPORT_SYMBOL(rt_vbus_set_recv_wm);

#else

void rt_vbus_set_recv_wm(unsigned char chnr, unsigned int low, unsigned int high)
{}
EXPORT_SYMBOL(rt_vbus_set_recv_wm);

void rt_vbus_set_post_wm(unsigned char chnr, unsigned int low, unsigned int high)
{}
EXPORT_SYMBOL(rt_vbus_set_post_wm);

#endif


/** Push a data packet into the queue.
 *
 * The data packet should be allocated by kmalloc.
 */
static int rt_vbus_data_push(unsigned int id, struct rt_vbus_data *dat)
{
	int res;

	BUG_ON(!(0 < id && id < RT_VBUS_CHANNEL_NR));

	/* TODO: on mutex per channel. */
	res = mutex_lock_interruptible(&_bus_data_lock);
	if (res)
		return res;

	if (_bus_data[id][_DT_HEAD] == NULL) {
		_bus_data[id][_DT_HEAD] = dat;
		_bus_data[id][_DT_TAIL] = dat;
	} else {
		_bus_data[id][_DT_TAIL]->next = dat;
		_bus_data[id][_DT_TAIL] = dat;
	}

#ifdef RT_VBUS_USING_FLOW_CONTROL
	_chn_recv_wm[id].level++;
	if (_chn_recv_wm[id].level == 0)
		_chn_recv_wm[id].level = -1;
	if (_chn_recv_wm[id].level > _chn_recv_wm[id].high_mark &&
	    _chn_recv_wm[id].level > _chn_recv_wm[id].last_warn) {
		unsigned char buf[2] = {RT_VBUS_CHN0_CMD_SUSPEND, id};
		//pr_info("%s --> remote\n", dump_cmd_pkt(buf, sizeof(buf)));
		rt_vbus_post(0, 0, buf, sizeof(buf));
		/* Warn the other side in 100 more pkgs. */
		_chn_recv_wm[id].last_warn = _chn_recv_wm[id].level + 100;
	}
#endif
	mutex_unlock(&_bus_data_lock);

	return 0;
}

/** Pop a data packet from the queue.
 *
 * The data packet should be freed by kfree.
 */
struct rt_vbus_data* rt_vbus_data_pop(unsigned char id)
{
	int res;
	struct rt_vbus_data *dat;

	if (!(0 < id && id < RT_VBUS_CHANNEL_NR))
		return ERR_PTR(-EINVAL);

	res = mutex_lock_interruptible(&_bus_data_lock);
	if (res)
		return ERR_PTR(res);

	dat = _bus_data[id][_DT_HEAD];
	if (dat)
		_bus_data[id][_DT_HEAD] = dat->next;

#ifdef RT_VBUS_USING_FLOW_CONTROL
	if (_chn_recv_wm[id].level != 0) {
		_chn_recv_wm[id].level--;
		if (_chn_recv_wm[id].level == _chn_recv_wm[id].low_mark &&
		    _chn_recv_wm[id].last_warn > _chn_recv_wm[id].low_mark) {
			unsigned char buf[2] = {RT_VBUS_CHN0_CMD_RESUME, id};
			//pr_info("%s --> remote\n", dump_cmd_pkt(buf, sizeof(buf)));
			rt_vbus_post(0, 0, buf, sizeof(buf));
			_chn_recv_wm[id].last_warn = 0;
		}
	}
#endif
	mutex_unlock(&_bus_data_lock);

	return dat;
}
EXPORT_SYMBOL(rt_vbus_data_pop);

int rt_vbus_data_empty(unsigned char id)
{
	int res;

	if (id == 0 || id >= RT_VBUS_CHANNEL_NR)
		return 1;

	mutex_lock(&_bus_data_lock);
	res = (_bus_data[id][_DT_HEAD] == NULL);
	mutex_unlock(&_bus_data_lock);

	return res;
}
EXPORT_SYMBOL(rt_vbus_data_empty);

const char *rt_vbus_chn_st2str[] = {
	"available",
	"closed",
	"establishing",
	"established",
	"suspended",
	"closing",
};

const char *rt_vbus_sess_st2str[] = {
	"available",
	"listening",
	"establishing",
};

const char *rt_vbus_cmd2str[] = {
	"ENABLE",
	"DISABLE",
	"SET",
	"ACK",
	"NAK",
	"SUSPEND",
	"RESUME",
};

int rt_vbus_connection_ok(unsigned char chnr)
{
	if (chnr >= RT_VBUS_CHANNEL_NR || !_chn_connected(chnr))
		return 0;
	return 1;
}
EXPORT_SYMBOL(rt_vbus_connection_ok);

rt_vbus_callback _vbus_callbacks[RT_VBUS_CHANNEL_NR];

static void rt_vbus_notify_chn(unsigned char chnr)
{
	BUG_ON(chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR);

	if (likely(_vbus_callbacks[chnr])) {
		_vbus_callbacks[chnr](chnr);
	} else {
		pr_err("empty callback on chn: %d\n", chnr);
	}
}

static void rt_vbus_register_callback(unsigned char chnr, rt_vbus_callback cb)
{
	if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR)
		return;

	_vbus_callbacks[chnr] = cb;
}

static void rt_vbus_notify_host(void)
{
	rt_vmm_trigger_emuint(_irq_offset + RT_VBUS_HOST_VIRQ);
}

static void _ring_add_get_bnr(struct rt_vbus_ring *ring,
			      size_t bnr)
{
	int nidx = ring->get_idx + bnr;

	if (nidx >= RT_VMM_RB_BLK_NR) {
		nidx -= RT_VMM_RB_BLK_NR;
	}
	smp_wmb();
	ring->get_idx = nidx;
}

static int _bus_ring_space_nr(struct rt_vbus_ring *rg)
{
	int delta;

	smp_rmb();
	delta = rg->get_idx - rg->put_idx;

	if (delta > 0) {
		/* Put is behind the get. */
		return delta - 1;
	} else {
		/* delta is negative. */
		return RT_VMM_RB_BLK_NR + delta - 1;
	}
}

struct rt_vbus_pkg {
	unsigned char id;
	unsigned char prio;
	unsigned char len;
	const void *data;
	struct completion *cmp;
};

static void _vbus_isr_bridge(struct work_struct *work);

static void _havest_in_data(struct work_struct *work);
static struct rt_prio_queue* _prio_que;
static struct workqueue_struct *_ring_in_wkq;
DECLARE_WORK(_ring_in_wk, _havest_in_data);
static struct workqueue_struct *_ring_wkq;
DECLARE_WORK(_ring_wk, _vbus_isr_bridge);

int rt_vbus_post(unsigned char id, unsigned char prio,
		 const void *data, size_t len)
{
	int putsz = 0;
	int res = 0;
	struct rt_vbus_pkg pkg;
	const unsigned char *dp;
	DECLARE_COMPLETION_ONSTACK(cmp);

	if (id >= RT_VBUS_CHANNEL_NR)
		return -EINVAL;

#ifdef RT_VBUS_USING_FLOW_CONTROL
	res = wait_event_interruptible(_chn_suspended_threads[id],
				       _chn_status[id] != RT_VBUS_CHN_ST_SUSPEND);
	if (res)
		return res;
#endif

	if (_chn_status[id] != RT_VBUS_CHN_ST_ESTABLISHED)
		return -EINVAL;

	dp       = data;
	pkg.id   = id;
	pkg.prio = prio;
	for (putsz = 0; len; len -= putsz) {
		int dataend;

		pkg.data = dp;

		if (len > RT_VBUS_MAX_PKT_SZ) {
			putsz = RT_VBUS_MAX_PKT_SZ;
			dataend = 0;
		} else {
			putsz = len;
			dataend = 1;
		}

		pkg.len = putsz;
		dp += putsz;

		if (dataend) {
			pkg.cmp = &cmp;
		} else {
			pkg.cmp = NULL;
		}

#ifdef RT_VBUS_USING_FLOW_CONTROL
		res = rt_wm_que_inc(&_chn_wm_que[id]);
		if (res)
			break;
#endif

		/* We need to queue the work before push data into prio_que
		 * because rt_prio_queue_push may block and it's safe to queue
		 * the work more than once. */
		queue_work(_ring_in_wkq, &_ring_in_wk);

		res = rt_prio_queue_push(_prio_que, prio, (char*)&pkg);
		/*
		 *pr_info("post chn: %d, prio: %d, data: %p, len: %lu, res: %d\n",
		 *        id, prio, data, (unsigned long)len, res);
		 */
		if (res)
			break;

		/* There is a chance that the work is done *before*
		 * rt_prio_queue_push, which will result in dead lock(work done
		 * but there is data in prio_queue). So we have to queue the
		 * work after rt_prio_queue_push.
		 *
		 * FIXME: get rid off this.
		 */
		queue_work(_ring_in_wkq, &_ring_in_wk);

		if (dataend) {
			/* we need to let the cmp be valid as long as possible or the workqueue
			 * will complete on garbage completion. */
			wait_for_completion(&cmp);
		}
	}

	return res;
}
EXPORT_SYMBOL(rt_vbus_post);

enum _vbus_session_st
{
	SESSIOM_AVAILABLE,
	SESSIOM_LISTENING,
	SESSIOM_ESTABLISHING,
};

struct rt_vbus_conn_session {
	struct {
		unsigned char cmd;
		char name[RT_VBUS_CHN_NAME_MAX];
	} buf;
	/* negative value means error */
	int chnr;
	enum _vbus_session_st st;
	rt_vbus_callback cb;
	struct completion cmp;
	struct rt_vbus_request *req;
};

static struct rt_vbus_conn_session _sess[RT_VBUS_CHANNEL_NR/2];
static DEFINE_MUTEX(_sess_lock);

void rt_vbus_sess_dump(void)
{
	int i;

	pr_err("vbus conn session:\n");
	for (i = 0; i < ARRAY_SIZE(_sess); i++) {
		pr_err("%2d(%s):%s\n", i, _sess[i].buf.name,
		       rt_vbus_sess_st2str[_sess[i].st]);
	}
}

static int _sess_find(const unsigned char *name,
		      enum _vbus_session_st st)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(_sess); i++)
		if (_sess[i].st == st &&
		    strncmp(_sess[i].buf.name,
			    (char*)name,
			    sizeof(_sess[i].buf.name)) == 0)
			break;
	return i;
}

int rt_vbus_request_chn(struct rt_vbus_request *req,
			int is_server,
			rt_vbus_callback cb)
{
	int i, res, nlen;

	res = mutex_lock_interruptible(&_sess_lock);
	if (res)
		return res;

	for (i = 0; i < ARRAY_SIZE(_sess); i++) {
		if (_sess[i].st == SESSIOM_AVAILABLE)
			break;
	}
	if (i == ARRAY_SIZE(_sess)) {
		mutex_unlock(&_sess_lock);
		return -EBUSY;
	}

	strncpy(_sess[i].buf.name, req->name, sizeof(_sess[i].buf.name));
	_sess[i].buf.name[sizeof(_sess[i].buf.name)-1] = '\0';
	nlen = strlen(_sess[i].buf.name) + 1;

	pr_info("request chn for: %s, %s server\n",
		_sess[i].buf.name, is_server ? "is" : "not");
	init_completion(&_sess[i].cmp);

	_sess[i].cb = cb;
	_sess[i].req = req;

	if (is_server) {
		_sess[i].st = SESSIOM_LISTENING;
		mutex_unlock(&_sess_lock);
		goto Wait_for_cmp;
	}

	_sess[i].st = SESSIOM_ESTABLISHING;
	mutex_unlock(&_sess_lock);

	_sess[i].buf.cmd = RT_VBUS_CHN0_CMD_ENABLE;

	pr_info("%s --> remote\n", dump_cmd_pkt((char*)&_sess[i].buf, nlen+1));
	res = rt_vbus_post(0, 0, &_sess[i].buf, nlen+1);
	if (res < 0)
		return res;

Wait_for_cmp:
	res = wait_for_completion_interruptible(&_sess[i].cmp);
	if (res) {
		/* cleanup the mass when there is a signal but we have done
		 * some job */
		if (_sess[i].st == SESSIOM_ESTABLISHING) {
			_chn_status[_sess[i].chnr] = RT_VBUS_CHN_ST_AVAILABLE;
		}
	} else {
		res = _sess[i].chnr;
	}

	pr_info("get chnr: %d for %s\n", res, _sess[i].buf.name);

	_sess[i].st = SESSIOM_AVAILABLE;

	return res;
}
EXPORT_SYMBOL(rt_vbus_request_chn);

void rt_vbus_close_chn(unsigned char chnr)
{
	int err;
	struct rt_vbus_data *dat, *ndat;
	unsigned char buf[2] = {RT_VBUS_CHN0_CMD_DISABLE, chnr};

	BUG_ON(!(0 < chnr && chnr < RT_VBUS_CHANNEL_NR));

	if (_chn_status[chnr] == RT_VBUS_CHN_ST_CLOSED ||
	    _chn_status[chnr] == RT_VBUS_CHN_ST_CLOSING) {
		_chn_status[chnr] = RT_VBUS_CHN_ST_AVAILABLE;
		return;
	}

	if (!_chn_connected(chnr))
		return;

	_chn_status[chnr] = RT_VBUS_CHN_ST_CLOSING;
	pr_info("%s --> remote\n", dump_cmd_pkt(buf, sizeof(buf)));
	err = rt_vbus_post(0, 0, &buf, sizeof(buf));

	rt_vbus_register_callback(chnr, NULL);

	for (dat = _bus_data[chnr][_DT_HEAD];
	     dat; dat = ndat) {
		ndat = dat->next;
		kfree(dat);
	}

	_bus_data[chnr][_DT_HEAD] = _bus_data[chnr][_DT_TAIL] = NULL;
}
EXPORT_SYMBOL(rt_vbus_close_chn);

/* dump cmd that is not start with ACK/NAK */
static size_t __dump_naked_cmd(char *dst, size_t lsize,
			       unsigned char *dp, size_t dsize)
{
	size_t len;

	if (dp[0] == RT_VBUS_CHN0_CMD_DISABLE ||
	    dp[0] == RT_VBUS_CHN0_CMD_SUSPEND ||
	    dp[0] == RT_VBUS_CHN0_CMD_RESUME) {
		len = snprintf(dst, lsize, "%s %d",
			       rt_vbus_cmd2str[dp[0]], dp[1]);
	} else if (dp[0] == RT_VBUS_CHN0_CMD_ENABLE) {
		len = snprintf(dst, lsize, "%s %s",
			       rt_vbus_cmd2str[dp[0]], dp+1);
	} else if (dp[0] < RT_VBUS_CHN0_CMD_MAX) {
		len = snprintf(dst, lsize, "%s %s %d",
			       rt_vbus_cmd2str[dp[0]],
			       dp+1, dp[2+strlen((char*)dp+1)]);
	} else {
		len = snprintf(dst, lsize, "(invalid)%d %d",
			       dp[0], dp[1]);
	}
	return len;
}

static char _cmd_dump_buf[64];
static char* dump_cmd_pkt(unsigned char *dp, size_t dsize)
{
	size_t len;

	if (dp[0] == RT_VBUS_CHN0_CMD_ACK || dp[0] == RT_VBUS_CHN0_CMD_NAK ) {
		len = snprintf(_cmd_dump_buf, sizeof(_cmd_dump_buf),
			       "%s ", rt_vbus_cmd2str[dp[0]]);
		len += __dump_naked_cmd(_cmd_dump_buf+len, sizeof(_cmd_dump_buf)-len,
					dp+1, dsize-1);
	} else {
		len = __dump_naked_cmd(_cmd_dump_buf, sizeof(_cmd_dump_buf),
				       dp, dsize);
	}

	if (len > sizeof(_cmd_dump_buf) - 1)
		len = sizeof(_cmd_dump_buf) - 1;

	_cmd_dump_buf[len] = '\0';
	return _cmd_dump_buf;
}

static int _chn0_echo_with(unsigned char prefix,
			   size_t dsize,
			   unsigned char *dp)
{
	int len;
	unsigned char *resp;

	resp = kmalloc(dsize+1, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;
	*resp = prefix;
	memcpy(resp+1, dp, dsize);
	len = rt_vbus_post(0, 0, resp, dsize+1);
	if (len > 0)
		pr_info("%s --> remote\n", dump_cmd_pkt(resp, dsize+1));
	kfree(resp);

	return len;
}

static int _chn0_nak(size_t dsize, unsigned char *dp)
{
	return _chn0_echo_with(RT_VBUS_CHN0_CMD_NAK, dsize, dp);
}

static int _chn0_ack(size_t dsize, unsigned char *dp)
{
	return _chn0_echo_with(RT_VBUS_CHN0_CMD_ACK, dsize, dp);
}

static int _chn0_actor(unsigned char *dp, size_t dsize)
{
	if (*dp != RT_VBUS_CHN0_CMD_SUSPEND && *dp != RT_VBUS_CHN0_CMD_RESUME)
		pr_info("local <-- %s\n", dump_cmd_pkt(dp, dsize));

	switch (*dp) {
	case RT_VBUS_CHN0_CMD_ENABLE: {
		int i, chnr;
		int err;
		unsigned char *resp;

		i = _sess_find(dp+1, SESSIOM_LISTENING);
		if (i == ARRAY_SIZE(_sess)) {
			_chn0_nak(dsize, dp);
			break;
		}

		for (chnr = 0; chnr < ARRAY_SIZE(_chn_status); chnr++) {
			if (_chn_status[chnr] == RT_VBUS_CHN_ST_AVAILABLE)
				break;
		}
		if (chnr == ARRAY_SIZE(_chn_status)) {
			_chn0_nak(dsize, dp);
			break;
		}

		resp = kmalloc(dsize + 1, GFP_KERNEL);
		if (!resp)
			break;

		*resp = RT_VBUS_CHN0_CMD_SET;
		memcpy(resp+1, dp+1, dsize-1);
		resp[dsize] = chnr;

		rt_vbus_set_recv_wm(chnr, _sess[i].req->recv_wm.low, _sess[i].req->recv_wm.high);
		rt_vbus_set_post_wm(chnr, _sess[i].req->post_wm.low, _sess[i].req->post_wm.high);

		err = rt_vbus_post(0, 0, resp, dsize+1);

		if (err >= 0) {
			pr_info("%s --> remote\n", dump_cmd_pkt(resp, dsize+1));
			_sess[i].st   = SESSIOM_ESTABLISHING;
			_sess[i].chnr = chnr;
			_chn_status[chnr] = RT_VBUS_CHN_ST_ESTABLISHING;
		} else {
			pr_err("post chn0 SET err: %d\n", err);
		}
		kfree(resp);
	}
		break;
	case RT_VBUS_CHN0_CMD_SET: {
		int i, chnr;

		pr_info("setting %s\n", dp+1);

		i = _sess_find(dp+1, SESSIOM_ESTABLISHING);
		if (i == ARRAY_SIZE(_sess))
			/* drop that spurious packet */
			break;

		chnr = dp[1+strlen(dp+1)+1];
		pr_info("setting chnr %d\n", chnr);
		if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR) {
			_chn0_nak(dsize, dp);
			break;
		}

		if (_chn_status[chnr] != RT_VBUS_CHN_ST_AVAILABLE) {
			pr_err("invalid chnr: %d, state: %d\n",
			       chnr, _chn_status[chnr]);
			_chn0_nak(dsize, dp);
			break;
		}

		rt_vbus_register_callback(chnr, _sess[i].cb);
		rt_vbus_set_recv_wm(chnr, _sess[i].req->recv_wm.low, _sess[i].req->recv_wm.high);
		rt_vbus_set_post_wm(chnr, _sess[i].req->post_wm.low, _sess[i].req->post_wm.high);

		if (_chn0_ack(dsize, dp) >= 0) {
			_sess[i].chnr = chnr;
			_chn_status[chnr] = RT_VBUS_CHN_ST_ESTABLISHED;
			complete(&_sess[i].cmp);
		}
	}
		break;
	case RT_VBUS_CHN0_CMD_ACK:
		if (dp[1] == RT_VBUS_CHN0_CMD_SET) {
			int i, chnr;

			i = _sess_find(dp+2, SESSIOM_ESTABLISHING);
			if (i == ARRAY_SIZE(_sess)) {
				pr_info("drop spurious packet\n");
				break;
			}

			chnr = dp[1+strlen((const char*)dp+2)+2];

			rt_vbus_register_callback(chnr, _sess[i].cb);
			_chn_status[_sess[i].chnr] = RT_VBUS_CHN_ST_ESTABLISHED;
			complete(&_sess[i].cmp);
		} else if (dp[1] == RT_VBUS_CHN0_CMD_DISABLE) {
			unsigned char chnr = dp[2];

			if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR)
				break;

			/* We could only get here by sending DISABLE command, which is
			 * initiated by the rt_vbus_close_chn. */
			_chn_status[chnr] = RT_VBUS_CHN_ST_AVAILABLE;

			rt_vbus_register_callback(chnr, NULL);
			/* notify the thread that the channel has been closed */
			rt_vbus_notify_chn(chnr);
		} else {
			printk("VMM/Bus: unkown ACK for %d\n", dp[1]);
		}
		break;
	case RT_VBUS_CHN0_CMD_DISABLE: {
		unsigned char chnr = dp[1];

		if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR)
			break;

		if (_chn_status[chnr] != RT_VBUS_CHN_ST_ESTABLISHED)
			break;

		_chn_status[chnr] = RT_VBUS_CHN_ST_CLOSING;

		_chn0_ack(dsize, dp);
		/* notify the thread that the channel has been closed */
		rt_vbus_notify_chn(chnr);
	}
		break;
	case RT_VBUS_CHN0_CMD_NAK:
		if (dp[1] == RT_VBUS_CHN0_CMD_ENABLE) {
			int i;

			i = _sess_find(dp+2, SESSIOM_ESTABLISHING);
			if (i == ARRAY_SIZE(_sess))
				/* drop that spurious packet */
				break;

			_sess[i].chnr = -EIO;
			complete(&_sess[i].cmp);
		} else if (dp[1] == RT_VBUS_CHN0_CMD_SET) {
			pr_err("NAK for %d not implemented\n", dp[1]);
		} else {
			pr_err("invalid NAK for %d\n", dp[1]);
		}
		break;
	case RT_VBUS_CHN0_CMD_SUSPEND: {
#ifdef RT_VBUS_USING_FLOW_CONTROL
		unsigned char chnr = dp[1];

		if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR)
			break;

		if (_chn_status[chnr] != RT_VBUS_CHN_ST_ESTABLISHED)
			break;

		_chn_status[chnr] = RT_VBUS_CHN_ST_SUSPEND;
#endif
	}
		break;
	case RT_VBUS_CHN0_CMD_RESUME: {
#ifdef RT_VBUS_USING_FLOW_CONTROL
		unsigned char chnr = dp[1];

		if (chnr == 0 || chnr >= RT_VBUS_CHANNEL_NR)
			break;

		if (_chn_status[chnr] != RT_VBUS_CHN_ST_SUSPEND)
			break;

		_chn_status[chnr] = RT_VBUS_CHN_ST_ESTABLISHED;

		wake_up_interruptible_all(&_chn_suspended_threads[chnr]);
#endif
	}
		break;
	default:
		/* just ignore the invalid cmd */
		printk("VMM/Bus: unknown cmd %d on chn0\n", *dp);
		break;
	};
	return 0;
}

static wait_queue_head_t _do_post_wait;

static int _vbus_do_post_check_space(struct rt_vbus_ring *rg, int dnr)
{
	if (_bus_ring_space_nr(rg) >= dnr)
		return 1;

	rg->blocked = 1;
	smp_wmb();
	rt_vbus_notify_host();
	return 0;
}

static int _vbus_do_post(unsigned char id, unsigned char prio,
			 const void *data, size_t len)
{
	int dnr, res;
	unsigned int nxtidx;

	if (id >= RT_VBUS_CHANNEL_NR || !_chn_connected(id))
		return -EINVAL;

#ifdef RT_VBUS_USING_FLOW_CONTROL
	rt_wm_que_dec(&_chn_wm_que[id]);
#endif

	BUG_ON(len > RT_VBUS_MAX_PKT_SZ);
	dnr = LEN2BNR(len);

	/* Wait for enough space first. Don't remember to set the blocked flag.
	 */
	res = wait_event_interruptible(_do_post_wait,
				       _vbus_do_post_check_space(IN_RING, dnr));
	if (res)
		return res;

	IN_RING->blocked = 0;

	/*
	 *pr_info("post for user chn %d, prio %d, len %d\n",
	 *        id, prio, len);
	 */

	nxtidx = IN_RING->put_idx + dnr;

	IN_RING->blks[IN_RING->put_idx].id  = id;
	IN_RING->blks[IN_RING->put_idx].qos = prio;
	IN_RING->blks[IN_RING->put_idx].len = len;

	if (nxtidx >= RT_VMM_RB_BLK_NR) {
		unsigned int tailsz;

		tailsz = (RT_VMM_RB_BLK_NR - IN_RING->put_idx)
			* sizeof(IN_RING->blks[0]) - RT_VBUS_BLK_HEAD_SZ;

		/* the remaining block is sufficient for the data */
		if (tailsz > len)
			tailsz = len;

		memcpy(&IN_RING->blks[IN_RING->put_idx].data,
		       data, tailsz);
		memcpy(&IN_RING->blks[0], ((char*)data)+tailsz,
		       len - tailsz);

		smp_wmb();
		IN_RING->put_idx = nxtidx - RT_VMM_RB_BLK_NR;
	} else {
		memcpy(&IN_RING->blks[IN_RING->put_idx].data,
		       data, len);

		smp_wmb();
		IN_RING->put_idx = nxtidx;
	}

	smp_wmb();
	rt_vbus_notify_host();

	return len;
}

static void _havest_in_data(struct work_struct *work)
{
	int res;
	struct rt_vbus_pkg pkg;

	for (res = rt_prio_queue_trypop(_prio_que, (char*)&pkg);
	     res == 0;
	     res = rt_prio_queue_trypop(_prio_que, (char*)&pkg)) {
		_vbus_do_post(pkg.id, pkg.prio,
			      pkg.data, pkg.len);
		if (pkg.cmp)
			complete(pkg.cmp);
	}
}

static irqreturn_t _vbus_isr(int irq,  void *dev_id)
{
	/* while(not empty) */
	while (OUT_RING->get_idx != OUT_RING->put_idx) {
		size_t size;
		struct rt_vbus_data *dp;
		unsigned int id, nxtidx;
		unsigned int tailsz;

		size = OUT_RING->blks[OUT_RING->get_idx].len;
		id   = OUT_RING->blks[OUT_RING->get_idx].id;

		/*
		 *pr_info("get pkg for chn %d, len %d\n",
		 *        id, size);
		 */

		/* Suspended channel can still recv data. */
		if (id > RT_VBUS_CHANNEL_NR || !_chn_connected(id)) {
			/* drop the invalid packet */
			if (!(_chn_status[id] == RT_VBUS_CHN_ST_CLOSED ||
			      _chn_status[id] == RT_VBUS_CHN_ST_CLOSING))
				pr_info("drop invalid packet by id(%d), %d, %d\n",
					id, size, _chn_status[id]);
			_ring_add_get_bnr(OUT_RING, LEN2BNR(size));
			continue;
		}

		if (id == 0) {
			if (size > 60)
				pr_err("too big(%d) packet on chn0\n", size);
			else
				_chn0_actor(OUT_RING->blks[OUT_RING->get_idx].data, size);
			_ring_add_get_bnr(OUT_RING, LEN2BNR(size));
			continue;
		}

		dp = kmalloc(size + sizeof(*dp), GFP_KERNEL);
		if (!dp) {
			pr_info("drop on kmalloc fail\n");
			_ring_add_get_bnr(OUT_RING, LEN2BNR(size));
			continue;
		}
		dp->size = size;
		dp->next = NULL;

		nxtidx = OUT_RING->get_idx + LEN2BNR(size);
		if (nxtidx == RT_VMM_RB_BLK_NR) {
			memcpy(dp+1, &OUT_RING->blks[OUT_RING->get_idx].data, size);
			rt_vbus_data_push(id, dp);

			OUT_RING->get_idx = nxtidx - RT_VMM_RB_BLK_NR;

			rt_vbus_notify_chn(id);
			continue;
		} else if (nxtidx < RT_VMM_RB_BLK_NR) {
			memcpy(dp+1, &OUT_RING->blks[OUT_RING->get_idx].data, size);
			rt_vbus_data_push(id, dp);

			OUT_RING->get_idx = nxtidx;

			rt_vbus_notify_chn(id);
			continue;
		}

		/* nxtidx > RT_VMM_RB_BLK_NR, join the data into a continuous
		 * region. */
		tailsz = (RT_VMM_RB_BLK_NR - OUT_RING->get_idx)
			* sizeof(OUT_RING->blks[0]) - RT_VBUS_BLK_HEAD_SZ;

		BUG_ON(tailsz > size);

		memcpy(dp + 1, &OUT_RING->blks[OUT_RING->get_idx].data,
		       tailsz);
		memcpy((char*)(dp + 1) + tailsz, &OUT_RING->blks[0],
		       size - tailsz);
		rt_vbus_data_push(id, dp);

		OUT_RING->get_idx = nxtidx - RT_VMM_RB_BLK_NR;

		rt_vbus_notify_chn(id);
	}

	smp_rmb();
	if (OUT_RING->blocked)
		rt_vbus_notify_host();

	return IRQ_HANDLED;
}

static void _vbus_isr_bridge(struct work_struct *work)
{
	_vbus_isr(0, NULL);
}

static irqreturn_t _vbus_isr2(int irq,  void *dev_id)
{
	if (irq != RT_VBUS_GUEST_VIRQ) return IRQ_HANDLED;

	if (IN_RING->blocked)
		wake_up_interruptible_all(&_do_post_wait);

	queue_work(_ring_wkq, &_ring_wk);
	return IRQ_HANDLED;
}

int driver_load(void __iomem *outr, void __iomem *inr)
{
	int res;

	_irq_offset = rt_vmm_get_int_offset();

	if (_irq_offset < 0)
		return _irq_offset;

	pr_info("get irq offset: %d\n", _irq_offset);

#ifdef CONFIG_ARM_GIC
	{
		typedef int (*smp_ipi_handler_t)(int irq, void *devid);
		extern void smp_set_ipi_handler(smp_ipi_handler_t handler);

		/* set IPI handler in Linux Kernel */
	    smp_set_ipi_handler((smp_ipi_handler_t)_vbus_isr2);
	    res = 0;
	}
#else
	res = request_irq(RT_VBUS_GUEST_VIRQ + _irq_offset,
			  _vbus_isr2, IRQF_ONESHOT,
			  "VMM-BUS", NULL);
#endif
	if (res) {
		pr_err("error request RTT VMM bus irq: %d\n", res);
		return res;
	}

	_prio_que = rt_prio_queue_create("vbus", RT_VMM_RB_BLK_NR, sizeof(struct rt_vbus_pkg));
	if (!_prio_que) {
		res = -ENOMEM;
		goto _free_irq;
	}

	init_waitqueue_head(&_do_post_wait);
	_ring_in_wkq = create_singlethread_workqueue("vbus_in");
	if (!_ring_in_wkq) {
		res = -ENOMEM;
		goto _free_que;
	}

	_ring_wkq = create_singlethread_workqueue("vbus");
	if (!_ring_wkq) {
		res = -ENOMEM;
		goto _free_que;
	}

	memset(_chn_status, RT_VBUS_CHN_ST_AVAILABLE, sizeof(_chn_status));
	_chn_status[0] = RT_VBUS_CHN_ST_ESTABLISHED;

#ifdef RT_VBUS_USING_FLOW_CONTROL
	{
		int i;

		for (i = 0; i < ARRAY_SIZE(_chn_wm_que); i++) {
			rt_wm_que_init(&_chn_wm_que[i],
				       RT_VMM_RB_BLK_NR / 3,
				       RT_VMM_RB_BLK_NR * 2 / 3);
		}
		/* Channel 0 has the full channel. */
		rt_wm_que_set_mark(&_chn_wm_que[0], 0, -1);

		for (i = 0; i < ARRAY_SIZE(_chn_suspended_threads); i++) {
			init_waitqueue_head(&_chn_suspended_threads[i]);
		}

		for (i = 1; i < ARRAY_SIZE(_chn_recv_wm); i++) {
			rt_vbus_set_recv_wm(i,
					    RT_VMM_RB_BLK_NR / 3,
					    RT_VMM_RB_BLK_NR * 2 / 3);
			_chn_recv_wm[i].level = 0;
			_chn_recv_wm[i].last_warn = 0;
		}
		/* Channel 0 has the full channel. Don't suspend it. */
		_chn_recv_wm[0].low_mark = 0;
		_chn_recv_wm[0].high_mark = -1;
		_chn_recv_wm[0].level = 0;
		_chn_recv_wm[0].last_warn = 0;
	}
#endif

	res = chn0_load();
	if (res)
		goto _free_wkq;

	OUT_RING = outr;
	IN_RING  = inr;

	pr_info("VBus loaded: %d in blocks, %d out blocks\n",
		RT_VMM_RB_BLK_NR, RT_VMM_RB_BLK_NR);

	return res;
_free_wkq:
	if (_ring_in_wkq)
		destroy_workqueue(_ring_in_wkq);
	if (_ring_wkq)
		destroy_workqueue(_ring_wkq);
_free_que:
	rt_prio_queue_delete(_prio_que);
_free_irq:
	free_irq(RT_VBUS_GUEST_VIRQ + _irq_offset, NULL);
	return res;
}

void driver_unload(void)
{
	chn0_unload();

	cancel_work_sync(&_ring_in_wk);
	destroy_workqueue(_ring_in_wkq);
	cancel_work_sync(&_ring_wk);
	destroy_workqueue(_ring_wkq);
	rt_prio_queue_delete(_prio_que);

	free_irq(RT_VBUS_GUEST_VIRQ + _irq_offset, NULL);
}
