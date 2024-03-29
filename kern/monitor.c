// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Displays the stack backtrace", mon_backtrace },
	{ "debug", "Displays data as helpful in debugging", mon_debug },
	{ "alloc_page", "Allocates a page", mon_alloc_page },
	{ "page_status", "Displays the current allocation status of a page", mon_page_status },
	{ "free_page", "Frees an allocated page", mon_free_page }
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();
void cga_setcolor();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	cprintf("Stack backtrace:\n");
	uint32_t ebp, eip;
	int counter;
	struct Eipdebuginfo info;
	char fn_name[128];
	ebp = read_ebp();
	eip = *((uint32_t *) (ebp + sizeof(uint32_t)));

	while(ebp != 0)
	{
		cprintf("  ebp %x eip %x args",ebp,eip);
		for(counter = 1; counter <= 5; counter++)
			cprintf(" %08x", *((uint32_t *) (ebp + (counter + 1)*sizeof(uint32_t))));
		debuginfo_eip((uintptr_t)eip,&info); 
		strcpy(fn_name,info.eip_fn_name);
		// Removing the parts after the colon
		fn_name[(int)info.eip_fn_namelen]='\0';
		cprintf("\n\t%s:%u: %s+%u\n",info.eip_file, info.eip_line, fn_name, (eip-info.eip_fn_addr));	
		ebp = *((uint32_t *) (ebp));
		eip = *((uint32_t *) (ebp + sizeof(uint32_t)));
	}
	return 0;
}

int 
mon_debug()
{
	/* Temporarily used for demonstrating using colored text
	   For the purpose of Challenge Problem in Lab 1 */
	cga_setcolor(LIGHTGREEN,WHITE);	
	cprintf("A colorful Hello World! :-)\n");
	return 0;
}

int
is_page_free(struct Page *p) 
{
	if(p->pp_link.le_next == NULL && p->pp_link.le_prev == NULL)
		return 0;
	return 1;
}

int
mon_alloc_page()
{
	/* Use the page_alloc() function to allocate a page 
	   For the purpose of Challenge Problem in Lab 2*/
	struct Page * p;
	if(page_alloc(&p) < 0) {
		cprintf("Error: Could not allocate page.\n");
		return 1;
	}
	cprintf("Page 0x%x Allocated\n",p-pages);
	return 0;
}

int 
parse_pagenum(char *str)
{
	// Allowing page number to be either a decimal or a hex
	int base = 16, cur_multiplier = 1;
	int pn = 0;
	if(!(strlen(str) > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')))
		base = 10;
	
	int i;
	for(i = strlen(str) - 1; (base == 10 && i >= 0) || (base == 16 && i >= 2); i--) {
		if(str[i] >= '0' && str[i] <= '9') {
				pn += (str[i] - '0') * cur_multiplier;
				cur_multiplier *= base;
				continue;
		}
		if(base == 16) {
			int pos = 0;
			if(str[i] >= 'a' && str[i] <= 'f') 
				pos = 'a' - 10;
			if(str[i] >= 'A' && str[i] <= 'F')
				pos = 'A' - 10;
			pn += (str[i] - pos) * cur_multiplier;
			cur_multiplier *= base;
			continue;
		}
	}
	return pn;
}
	
int
mon_page_status(int argc, char **argv, struct Trapframe *tf)
{
	/* For the purpose of Challeng Problem in Lab 2 */
	int page_num = parse_pagenum(argv[1]);
	struct Page * query_page = pages + page_num;
	cprintf("Page 0x%x is ", page_num);
	switch(is_page_free(query_page)) {
		case 0:
			cprintf("Allocated\n");
			break;
		case 1:
			cprintf("Free\n");
			break;
	}
	return 0;
}

int 
mon_free_page(int argc, char **argv, struct Trapframe *tf)
{
	/* For the purpose of Challenge Problem in Lab 2 */
	int page_num = parse_pagenum(argv[1]);
	struct Page * query_page = pages + page_num;
	if(is_page_free(query_page))
		cprintf("Page 0x%x is already free\n", page_num);
	else {
		page_free(query_page);
		cprintf("Page 0x%x freed\n", page_num);
	}
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
