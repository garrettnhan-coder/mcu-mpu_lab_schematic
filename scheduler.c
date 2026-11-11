/*
 * scheduler.c
 *
 *  Created on: 8 thg 11, 2026
 *      Author: Windows
 */

#include "scheduler.h"
#include "main.h"

static sTasks SCH_tasks_G[SCH_MAX_TASKS];
static uint8_t head_G = SCH_NO_TASK;

static volatile uint8_t ready_Q[SCH_READY_SIZE]; // FIFO of due task slots
static volatile uint8_t ready_head = 0; // written by SCH_Update
static volatile uint8_t ready_tail = 0; // read by Dispatch

static volatile uint32_t tick_count_G = 0;
uint8_t Error_code_G = 0;

// protect the task list against the ISR while main modifies
#define ENTER_CRITICAL()  uint32_t primask_ = __get_PRIMASK(); __disable_irq()
#define EXIT_CRITICAL()   __set_PRIMASK(primask_)

/* Insert task 'idx' so that it becomes due 'delay' ticks from now.
The list is sorted by due time; each Delay is relative to the previous
task. Equal due times keep insertion order (stable) -> correct order.
Must be called with interrupts disabled (or from the ISR).*/

static void SCH_Insert(uint8_t idx, uint32_t delay)
{
	uint8_t prev = SCH_NO_TASK;
	uint8_t cur  = head_G;

	while (cur != SCH_NO_TASK && delay >= SCH_tasks_G[cur].Delay)
	{
		delay -= SCH_tasks_G[cur].Delay;
		prev = cur;
		cur  = SCH_tasks_G[cur].Next;
	}

	SCH_tasks_G[idx].Delay = delay;
	SCH_tasks_G[idx].Next  = cur;
	if (cur != SCH_NO_TASK)
	{
		SCH_tasks_G[cur].Delay -= delay;
	}

	if (prev == SCH_NO_TASK)
		head_G = idx;
	else
		SCH_tasks_G[prev].Next = idx;
}

void SCH_Init(void)
{
	for (uint8_t i = 0; i < SCH_MAX_TASKS; i++)
	{
		SCH_tasks_G[i].pTask  = 0;
		SCH_tasks_G[i].Delay  = 0;
		SCH_tasks_G[i].Period = 0;
		SCH_tasks_G[i].RunMe  = 0;
		SCH_tasks_G[i].TaskID = i;
		SCH_tasks_G[i].Next   = SCH_NO_TASK;
	}
	head_G       = SCH_NO_TASK;
	ready_head   = 0;
	ready_tail   = 0;
	tick_count_G = 0;
	Error_code_G = 0;
}

uint32_t SCH_Add_Task(void (*pFunction)(), uint32_t DELAY, uint32_t PERIOD)
{
	uint8_t idx;
	ENTER_CRITICAL();

	if (pFunction == 0)
	{
		EXIT_CRITICAL();
		return SCH_MAX_TASKS;
	}

	/* find a free slot */
	for (idx = 0; idx < SCH_MAX_TASKS; idx++)
	{
		if (SCH_tasks_G[idx].pTask == 0) break;
	}

	if (idx == SCH_MAX_TASKS)
	{
		Error_code_G = ERROR_SCH_TOO_MANY_TASKS;
		EXIT_CRITICAL();
		return SCH_MAX_TASKS;
	}

	SCH_tasks_G[idx].pTask  = pFunction;
	SCH_tasks_G[idx].Period = PERIOD;
	SCH_tasks_G[idx].RunMe  = 0;
	SCH_tasks_G[idx].TaskID = idx;
	SCH_Insert(idx, DELAY);

	EXIT_CRITICAL();
	return idx;
}

/* Called every tick from the timer ISR.
Only the head of the list is decremented (O(1)). Tasks that reach 0 are
moved to the ready FIFO; periodic tasks are re-inserted immediately so
their period does not drift even if the dispatcher is late.*/
void SCH_Update(void)
{
	tick_count_G++;

	if (head_G == SCH_NO_TASK) return;

	if (SCH_tasks_G[head_G].Delay > 0)
		SCH_tasks_G[head_G].Delay--;

	while (head_G != SCH_NO_TASK && SCH_tasks_G[head_G].Delay == 0)
	{
		uint8_t idx = head_G;
		head_G = SCH_tasks_G[idx].Next;
		SCH_tasks_G[idx].Next = SCH_NO_TASK;

		uint8_t next_w = (uint8_t)((ready_head + 1) % SCH_READY_SIZE);
		if (next_w == ready_tail)
		{
			Error_code_G = ERROR_SCH_READY_OVERFLOW;   /* FIFO full */
		}
		else
		{
			ready_Q[ready_head] = idx;
			ready_head = next_w;
			SCH_tasks_G[idx].RunMe++;
		}

		if (SCH_tasks_G[idx].Period > 0)
		{
			SCH_Insert(idx, SCH_tasks_G[idx].Period);   /* periodic */
		}
	}
}

/* Called in the super loop: run all due tasks in FIFO order */
void SCH_Dispatch_Tasks(void)
{
	while (ready_tail != ready_head)
	{
		uint8_t idx = ready_Q[ready_tail];
		ready_tail = (uint8_t)((ready_tail + 1) % SCH_READY_SIZE);

		void (*task)(void) = 0;
		{
			ENTER_CRITICAL();
			if (SCH_tasks_G[idx].pTask != 0 && SCH_tasks_G[idx].RunMe > 0)
			{
				SCH_tasks_G[idx].RunMe--;
				task = SCH_tasks_G[idx].pTask;
			}
			EXIT_CRITICAL();
		}

		if (task != 0)
		{
			(*task)();   /* run the task to completion */

			/* one-shot task: free its slot after it has run */
			ENTER_CRITICAL();
			if (SCH_tasks_G[idx].pTask == task &&
				SCH_tasks_G[idx].Period == 0 &&
				SCH_tasks_G[idx].RunMe == 0)
			{
				SCH_tasks_G[idx].pTask = 0;
			}
			EXIT_CRITICAL();
		}
	}
}

uint8_t SCH_Delete_Task(uint32_t taskID)
{
	uint8_t ret;
	ENTER_CRITICAL();

	if (taskID >= SCH_MAX_TASKS || SCH_tasks_G[taskID].pTask == 0)
	{
		Error_code_G = ERROR_SCH_CANNOT_DELETE_TASK;
		ret = SCH_RETURN_ERROR;
	}
	else
	{
		/* unlink from the delta list (if it is still in it) */
		uint8_t prev = SCH_NO_TASK;
		uint8_t cur  = head_G;
		while (cur != SCH_NO_TASK && cur != (uint8_t)taskID)
		{
			prev = cur;
			cur  = SCH_tasks_G[cur].Next;
		}
		if (cur == (uint8_t)taskID)
		{
			uint8_t nxt = SCH_tasks_G[cur].Next;
			if (nxt != SCH_NO_TASK)
				SCH_tasks_G[nxt].Delay += SCH_tasks_G[cur].Delay;
			if (prev == SCH_NO_TASK)
				head_G = nxt;
			else
				SCH_tasks_G[prev].Next = nxt;
		}

		SCH_tasks_G[taskID].pTask  = 0;
		SCH_tasks_G[taskID].Delay  = 0;
		SCH_tasks_G[taskID].Period = 0;
		SCH_tasks_G[taskID].RunMe  = 0;
		SCH_tasks_G[taskID].Next   = SCH_NO_TASK;
		ret = SCH_RETURN_NORMAL;
	}

	EXIT_CRITICAL();
	return ret;
}

uint32_t SCH_Get_Tick(void)
{
	return tick_count_G;
}

uint32_t SCH_Get_Time_Ms(void)
{
	return tick_count_G * SCH_TICK_MS;
}

