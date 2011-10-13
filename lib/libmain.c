// Called from entry.S to get us going.
// entry.S already took care of defining envs, pages, vpd, and vpt.

#include <inc/lib.h>

extern void umain(int argc, char **argv);

volatile struct Env *env;
char *binaryname = "(PROGRAM NAME UNKNOWN)";

void
libmain(int argc, char **argv)
{
	// set env to point at our env structure in envs[].
	// LAB 3: Your code here.
	//cprintf("Current environment id: %d\n", sys_getenvid());
	//panic("Values %x %x\n", sys_getenvid(), ENVX(sys_getenvid()));
	uint32_t env_id = sys_getenvid();
	//panic("Value %x\n", env_id);
	env = &envs[ENVX(env_id)];
	// save the name of the program so that panic() can use it
	if (argc > 0)
		binaryname = argv[0];

	// call user main routine
	umain(argc, argv);

	// exit gracefully
	exit();
}

