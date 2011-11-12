#include<kern/pci.h>
#include<inc/stdio.h>
// LAB 6: Your driver code here

// The IRQ line for E100
static uint8_t e100_irq_line;
// The three base & size pairs as described in section 4.1.10
// CSR Memory Mapped Base Address & Size
// CSR I/O Mapped Base Address & Size
// Flash Memory Mapped Base Address & Size
static uint32_t e100_reg_base[3], e100_reg_size[3];

int
e100_attachfn(struct pci_func * pcif) {
	//cprintf("In the attach function\n");
	pci_func_enable(pcif);
	//cprintf("E100 now on IRQ Line: %d\n", pcif->irq_line);
	e100_irq_line = pcif->irq_line;
	int i;
	for(i = 0; i < 3; i++)
	{
		//cprintf("reg_base[%d]: %x  reg_base[%d], %x\n", i, pcif->reg_base[i], i, pcif->reg_size[i]);
		e100_reg_base[i] = pcif->reg_base[i];
		e100_reg_size[i] = pcif->reg_size[i];
	}
	return 0;
}
