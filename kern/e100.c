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
struct cbl rx_rfa[RX_LIMIT];
int cur_tx_offset;
int cur_rx_offset;
// The helpful folks on TLPD say that reading from port 0x80 should take
// around 1 us. So be it. 
static void udelay(int us)
{
	int i;
	for(i = 0; i < us; i++)
	{
		inb(0x80);
	}
}
static void
hexdump(const char *prefix, const void *data, int len)
{
	int i;
	char buf[80];
	char *end = buf + sizeof(buf);
	char *out = NULL;
	for (i = 0; i < len; i++) {
		if (i % 16 == 0)
			out = buf + snprintf(buf, end - buf,
					     "%s%04x   ", prefix, i);
		out += snprintf(out, end - out, "%02x", ((uint8_t*)data)[i]);
		if (i % 16 == 15 || i == len - 1)
			cprintf("%.*s\n", out - buf, buf);
		if (i % 2 == 1)
			*(out++) = ' ';
		if (i % 16 == 7)
			*(out++) = ' ';
	}
}
void e100_delay()
{
	// Just wait for 20 us
	udelay(20);
}
int e100_wait_for_complete(int offset)
{
	if(!(tx_cbl[offset].status & CBL_COMPLETE))
	{
		//udelay(5);
		return -1;
	}
	return 0;
}
int e100_transmit(void* va, int size)
{
	cprintf("\t\t\t\t\t\t IN E100 TRANSMIT, %x %x\n", va, size);
	cprintf("%x %x %x\n", tx_cbl[cur_tx_offset].status, tx_cbl[cur_tx_offset].cmd, tx_cbl[cur_tx_offset].link);
	
	// Wait till the last packet is transmitted
	if(e100_wait_for_complete(cur_tx_offset) != 0)
		return -1;
	tx_cbl[cur_tx_offset].cmd = CBL_TX | CBL_SUSPEND;
	tx_cbl[cur_tx_offset].status = 0;
	tx_cbl[cur_tx_offset].cmd_data.tx.tcb_byte_count = size;
	memmove(&tx_cbl[cur_tx_offset].cmd_data.tx.data, va, size);

	// The device is suspended, so resume it. 
	outb(SCB_CW_LO, CUC_RESUME);
	return 0;
}
int e100_recv(void* va, uint16_t* size)
{
	//cprintf("\t\t\t\t\t\t\t IN E100 RECEIVE\n");
	//cprintf("%x %x %x %x\n", rx_rfa[cur_rx_offset].status, rx_rfa[cur_rx_offset].cmd, rx_rfa[cur_rx_offset].cmd_data.rx.actual_count, rx_rfa[cur_rx_offset].cmd_data.rx.size);
	if(!(rx_rfa[cur_rx_offset].status & CBL_COMPLETE) || rx_rfa[cur_rx_offset].cmd_data.rx.actual_count <= 0 )
	{
		return -1;
	}
	*size = rx_rfa[cur_rx_offset].cmd_data.rx.actual_count & RUC_ACT_MASK;
	hexdump("e100 input: ", rx_rfa[cur_rx_offset].cmd_data.rx.data, *size);
	memmove(va, rx_rfa[cur_rx_offset].cmd_data.rx.data, *size);
	rx_rfa[cur_rx_offset].cmd_data.rx.actual_count = 0;
	rx_rfa[cur_rx_offset].status = 0;
	//rx_rfa[cur_rx_offset].cmd = CBL_SUSPEND;
	cprintf("\t\t\t\t\t E100 RECV returning SIZE %x\n", *size);
	if(inw(SCB_SW) & 0x4)
	{
		cprintf("\t\t\t\t\t\t\t Resuming RU\n");
		outb(SCB_CW_LO, RUC_RESUME);
	}
	//cur_rx_offset = !cur_rx_offset;
	return *size;
}
int e100_attachfn(struct pci_func * pcif) 
{
	cur_tx_offset = 0;
	cur_rx_offset = 0;
	int i;
	memset(tx_cbl, 0, sizeof(tx_cbl));
	memset(rx_rfa, 0, sizeof(rx_rfa));
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
	for(i = 0; i < RX_LIMIT; i++)
	{
		rx_rfa[i].link = PADDR(&rx_rfa[(i+1)%RX_LIMIT]);
		rx_rfa[i].cmd_data.rx.size = 1518;
		rx_rfa[i].cmd_data.rx.reserved = 0xFFFFFFFF;
		rx_rfa[i].cmd |= CBL_SUSPEND ;
	}
	// Disable interrupts
	outb(SCB_CW_HI, CUC_INT_DISABLE);

	// Load CU base with 0
	outl(SCB_GP, 0x0);
	outb(SCB_CW_LO, CUC_LOAD_CU);
	e100_delay();
	// Start it
	// This should execute the first CBL which has a NOOP as of now and then suspend the device
	outl(SCB_GP, PADDR(&tx_cbl[0]));
	outb(SCB_CW_LO, CUC_START);

	e100_delay();
	// Load RU base with 0
	outl(SCB_GP, 0x0);
	outb(SCB_CW_LO, RUC_LOAD_RU);

	e100_delay();
	// Start it
	outl(SCB_GP, PADDR(&rx_rfa[0]));
	outb(SCB_CW_LO, RUC_START);
	e100_delay();
	return 0;
}
