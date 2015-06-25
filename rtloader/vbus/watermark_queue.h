/*
 * Thread queue with water mark
 *
 * COPYRIGHT (C) 2014, Shanghai Real-Thread Technology Co., Ltd
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
 * 2014-04-16     Grissiom     first version
 */

struct rt_watermark_queue
{
	/* Current water level. */
	unsigned int level;
	unsigned int high_mark;
	unsigned int low_mark;
	wait_queue_head_t waitq;
	spinlock_t lock;
};

/** Init the struct rt_watermark_queue.
 */
void rt_wm_que_init(struct rt_watermark_queue *wg,
                    unsigned int low, unsigned int high);
void rt_wm_que_set_mark(struct rt_watermark_queue *wg,
                        unsigned int low, unsigned int high);
void rt_wm_que_dump(struct rt_watermark_queue *wg);

/* Water marks are often used in performance critical places. Benchmark shows
 * inlining functions will have 10% performance gain in some situation(for
 * example, VBus). So keep the inc/dec compact and inline. */

/** Increase the water level.
 *
 * It should be called in the thread that want to raise the water level. If the
 * current level is above the high mark, the thread will be suspended up to
 */
static inline int rt_wm_que_inc(struct rt_watermark_queue *wg)
{
	spin_lock(&wg->lock);

	if (wg->level > wg->high_mark) {
		DEFINE_WAIT(__wait);

		for (;;) {
			prepare_to_wait(&wg->waitq,
					&__wait,
					TASK_INTERRUPTIBLE);

			if (wg->level <= wg->high_mark)
				break;

			if (signal_pending(current)) {
				finish_wait(&wg->waitq, &__wait);
				spin_unlock(&wg->lock);
				return -ERESTARTSYS;
			}

			/* Give other thread the chance to change th water
			 * level. */
			spin_unlock(&wg->lock);
			schedule();
			spin_lock(&wg->lock);
		}
		finish_wait(&wg->waitq, &__wait);
	}

	wg->level++;
	if (wg->level == 0)
		wg->level = -1;
	spin_unlock(&wg->lock);

	return 0;
}

/** Decrease the water level.
 *
 * It should be called by the consumer that drain the water out. If the water
 * level reached low mark, all the thread suspended in this queue will be waken
 * up. It's safe to call this function in interrupt context.
 */
static inline void rt_wm_que_dec(struct rt_watermark_queue *wg)
{
	spin_lock(&wg->lock);

	if (wg->level == 0) {
		spin_unlock(&wg->lock);
		return;
	}

	wg->level--;
	if (wg->level == wg->low_mark) {
		/* There should be spaces between the low mark and high mark, so it's
		 * safe to resume all the threads. */
		spin_unlock(&wg->lock);
		wake_up_interruptible_all(&wg->waitq);
		return;
	}
	spin_unlock(&wg->lock);
}
