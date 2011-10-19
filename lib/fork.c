// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	cprintf("In the handler of pgfault with va:%x\n", addr);
	if(!((err & FEC_WR) && (vpt[VPN(addr)] & PTE_COW)))
		panic("Incorrectly entered in pagefault entered. Fault va: %p, err: %d, perm: %d, VPN(addr): %d, addr>>PGSHIFT: %d\n", \
			addr, err, PTE_COW, vpt[VPN(addr)], (uint32_t)addr>>PGSHIFT);
	

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	// Allocate a new page at PFTEMP
	sys_page_alloc(env->env_id, (void *)PFTEMP, PTE_W | PTE_U | PTE_P);
	// Copy the old contents into the new page
	memmove((void *)PFTEMP, (void *)ROUNDDOWN((uint32_t)addr, PGSIZE), PGSIZE);	
	// Now map the page to the 
	sys_page_map(env->env_id, (void *)PFTEMP, \
		env->env_id, (void *)ROUNDDOWN((uint32_t)addr,PGSIZE), PTE_W | PTE_U | PTE_P);		

	sys_page_unmap(env->env_id, PFTEMP);
	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	int perm = vpt[pn] & PTE_USER;

	// LAB 4: Your code here.
	if((perm & PTE_COW) || (perm & PTE_W))
	{
		
		cprintf("In duppage, envid: %d, addr: %x, vpt[n]&PTE_COW:%d, vpt[n]&PTE_W:%d, vpt[n]&PTE_U:%d, perm:%d\n", envid, pn<<PGSHIFT, vpt[pn]&PTE_COW, vpt[pn]&PTE_W, vpt[pn]&PTE_U, perm);
		perm &= ~PTE_W;
		perm |= PTE_COW;
		if((r=sys_page_map(0, (void *)(pn<<PGSHIFT), \
			envid, (void *)(pn<<PGSHIFT), perm))<0)
			return r;
		if((r=sys_page_map(0, (void *)(pn<<PGSHIFT), \
			0, (void *)(pn<<PGSHIFT), perm))<0)
			return r;
	}
	else
	{
		
	cprintf("In duppage, envid: %d, addr: %x, vpt[n]&PTE_COW:%d, vpt[n]&PTE_W:%d, vpt[n]&PTE_U:%d, perm:%d\n", envid, pn<<PGSHIFT, vpt[pn]&PTE_COW, vpt[pn]&PTE_W, vpt[pn]&PTE_U, perm);
		if((r=sys_page_map(env->env_id, (void *)(pn<<PGSHIFT), \
			envid, (void *)(pn<<PGSHIFT), perm))<0)
			return r;
	}
		
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	set_pgfault_handler(pgfault);
	envid_t envid;
	int r;
	envid = sys_exofork();
	if(envid < 0)
		panic("sys_exofork: %e", envid);
	
	else if(envid == 0)
	{
		env = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	
	sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_P);
	sys_env_set_pgfault_upcall(envid, env->env_pgfault_upcall);	
	uint32_t pn;
	for(pn = UTEXT; pn < UTOP; pn += PGSIZE)
		if(pn == UXSTACKTOP - PGSIZE) 
		{
			if((r = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_W | PTE_U))<0)
				panic("sys_page_alloc: %e", r);
		}
		else if((vpd[(pn>>PGSHIFT)/NPTENTRIES] & PTE_P) && (vpt[(pn>>PGSHIFT)] & PTE_P))
			duppage(envid, pn>>PGSHIFT);
	
	if((r = sys_env_set_pgfault_upcall(envid, env->env_pgfault_upcall))<0)
		panic("sys_env_set_pgfault_upcall: %e", r);

	if((r = sys_env_set_status(envid, ENV_RUNNABLE))<0)
		panic("sys_env_status: %e", r);

	// Returning the child's envid to the parent
	return envid;
	//panic("fork not implemented");
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
