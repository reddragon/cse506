/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */

TRAPHANDLER_NOEC(handler0, T_DIVIDE);
/*
		mov %cs, %ebx;
	ret
*/
/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	/* Getting the trap number */
	pop %eax;
	/* Pushing all registers */
	pushal;
	push %es;
	push $0x0;
	push %ds;
	push $0x0;
	/* Trap Number */
	push %eax;
	/* push %err; */
	push $0x0;
	/* push %eip; */
	push $0x0;
	push %cs;
	push $0x0;
	/* To push eflags. TODO check if this works */
	pushf;
	push %esp;
	push %ss;
	push $0x0;
	pushl %esp;
	/* Dummy call*/
	call trap;
