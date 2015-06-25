/*
 * File      : board.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2012, RT-Thread Development Team
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rt-thread.org/license/LICENSE
 *
 * Change Logs:
 * Date           Author       Notes
 * 2012-11-20     Bernard    the first version
 */

#include <rthw.h>
#include <rtthread.h>

#include "board.h"
#include "serial.h"

extern void rt_hw_mmu_init(void);

/**
 * This function will initialize EVB board
 */
void rt_hw_board_init(void)
{
    // rt_hw_uart_init();
    // rt_console_set_device(RT_CONSOLE_DEVICE_NAME);

    /* MMU */
    rt_hw_mmu_init();
}

/*@}*/
