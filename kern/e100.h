#ifndef JOS_KERN_E100_H
#define JOS_KERN_E100_H

#include <kern/pci.h>
#define TX_LIMIT 1
#define RX_LIMIT 1
#define MAX_DATA 1518 // Ethernet tch tch. 

#define CUC_INT_DISABLE 0x1
#define CUC_LOAD_CU 0x60
#define CUC_START 0x10
#define CUC_RESUME 0x20

#define RUC_LOAD_RU 0x06
#define RUC_START 0x1
#define RUC_RESUME 0x2
#define RUC_ACT_MASK 0x3FFF
#define RUC_EOF 0x8000

#define CBL_LAST 0x8000
#define CBL_SUSPEND 0x4000
#define CBL_COMPLETE 0x8000
#define CBL_TX 0x4
int e100_attachfn(struct pci_func * pcif);
int e100_transmit(void* va, int size);
int e100_recv(void* va, uint16_t *size);
// Structs
struct tx_data
{
	uint32_t tbd_array_addr;
	uint16_t tcb_byte_count;
	uint8_t tbd_count;
	uint8_t tbd_thrs;
	uint8_t data[MAX_DATA];
}__attribute__((packed));

struct rx_data
{
	uint32_t reserved;
	volatile uint16_t actual_count;
	uint16_t size;
	uint8_t data[MAX_DATA];
}__attribute__((packed));

struct cbl
{
	volatile uint16_t status;
	uint16_t cmd;
	uint32_t link;
	union
	{
		struct tx_data tx;
		struct rx_data rx;
	} cmd_data;
}__attribute__((packed));

#endif	// JOS_KERN_E100_H
