/*
 * File      : startup.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006, RT-Thread Develop Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-12-05     Bernard      the first version
 */

#include <rthw.h>
#include <rtthread.h>

#include <board.h>

extern void rt_hw_board_init(void);

#define RTT_BASE_ADDR	0x6fc00000
typedef void (*func_t)(void);

/**
 * This function will startup RT-Thread RTOS.
 */
void rtthread_startup(void)
{
    func_t func;

    /* initialize board */
    rt_hw_board_init();

	func = (func_t)RTT_BASE_ADDR;
	func();
	
    /* never reach here */
    return ;
}

int main(void)
{
    /* disable interrupt first */
    rt_hw_interrupt_disable();

    /* invoke rtthread_startup */
    rtthread_startup();

    return 0;
}

