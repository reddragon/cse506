#include "ns.h"
#include <inc/lib.h>
extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	int status = 0;
	envid_t from;
	int perms;
	cprintf("In output\n");
	// LAB 6: Your code here:
	// 	- read a packet from the network server
	while(1)
	{
		if((status = ipc_recv(&from, &nsipcbuf, &perms)) < 0)
		{
			cprintf("net/output.c ipc_recv failed\n");
			return;
		}
		cprintf("net/output.c : received size %x\n", nsipcbuf.pkt.jp_len);
		int i;
		for(i = 0; i < nsipcbuf.pkt.jp_len; i++)
		{
			cprintf("%x ", (int)nsipcbuf.pkt.jp_data[i]);
		}
		cprintf("\n");
		//	- send the packet to the device driver

		while((status = sys_net_send((void*)nsipcbuf.pkt.jp_data, (uint32_t) nsipcbuf.pkt.jp_len)) < 0)
		{
			sys_yield();
			//cprintf("net/output.c sys_net_send failed\n");
			// return;
		}
	}
}
