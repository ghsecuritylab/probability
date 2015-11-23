/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
 *
 * Copyright (c) Puhui Xiong <phuuix@163.com>
 * @file
 *   A portable timer lib
 *
 * @History
 *   AUTHOR         DATE                 NOTES
 *   
 */

#include <stdlib.h>
#include <assert.h>

#include <ptimer.h>

/***********************************************************************************/
/* Function : ptimer_start                                                         */
/***********************************************************************************/
/* Description : - Start a timer                                                   */
/*                                                                                 */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   table              | i  | pointer to timer table                              */
/*   timer              | i  | timer                                               */
/*   timeval            | i  | expires time = now + timeval                        */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
int ptimer_start(ptimer_table_t *table, ptimer_t *timer, uint32_t timeval)
{
	uint32_t slot;
	
	if(table == NULL || timer == NULL)
		return -1;
	
	if(timer->flags & PTIMER_FLAG_RUNNING)
	{
		/* avoid timer is started multiple times */
		//ZLOG_WARN("timer is running, ignore it: 0x%p timeval=%d\n", timer, timeval);
		return -1;
	}
		
	timer->flags |= PTIMER_FLAG_RUNNING;
	timer->duration = timeval;
	if(timeval >= table->allslots)
	{
		timer->remainder = timeval - table->allslots + 1;
		slot = (table->curslot + table->allslots - 1) & (table->allslots - 1);
	}
	else
	{
		timer->remainder = 0;
	
		/* find register slot */
		slot = (table->curslot + timeval) & (table->allslots - 1);
	}
	
	dllist_append(&table->table[slot], (dllist_node_t *)timer);
	
//	ZLOG_DEBUG("start timer: 0x%p timeval=%u, curslot=%u target_slot=%u\n", timer, timeval, table->curslot, slot);
	return 0;
}


/***********************************************************************************/
/* Function : ptimer_start_remainder                                               */
/***********************************************************************************/
/* Description : - internal function                                               */
/*               - in case of remainder timer                                      */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   table              | i  | pointer to timer table                              */
/*   timer              | i  | timer                                               */
/*   timeval            | i  | expires time = now + timeval                        */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void ptimer_start_remainder(ptimer_table_t *table, ptimer_t *timer, uint32_t timeval)
{
	uint32_t slot;
			
	if(timeval >= table->allslots)
	{
		timer->remainder = timeval - table->allslots + 1;
		slot = (table->curslot + table->allslots - 1) & (table->allslots - 1);
	}
	else
	{
		timer->remainder = 0;
		
		/* find register slot */
		slot = (table->curslot + timeval) & (table->allslots - 1);
	}
	
    // in case of new slot equals current slot, we have to shift it by one
    if(slot == table->curslot)
        slot = (table->curslot + 1) & (table->allslots - 1);

	dllist_append(&table->table[slot], (dllist_node_t *)timer);
	
//	ZLOG_DEBUG("start timer remainder: 0x%p timeval=%u, curslot=%u target_slot=%u\n", timer, timeval, table->curslot, slot);
}


/***********************************************************************************/
/* Function : ptimer_cancel                                                        */
/***********************************************************************************/
/* Description : - Stop a timer                                                    */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   table              | i  | pointer to timer table                              */
/*   timer              | i  | timer                                               */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void ptimer_cancel(ptimer_table_t *table, ptimer_t *timer)
{
	if(table == NULL || timer == NULL)
		return;
		
//	ZLOG_DEBUG("cancel timer: 0x%p\n", timer);
	
	timer->flags &= ~PTIMER_FLAG_RUNNING;
	dllist_remove(NULL, (dllist_node_t *)timer);
}

/***********************************************************************************/
/* Function : ptimer_consume_time                                                  */
/***********************************************************************************/
/* Description : - consume time, it is up to user to call this function            */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   table              | i  | pointer to timer table                              */
/*   time               | i  | passed time                                         */
/*   Return             |    | N/A                                                 */
/***********************************************************************************/
void ptimer_consume_time(ptimer_table_t *table, uint32_t time)
{
	ptimer_t *timer;
	uint32_t i, ret;
	
	if(table == NULL) return;
	
	for(i=0; i<time; i++)
	{
		while(!DLLIST_EMPTY(&table->table[table->curslot]))
		{
			timer = (ptimer_t *)DLLIST_HEAD(&table->table[table->curslot]);

			assert(ptimer_is_running(timer));
			
			/* remove all timers in current slot */
			dllist_remove(NULL, (dllist_node_t *)timer);
			
			/* handle remainder */
			if(timer->remainder)
			{
				ptimer_start_remainder(table, timer, timer->remainder);
				continue;
			}
			
			/* mark timer as not running */
			timer->flags &= ~PTIMER_FLAG_RUNNING;
			
			/* call onexpired_func */
//			ZLOG_DEBUG("timer expired: 0x%p at slot %u\n", timer, table->curslot);
			if(timer->onexpired_func)
			{
				ret = timer->onexpired_func(timer, timer->param[0], timer->param[1]);
			}
			
			/* if periodic timer */
			if((timer->flags & PTIMER_FLAG_PERIODIC) && (ret == 0))
				ptimer_start(table, timer, timer->duration);
		}
		
		table->curslot = (table->curslot + 1) & (table->allslots - 1);
	}
}

/***********************************************************************************/
/* Function : ptimer_init                                                          */
/***********************************************************************************/
/* Description : - init timer per table mechanism                                  */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   table              | o  | pointer to timer table                              */
/*   allslots           | i  | the number of time slot of table                    */
/*   Return             |    | 0 is success                                        */
/***********************************************************************************/
int ptimer_init(ptimer_table_t *table, uint16_t allslots)
{
	uint16_t vpower = 1;
	
	if(table == NULL)
		return -1;
	
	if(allslots > (1<<15))	/* 32768 */
		vpower = (1<<15);
	else if(allslots < 64)
		vpower = 64;
	else
	{
		while(vpower < allslots)
			vpower = vpower << 1;
	}
	
	table->table = malloc(sizeof(dllist_node_t) * vpower);
	table->allslots = vpower;
	table->curslot = 0;
	
	if(table->table)
	{
		uint32_t i;
		for(i=0; i<table->allslots; i++)
		{
			dllist_init(&table->table[i]);
		}
	}
	
	return table->table?0:-1;
}

/* allslots must be power of 2 */
int ptimer_init_nomalloc(ptimer_table_t *table, uint16_t allslots, dllist_node_t *slot)
{
	if(table == NULL)
		return -1;
	
	table->table = slot;
	table->allslots = allslots;
	table->curslot = 0;
	
	if(table->table)
	{
		uint32_t i;
		for(i=0; i<table->allslots; i++)
		{
			dllist_init(&table->table[i]);
		}
	}
	
	return table->table?0:-1;
}


/***********************************************************************************/
/* Function : ptimer_is_running                                                    */
/***********************************************************************************/
/* Description : - judge if timer is running                                       */
/*                                                                                 */
/* Interface :                                                                     */
/*      Name            | io |       Description                                   */
/* ---------------------|----|-----------------------------------------------------*/
/*   timer              | i  |                                                     */
/*   Return             |    | 1 is running                                        */
/***********************************************************************************/
int ptimer_is_running(ptimer_t *timer)
{
	return (timer->flags & PTIMER_FLAG_RUNNING);
}
