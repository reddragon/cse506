#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>
#include <kern/pci.h>
#include <kern/pcireg.h>
#include <kern/pmap.h>
#include <kern/e100.h>
#include <inc/mmu.h>
#include <inc/memlayout.h>
// LAB 6: Your driver code here

// The IRQ line for E100
static struct pci_func e100_info;

// SCR registers
#define SCB_SW (e100_info.reg_base[1] + 0x0)
#define SCB_CW_LO (e100_info.reg_base[1] + 0x2)
#define SCB_CW_HI (e100_info.reg_base[1] + 0x3)
#define SCB_GP (e100_info.reg_base[1] + 0x4)
#define SCB_PORT (e100_info.reg_base[1] + 0x8)

// Transmission CBL in main memory
struct cbl tx_cbl[TX_LIMIT];
int cur_tx_offset;
// The helpful folks on TLPD say that reading from port 0x80 should take
// around 1 us. So be it. 
void udelay(int us)
{
	int i;
	for(i = 0; i < us; i++)
	{
		inb(0x80);
	}
}
void e100_delay()
{
	// Just wait for 20 us
	udelay(20);
}
void e100_wait_for_complete(int offset)
{
	while(!(tx_cbl[offset].status & CBL_COMPLETE))
	{
		udelay(5);
	}
}
int e100_transmit(void* va, int size)
{
	cprintf("\t\t\t\t\t\t IN E100 TRANSMIT, %x %x\n", va, size);
	cprintf("%x %x %x\n", tx_cbl[cur_tx_offset].status, tx_cbl[cur_tx_offset].cmd, tx_cbl[cur_tx_offset].link);
	// Wait till the last packet is transmitted
	e100_wait_for_complete(cur_tx_offset);
	tx_cbl[cur_tx_offset].cmd = CBL_TX | CBL_SUSPEND;
	tx_cbl[cur_tx_offset].status = 0;
	tx_cbl[cur_tx_offset].cmd_data.tx.tcb_byte_count = size;
	memmove(&tx_cbl[cur_tx_offset].cmd_data.tx.data, va, size);

	// The device is suspended, so resume it. 
	outb(SCB_CW_LO, CUC_RESUME);
	return 0;
}
int e100_attachfn(struct pci_func * pcif) 
{
	cur_tx_offset = 0;
	int i;
	memset(tx_cbl, 0, sizeof(tx_cbl));
	pci_func_enable(pcif);
	e100_info = *pcif;
	cprintf("e100_info reg 1 : %x\n", e100_info.reg_base[1]);
	// Resetting the NIC
	outl(SCB_PORT, 0);
	// Wait for 20us FFS
	e100_delay();
	// Create the CBL list
	for(i = 0; i < TX_LIMIT; i++)
	{
		tx_cbl[i].link = PADDR(&tx_cbl[(i+1)%TX_LIMIT]);
		tx_cbl[i].cmd_data.tx.tbd_array_addr = 0xFFFFFFFF;
		tx_cbl[i].cmd_data.tx.tcb_byte_count = 1518;
		tx_cbl[i].cmd_data.tx.tbd_thrs = 0xE0;
		tx_cbl[i].cmd |= CBL_SUSPEND;
	}
	// Disable interrupts
	outb(SCB_CW_HI, CUC_INT_DISABLE);

	// Load CU base with 0
	outl(SCB_GP, 0x0);
	outb(SCB_CW_LO, CUC_LOAD_CU);
	// Start it
	outl(SCB_GP, PADDR(&tx_cbl[0]));
	outb(SCB_CW_LO, CUC_START);

	// This should execute the first CBL which has a NOOP as of now and then suspend the device
	return 0;
}
