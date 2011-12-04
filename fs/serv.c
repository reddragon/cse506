/*
 * File system server main loop -
 * serves IPC requests from other environments.
 */

#include <inc/x86.h>
#include <inc/string.h>

#include "fs.h"


#define debug 1

// The file system server maintains three structures
// for each open file.
//
// 1. The on-disk 'struct File' is mapped into the part of memory
//    that maps the disk.  This memory is kept private to the file
//    server.
// 2. Each open file has a 'struct Fd' as well, which sort of
//    corresponds to a Unix file descriptor.  This 'struct Fd' is kept
//    on *its own page* in memory, and it is shared with any
//    environments that have the file open.
// 3. 'struct OpenFile' links these other two structures, and is kept
//    private to the file server.  The server maintains an array of
//    all open files, indexed by "file ID".  (There can be at most
//    MAXOPEN files open concurrently.)  The client uses file IDs to
//    communicate with the server.  File IDs are a lot like
//    environment IDs in the kernel.  Use openfile_lookup to translate
//    file IDs to struct OpenFile.

struct OpenFile {
	uint32_t o_fileid;	// file id
	struct File *o_file;	// mapped descriptor for open file
	int o_mode;		// open mode
	struct Fd *o_fd;	// Fd page
};

// Max number of open files in the file system at once
#define MAXOPEN		1024
#define FILEVA		0xD0000000

// initialize to force into data section
struct OpenFile opentab[MAXOPEN] = {
	{ 0, 0, 1, 0 }
};

// Virtual address at which to receive page mappings containing client requests.
union Fsipc *fsreq = (union Fsipc *)0x0ffff000;

void
serve_init(void)
{
	int i;
	uintptr_t va = FILEVA;
	for (i = 0; i < MAXOPEN; i++) {
		opentab[i].o_fileid = i;
		opentab[i].o_fd = (struct Fd*) va;
		va += PGSIZE;
	}
}

// Allocate an open file.
int
openfile_alloc(struct OpenFile **o)
{
	int i, r;

	// Find an available open-file table entry
	for (i = 0; i < MAXOPEN; i++) {
		switch (pageref(opentab[i].o_fd)) {
		case 0:
			if ((r = sys_page_alloc(0, opentab[i].o_fd, PTE_P|PTE_U|PTE_W)) < 0)
				return r;
			/* fall through */
		case 1:
			opentab[i].o_fileid += MAXOPEN;
			*o = &opentab[i];
			memset(opentab[i].o_fd, 0, PGSIZE);
			return (*o)->o_fileid;
		}
	}
	return -E_MAX_OPEN;
}

// Look up an open file for envid.
int
openfile_lookup(envid_t envid, uint32_t fileid, struct OpenFile **po)
{
	struct OpenFile *o;

	o = &opentab[fileid % MAXOPEN];
	if (pageref(o->o_fd) == 1 || o->o_fileid != fileid)
		return -E_INVAL;
	*po = o;
	return 0;
}

// Open req->req_path in mode req->req_omode, storing the Fd page and
// permissions to return to the calling environment in *pg_store and
// *perm_store respectively.
int
serve_open(envid_t envid, struct Fsreq_open *req,
	   void **pg_store, int *perm_store)
{
	char path[MAXPATHLEN];
	struct File *f;
	int fileid;
	int r;
	struct OpenFile *o;

	if (debug)
		cprintf("serve_open %08x %s 0x%x\n", envid, req->req_path, req->req_omode);

	// Copy in the path, making sure it's null-terminated
	memmove(path, req->req_path, MAXPATHLEN);
	path[MAXPATHLEN-1] = 0;

	// Find an open file ID
	if ((r = openfile_alloc(&o)) < 0) {
		if (debug)
			cprintf("openfile_alloc failed: %e", r);
		return r;
	}
	fileid = r;

	// Open the file
	if (req->req_omode & O_CREAT) {
		if ((r = file_create(path, &f)) < 0) {
			if (!(req->req_omode & O_EXCL) && r == -E_FILE_EXISTS)
				goto try_open;
			if (debug)
				cprintf("file_create failed: %e", r);
			return r;
		}
	} else {
try_open:
		if ((r = file_open(path, &f)) < 0) {
			if (debug)
				cprintf("file_open failed: %e", r);
			return r;
		}
	}

	// Truncate
	if (req->req_omode & O_TRUNC) {
		if ((r = file_set_size(f, 0)) < 0) {
			if (debug)
				cprintf("file_set_size failed: %e", r);
			return r;
		}
	}

	// Save the file pointer
	o->o_file = f;

	// Fill out the Fd structure
	o->o_fd->fd_file.id = o->o_fileid;
	o->o_fd->fd_omode = req->req_omode & O_ACCMODE;
	o->o_fd->fd_dev_id = devfile.dev_id;
	o->o_mode = req->req_omode;

	if (debug)
		cprintf("sending success, page %08x\n", (uintptr_t) o->o_fd);

	// Share the FD page with the caller
	*pg_store = o->o_fd;
	*perm_store = PTE_P|PTE_U|PTE_W;
	return 0;
}

// Set the size of req->req_fileid to req->req_size bytes, truncating
// or extending the file as necessary.
int
serve_set_size(envid_t envid, struct Fsreq_set_size *req)
{
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_set_size %08x %08x %08x\n", envid, req->req_fileid, req->req_size);

	// Every file system IPC call has the same general structure.
	// Here's how it goes.

	// First, use openfile_lookup to find the relevant open file.
	// On failure, return the error code to the client with ipc_send.
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	// Second, call the relevant file system function (from fs/fs.c).
	// On failure, return the error code to the client.
	return file_set_size(o->o_file, req->req_size);
}

// Read at most ipc->read.req_n bytes from the current seek position
// in ipc->read.req_fileid.  Return the bytes read from the file to
// the caller in ipc->readRet, then update the seek position.  Returns
// the number of bytes successfully read, or < 0 on error.
int
serve_read(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;

	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Look up the file id, read the bytes into 'ret', and update
	// the seek position.  Be careful if req->req_n > PGSIZE
	// (remember that read is always allowed to return fewer bytes
	// than requested).  Also, be careful because ipc is a union,
	// so filling in ret will overwrite req.
	//
	// Hint: Use file_read.
	// Hint: The seek position is stored in the struct Fd.
	// LAB 5: Your code here
	int fd = req -> req_fileid;
	size_t n = MIN(req -> req_n, PGSIZE);
	struct OpenFile* o;
	int status;
	if((status = openfile_lookup(envid, fd, &o)) < 0)
		return status;
	cprintf("opened : %x %x %x\n",o, o->o_fd, ret->ret_buf);
	off_t offset = o->o_fd->fd_offset;
	if((status = file_read(o->o_file, (void*)ret -> ret_buf, n, offset)) < 0)
	{
		cprintf("file_read : error now\n");
		// Kind of redundant, but useful for debugging
		return status;
	}
	o->o_fd->fd_offset += status;
	return status;
	//panic("serve_read not implemented");
}

// Write req->req_n bytes from req->req_buf to req_fileid, starting at
// the current seek position, and update the seek position
// accordingly.  Extend the file if necessary.  Returns the number of
// bytes written, or < 0 on error.
int
serve_write(envid_t envid, struct Fsreq_write *req)
{
	if (debug)
		cprintf("serve_write %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// LAB 5: Your code here.
	int fd = req -> req_fileid;
	size_t n = MIN(req -> req_n, PGSIZE);
	struct OpenFile* o;
	int status;
	if((status = openfile_lookup(envid, fd, &o)) < 0)
		return status;
	off_t offset = o->o_fd->fd_offset;
	if((status = file_write(o->o_file, (void*)req -> req_buf, n, offset)) < 0)
	{
		// Kind of redundant, but useful for debugging
		return status;
	}
	o->o_fd->fd_offset += status;
	return status;

	//panic("serve_write not implemented");
}

// Stat ipc->stat.req_fileid.  Return the file's struct Stat to the
// caller in ipc->statRet.
int
serve_stat(envid_t envid, union Fsipc *ipc)
{
	struct Fsreq_stat *req = &ipc->stat;
	struct Fsret_stat *ret = &ipc->statRet;
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_stat %08x %08x\n", envid, req->req_fileid);

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;

	strcpy(ret->ret_name, o->o_file->f_name);
	ret->ret_size = o->o_file->f_size;
	ret->ret_isdir = (o->o_file->f_type == FTYPE_DIR);
	return 0;
}

// Flush all data and metadata of req->req_fileid to disk.
int
serve_flush(envid_t envid, struct Fsreq_flush *req)
{
	struct OpenFile *o;
	int r;

	if (debug)
		cprintf("serve_flush %08x %08x\n", envid, req->req_fileid);

	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
	file_flush(o->o_file);
	return 0;
}

// Remove the file req->req_path.
int
serve_remove(envid_t envid, struct Fsreq_remove *req)
{
	char path[MAXPATHLEN];
	int r;

	if (debug)
		cprintf("serve_remove %08x %s\n", envid, req->req_path);

	// Delete the named file.
	// Note: This request doesn't refer to an open file.

	// Copy in the path, making sure it's null-terminated
	memmove(path, req->req_path, MAXPATHLEN);
	path[MAXPATHLEN-1] = 0;

	// Delete the specified file
	return file_remove(path);
}

// Sync the file system.
int
serve_sync(envid_t envid, union Fsipc *req)
{
	fs_sync();
	return 0;
}

typedef int (*fshandler)(envid_t envid, union Fsipc *req);

fshandler handlers[] = {
	// Open is handled specially because it passes pages
	/* [FSREQ_OPEN] =	(fshandler)serve_open, */
	[FSREQ_SET_SIZE] =	(fshandler)serve_set_size,
	[FSREQ_READ] =		serve_read,
	[FSREQ_WRITE] =		(fshandler)serve_write,
	[FSREQ_STAT] =		serve_stat,
	[FSREQ_FLUSH] =		(fshandler)serve_flush,
	[FSREQ_REMOVE] =	(fshandler)serve_remove,
	[FSREQ_SYNC] =		serve_sync
};
#define NHANDLERS (sizeof(handlers)/sizeof(handlers[0]))

void
serve(void)
{
	uint32_t req, whom;
	int perm, r;
	void *pg;

	while (1) {
		perm = 0;
		req = ipc_recv((int32_t *) &whom, fsreq, &perm);
		if (debug)
			cprintf("fs req %d from %08x [page %08x: %s]\n",
				req, whom, vpt[VPN(fsreq)], fsreq);

		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
				whom);
			continue; // just leave it hanging...
		}

		pg = NULL;
		if (req == FSREQ_OPEN) {
			r = serve_open(whom, (struct Fsreq_open*)fsreq, &pg, &perm);
		} else if (req < NHANDLERS && handlers[req]) {
			r = handlers[req](whom, fsreq);
		} else {
			cprintf("Invalid request code %d from %08x\n", whom, req);
			r = -E_INVAL;
		}
		ipc_send(whom, r, pg, perm);
		sys_page_unmap(0, fsreq);
	}
}


void
fs_integrity_tests()
{
	/*
		Try to create files as long as the root dir's size
		does not increase. And then fail before the dir
		is flushed out, but its block has been flushed out.
	*/
	#ifdef FILE_CREATE_TEST
	struct File * pf, * tmp;
	int create = (file_open("/fcftest", &tmp) < 0);
	if(create)
	{
		int i, j, t;
		file_create("/fcftest", &tmp);
		for(i = 0; i <= 12; i++)
		{
			char name[20];
			strcpy(name, "/random000");
			t = i;
			for(j = 7; j < 9; j++)
			{
				name[j] = t%10 + '0';
				t/=10;
			}
			cprintf("creating file: %s\n", name);
			int r = crash_on_file_create(name, &pf);
			cprintf("%e\n", r);
		}
	}
	else
	{
		int i, j, t;
		for(i = 0; i <= 12; i++)
		{
			char name[20];
			strcpy(name, "/random000");
			t = i;
			for(j = 7; j < 9; j++)
			{
				name[j] = t%10 + '0';
				t/=10;
			}
			cprintf("opening file: %s\n", name);
			int r = file_open(name, &pf);
			cprintf("%e\n", r);
			if(r < 0)
				panic("%s should have been created, but is not\n", name);
		}
		cprintf("File Create Test: OK\n");
	}
	#endif
	
	/*
		Create a file named randomfrf, and then crash
		before the updated file block is committed to 
		disk.
	*/
	#ifdef FILE_REMOVE_TEST
	struct File * pf, * tmp;
	int create = (file_open("/frftest", &tmp) < 0);
	if(create)
	{
		int i, j, t;
		file_create("/frftest", &tmp);
		file_create("/randomfrf", &pf);
		crash_on_file_remove("/randomfrf");
	}
	else
	{
		int r = (file_open("/randomfrf", &pf));
		if(r == 0)
			panic("The file should have been removed\n");
		else
			cprintf("File Remove Test: OK\n");
	}
	#endif
	
	/*
		Decrease the size of the file, and crash before
		the file's metadata is sent to the disk.
	*/
	#ifdef BLOCK_FREE_TEST
	struct File * pf, * tmp;
	int create = (file_open("/bftest", &tmp) < 0);
	if(create)
	{
		int i, j, t;
		file_create("/bftest", &tmp);
		file_create("/randombf", &pf);
		// Increase the size normally
		file_set_size(pf, 8192);
		// Now crash while decreasing the size
		crash_on_file_set_size(pf, 4096, 1);
	}
	else
	{
		file_open("/randombf", &pf);
		if(pf->f_size != 4096)
			panic("The file size was supposed to be 4096");
		cprintf("Block Free Test: OK\n");
	}		
	#endif
}

void
umain(void)
{
	static_assert(sizeof(struct File) == 256);
	binaryname = "fs";
	cprintf("FS is running\n");

	// Check that we are able to do I/O
	outw(0x8A00, 0x8A00);
	cprintf("FS can do I/O\n");

	serve_init();
	fs_init();
	fs_test();

	#define FS_INTEGRITY_TESTS 1
	#ifdef FS_INTEGRITY_TESTS
	fs_integrity_tests();
	#endif

	serve();
}

