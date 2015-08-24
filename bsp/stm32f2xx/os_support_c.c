/*
 * os_support_c.c
 * OS support C functions 
 */
#include <bsp.h>
#include <config.h>
#include <task.h>
#include <memory.h>
#include <clock.h>

#include "stm32f2xx.h"
#include "core_cm3.h"
#include "stm32f2xx_rcc.h"
#include "stm32f2xx_tim.h"
#include "siprintf.h"
#include "uart.h"
#include "journal.h"

#define MAX_HANDLERS	100

/* the two variables are used in bsp_task_switch_interrupt() function */
uint32_t interrupt_from_task, interrupt_to_task;
static uint32_t isr_table[MAX_HANDLERS];

/* dummy interrupt service function */
static void dummy_isr_fun(int i);
extern char _irq_stack_start[];
extern struct dtask systask[];
extern uint32_t sys_time_calibration;

void dump_buffer(uint8_t *buffer, uint16_t length)
{
	uint16_t i;

	for(i=0; i<length; i++)
	{
		kprintf("%02x ", buffer[i]);
		if(((i+1)&(16-1)) == 0)
			kprintf("\n");
		else if(((i+1)&(8-1)) == 0)
			kprintf("-- ");
	}
	kprintf("\n");
}

void bsp_gettime(uint32_t *tv_sec, uint32_t *tv_nsec)
{
	uint32_t ns;

	*tv_sec = time(NULL);
	/* TIM_GetCounter() return us */
	ns = TIM_GetCounter(TIM2)*1000;
	//*tv_nsec = ns;
	*tv_nsec = (ns >= sys_time_calibration)? (ns-sys_time_calibration):(1000000000-sys_time_calibration+ns);
}


/* Init registers stacked as if auto-saved on exception */
char *bsp_task_init(int t, void (*task)(void *p), void *parm,
	char *stack, void (*killer)(void))
{
	uint32_t *stk;

	stk = (uint32_t *)stack;
	*(stk) = (uint32_t)0x01000000L;/* xPSR */
	*(--stk) = (uint32_t)task;		/* entry point */
	*(--stk) = (uint32_t)killer;	/* r14 (lr) */
	*(--stk) = 0;						/* r12 */
	*(--stk) = 0;						/* r3 */
	*(--stk) = 0;						/* r2 */
	*(--stk) = 0;						/* r1 */
	*(--stk) = (uint32_t)parm;		/* r0 : argument */
	
	*(--stk) = 0;						/* r11 */
	*(--stk) = 0;						/* r10 */
	*(--stk) = 0;						/* r9 */
	*(--stk) = 0;						/* r8 */
	*(--stk) = 0;						/* r7 */
	*(--stk) = 0;						/* r6 */
	*(--stk) = 0;						/* r5 */
	*(--stk) = 0;						/* r4 */

	/* return task's current stack address */
	return (char *)stk;
}

uint32_t get_fp(void)
{
    uint32_t temp;
    __asm__ __volatile__(
			 "mov %0, fp"
			 : "=r" (temp)
			 :
			 : "memory");
    return temp;
}

uint32_t get_lr(void)
{
    uint32_t temp;
    __asm__ __volatile__(
			 "mov %0, lr"
			 : "=r" (temp)
			 :
			 : "memory");
    return temp;
}

uint32_t get_psr(void)
{
	uint32_t temp;
    __asm__ __volatile__(
			 "mrs %0, psr"
			 : "=r" (temp)
			 :
			 : "memory");
    return temp;
}

/* disable irq
 * irq: exception number
 */
void bsp_irq_mask(uint32_t irqno)
{
	if(irqno >= 16)
		NVIC_DisableIRQ((IRQn_Type)(irqno-16));
}

/* enable irq
 * irq: exception number
 */
void bsp_irq_unmask(uint32_t irqno)
{
	if(irqno >= 16)
		NVIC_EnableIRQ((IRQn_Type)(irqno-16));
}

/* attach isr
 * index: exception number
 */
void bsp_isr_attach(uint16_t index, void (*handler)(int n))
{
	if(index < MAX_HANDLERS)
	{
		isr_table[index] = (uint32_t)handler;
	}
}


/* deattach isr
 * index: exception number
 */
void bsp_isr_detach(uint16_t index)
{
	if(index < MAX_HANDLERS)
		isr_table[index] = (uint32_t)dummy_isr_fun;
}


void bsp_esr_attach(uint16_t index, void (*handler)(int n))
{
}

void bsp_esr_detach(uint16_t index)
{
}

/* enable IRQ/FIQ interrupts */
void bsp_int_enable(void)
{
    __enable_irq();
}


/* 
 * disable IRQ/FIQ interrupts
 * returns true if interrupts had been enabled before we disabled them
 */
int bsp_int_disable(void)
{
    __disable_irq();
	return 0;
}

/* dummy interrupt service function */
static void dummy_isr_fun(int i)
{
	kprintf("Unhandled interrupt %d occured!!!\n", i);

    panic("Unhandled interrupt\n");
	
	while(1);
}


enum { r0, r1, r2, r3, r12, lr, pc, psr};


void Hard_Fault_Handler(uint32_t stack[])
{
    /* get LR to detect if MSP or PSP is used */

    kprintf("r0  = 0x%08x\n", stack[r0]);
    kprintf("r1  = 0x%08x\n", stack[r1]);
    kprintf("r2  = 0x%08x\n", stack[r2]);
    kprintf("r3  = 0x%08x\n", stack[r3]);
    kprintf("r12 = 0x%08x\n", stack[r12]);
    kprintf("lr  = 0x%08x\n", stack[lr]);
    kprintf("pc  = 0x%08x\n", stack[pc]);
    kprintf("psr = 0x%08x\n", stack[psr]);

    kprintf ("BFAR=0x%08x, CFSR=0x%08x, HFSR=0x%08x, DFSR=0x%08x, AFSR=0x%08x, MMFAR=0x%08x\n",
                SCB->BFAR, SCB->CFSR, SCB->HFSR, SCB->DFSR, SCB->AFSR, SCB->MMFAR);

    panic("Hard Fault!!");
}

typedef void (*ISR_FUNC)(int);

/*
 * Interrupt priority principles: 
 * Only the group priority determines preemption of interrupt exceptions. When the processor
 * is executing an interrupt exception handler, another interrupt with the same group priority as
 * the interrupt being handled does not preempt the handler,
 * If multiple pending interrupts have the same group priority, the subpriority field determines
 * the order in which they are processed. If multiple pending interrupts have the same group
 * priority and subpriority, the interrupt with the lowest IRQ number is processed first.
 *
 * Set interrupt group priority to 0x07: 
 *   group priority bits = none 
 *   sub priority bits = 4 (16 levels)
 * 
 * Set PendSV interrupt priority to 0:
 *   prevent other interrupts destroy task switching
 */
void interrupt_init()
{
	int32_t i;
	
	/* set group priority none */
	NVIC_SetPriorityGrouping(0x07);

	/* set PENDSV's priority to highest */
	NVIC_SetPriority(PendSV_IRQn, 0); // set prio to 0 will fault, why?

	/* set SysTick's priority to lowest */
	NVIC_SetPriority(SysTick_IRQn, 15);
	
    /* pre-set dummy isr */
	for(i=0; i<MAX_HANDLERS; i++)
		isr_table[i] = (uint32_t)dummy_isr_fun;
}

void isr_default_handler()
{
	uint8_t int_id;
	ISR_FUNC  isr;
	uint32_t f;

	f = bsp_fsave();
	int_id = get_psr();

	sys_interrupt_enter(int_id);
	
	if (int_id < MAX_HANDLERS) {
        isr = (ISR_FUNC)isr_table[int_id];
        if (isr != (ISR_FUNC)0) {
            isr(int_id);
        }
    }
	else
	{
		kprintf("warning: unknown interrupt:%d\n", int_id);
	}

	sys_interrupt_exit(int_id);
	bsp_frestore(f);
}


/*
 * fp: top of innerest stack
 * low: low value of stack
 * high: high vlaue of stack
 */
void dump_call_stack(uint32_t fp, uint32_t low, uint32_t high)
{
#if 0 // not valid in cortex, only can be used in arm7/9
	uint32_t *pStack, lr, loops = 0;

	kprintf("calling stacks:\n");
	while(fp>=low && fp<=high && loops <20)
	{
		/* {fp, ip, lr, pc} */
		pStack = (uint32_t *)(fp-12);
		//pc = pStack[3];
		lr = pStack[2];
		//ip = pStack[1];
		fp = pStack[0];
		kprintf("%d -- 0x%x \n", loops, lr);
		loops ++;
	}
#endif
}

/* stack dump and information dump
 * for stack dump, a common rules is:
 * 0x2000xxxx (r7) 0x0000xxxx(lr), r7 will in stack and lr will in flash.
 * usually 12th long word is the address which calls panic().
 */
void panic(char *infostr)
{
	uint32_t sp, fp, psr;
	uint32_t size, i;
	bsp_fsave();
	
    psr = get_psr();
    fp = get_fp();

	kprintf("PANIC psr=0x%08x fp=0x%08x: %s\n", psr, fp, infostr);

	if(get_psr() & 0xFF){
		/* in exception context, dump exception stack */
		sp = __get_MSP();
		if((sp>(uint32_t)_irq_stack_start) && (sp<(uint32_t)_irq_stack_start+1024))
		{
			size = (uint32_t)_irq_stack_start+1024-sp;
			kprintf("exception stacks: sp=0x%x depth=%d bytes\n", sp, size);
			dump_buffer((uint8_t *)sp, size);
		}
		else
			kprintf("broken MSP: 0x%x\n", sp);
	}

	
	if((current>=&systask[0]) && (current<&systask[MAX_TASK_NUMBER]))
	{
		/* dump task stack */
		sp = __get_PSP();
		if((sp>(uint32_t)current->stack_base) && (sp<(uint32_t)current->stack_base+current->stack_size))
		{
			size = (uint32_t)current->stack_base+current->stack_size-sp;
			kprintf("task stacks: sp=0x%x depth=%d bytes\n", sp, size);
			dump_buffer((uint8_t *)sp, size);
		}
		else
			kprintf("broken PSP: 0x%x\n", sp);

		/* dump current task info */
		kprintf("current=0x%x last sp=0x%x stack_base=0x%x taskname=%s state=%d taskq=0x%x\n", 
			current, current->sp, current->stack_base, current->name, current->state, current->taskq);
	}
	else
		kprintf("current is overwriten! current=0x%x\n", current);
	

	/* dump task info */
    for(i=0; i<MAX_TASK_NUMBER; i++)
    {
        if(systask[i].state != TASK_STATE_DEAD)
        {
			kprintf("Task %d (%8s): sp=0x%08x taskq=0x%08x state8_prio8_pri_origin8_flags8=0x%08x stack=(0x%08x %d)\n", 
                    i, systask[i].name, systask[i].sp, systask[i].taskq,
                    (systask[i].state<<24)|(systask[i].priority<<16)|(systask[i].pri_origin<<8)|(systask[i].flags),
                    systask[i].stack_base, systask[i].stack_size);
        }
    }

	/* dump memory usage */
	memory_dump();

	/* dump journal and counters */
	journal_dump();
	pmc_dump();
	
    while(1);
}

void  systick_init (void)
{
    uint32_t  cnts;
	RCC_ClocksTypeDef  rcc_clocks;

    RCC_GetClocksFreq(&rcc_clocks);

    cnts = rcc_clocks.HCLK_Frequency / TICKS_PER_SECOND;

	SysTick->LOAD = (cnts - 1);
	SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
}

int kprint_outchar(printdev_t *dev, char c)
{
	uart_putc_ex(SERIAL_UART_PORT, c);
	return 1;
}


