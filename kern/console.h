/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>

#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

/* #define-s for colors */

#define BLACK 		0x0
#define BLUE 		0x1
#define GREEN		0x2
#define CYAN		0x3
#define RED		0x4
#define MAGENTA		0x5
#define BROWN 		0x6
#define LIGHTGRAY	0x9
#define LIGHTGREEN	0xA
#define LIGHTCYAN	0xB
#define LIGHTRED	0xC
#define LIGHTMAGENTA	0xD
#define YELLOW		0xE
#define WHITE		0xF

/* #define-s for colors */

void cons_init(void);
int cons_getc(void);
void cga_setcolor(int,int);

void kbd_intr(void); // irq 1
void serial_intr(void); // irq 4

#endif /* _CONSOLE_H_ */
