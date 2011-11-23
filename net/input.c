#include "ns.h"
#include <inc/x86.h>
#include <inc/lib.h>
#define MAX_DATA 1518
extern union Nsipc nsipcbuf;
uint8_t data[MAX_DATA];
uint16_t size;
#define SVA REQVA-PGSIZE
struct jif_pkt *pkt = (struct jif_pkt*)SVA;
static void udelay(int us)
{
	int i;
	for(i = 0; i < us; i++)
	{
		inb(0x80);
	}
}
void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int status = 0;
	int cnt1 = 0;
	while(cnt1 == 0)
	{
		int cnt = 0;
		size = 0;
		while((status = sys_net_recv(&data, &size)) < 0)
		{
			sys_yield();
			//udelay(1000000);
		}
		if(size == 0)
			continue;
		cprintf("Now sending stufFFFFFFFFFFFFFFFFFF %x\n", size);
		if((status = sys_page_alloc(0, pkt, PTE_P|PTE_U|PTE_W)) < 0)
		{
			continue;
		}
		memmove(pkt->jp_data, data, size);
		pkt->jp_len = size;
		// Send to the network server
		ipc_send(ns_envid, NSREQ_INPUT, pkt, PTE_P|PTE_W|PTE_U);
		// Wait for 10 us 
		udelay(100);
		sys_page_unmap(0, pkt);
	}
}
