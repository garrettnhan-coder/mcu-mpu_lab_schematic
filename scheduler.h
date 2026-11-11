/*
 * scheduler.h
 *
 *  Created on: 8 thg 11, 2026
 *      Author: Windows
 */

#ifndef INC_SCHEDULER_H_
#define INC_SCHEDULER_H_
#include "stdint.h"

typedef struct
{
	void (*pTask)(void);
	uint32_t Delay;
	uint32_t Period;
	uint8_t RunMe;
	uint32_t TaskID;
	uint8_t Next;
} sTasks;

#define SCH_MAX_TASKS 10 // temporarily we define max tasks = 10;
#define SCH_TICK_MS 10 // timer tick period in 10ms
#define SCH_READY_SIZE 32 // size of the ready FIFO
#define SCH_NO_TASK 0xFF// "null" index

// return code of the delete task SCH_Delete_Task()
#define SCH_RETURN_NORMAL 0
#define SCH_RETURN_ERROR 1

// error codes stored in Error_code_G
#define ERROR_SCH_TOO_MANY_TASKS 1
#define ERROR_SCH_CANNOT_DELETE_TASK 2
#define ERROR_SCH_READY_OVERFLOW 3

extern uint8_t Error_code_G;

void SCH_Init(void);

uint32_t SCH_Add_Task(void (*pFunction)(),
		uint32_t DELAY,
		uint32_t PERIOD); //this function serves as set timer

void SCH_Update(void); //this function serves as timer run, it will be called in ISR (every 10ms)

void SCH_Dispatch_Tasks(void); //this function serves as flag checker, it will be called in while(1){}

uint8_t SCH_Delete_Task(uint32_t taskID);

uint32_t SCH_Get_Tick(void);

uint32_t SCH_Get_Time_Ms(void);

#endif /* INC_SCHEDULER_H_ */

























