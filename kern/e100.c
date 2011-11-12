#include<kern/pci.h>
#include<inc/stdio.h>
// LAB 6: Your driver code here

int
e100_attachfn(struct pci_func * pcif) {
	cprintf("In the attach function\n");
	pci_func_enable(pcif);
	cprintf("E100 now on IRQ Line: %d\n", pcif->irq_line);
	int i;
	for(i = 0; i < 6; i++)
		cprintf("reg_base[%d]: %x  ", i, pcif->reg_base[i]);
	cprintf("\n");
	for(i = 0; i < 6; i++)
		cprintf("reg_size[%d]: %x  ", i, pcif->reg_size[i]);
	cprintf("\n");
	// TODO Save the IO Ports and IRQ data in static vars
	return 0;
}
