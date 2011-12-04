#include <inc/string.h>

#include "fs.h"

// --------------------------------------------------------------
// Super block
// --------------------------------------------------------------

// Validate the file system super-block.
void
check_super(void)
{
	if (super->s_magic != FS_MAGIC)
		panic("bad file system magic number");

	if (super->s_nblocks > DISKSIZE/BLKSIZE)
		panic("file system is too large");

	cprintf("superblock is good\n");
}

// --------------------------------------------------------------
// Free block bitmap
// --------------------------------------------------------------

// Check to see if the block bitmap indicates that block 'blockno' is free.
// Return 1 if the block is free, 0 if not.
bool
block_is_free(uint32_t blockno)
{
	if (super == 0 || blockno >= super->s_nblocks)
		return 0;
	if (bitmap[blockno / 32] & (1 << (blockno % 32)))
		return 1;
	return 0;
}

// Mark a block free in the bitmap
void
free_block(uint32_t blockno)
{
	// Blockno zero is the null pointer of block numbers.
	if (blockno == 0)
		panic("attempt to free zero block");
	bitmap[blockno/32] |= 1<<(blockno%32);
}

// Search the bitmap for a free block and allocate it.  When you
// allocate a block, immediately flush the changed bitmap block
// to disk.
// 
// Return block number allocated on success,
// -E_NO_DISK if we are out of blocks.
//
// Hint: use free_block as an example for manipulating the bitmap.
int
alloc_block(void)
{
	// The bitmap consists of one or more blocks.  A single bitmap block
	// contains the in-use bits for BLKBITSIZE blocks.  There are
	// super->s_nblocks blocks in the disk altogether.

	// LAB 5: Your code here.
	uint32_t blockno;
	for(blockno = 1; blockno < super->s_nblocks; blockno++)
		if(block_is_free(blockno))
		{
			bitmap[blockno/32] ^= 1<<(blockno%32);
			flush_block(diskaddr(2));
			assert(!block_is_free(blockno));
			return blockno;
		}
	return -E_NO_DISK;
}

// Validate the file system bitmap.
//
// Check that all reserved blocks -- 0, 1, and the bitmap blocks themselves --
// are all marked as in-use.
void
check_bitmap(void)
{
	uint32_t i;

	// Make sure all bitmap blocks are marked in-use
	for (i = 0; i * BLKBITSIZE < super->s_nblocks; i++)
		assert(!block_is_free(2+i));

	// Make sure the reserved and root blocks are marked in-use.
	assert(!block_is_free(0));
	assert(!block_is_free(1));

	cprintf("bitmap is good\n");
}


// --------------------------------------------------------------
// File system structures
// --------------------------------------------------------------

// Initialize the file system
void
fs_init(void)
{
	static_assert(sizeof(struct File) == 256);

	// Find a JOS disk.  Use the second IDE disk (number 1) if available.
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);
	
	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	// Set "bitmap" to the beginning of the first bitmap block.
	bitmap = diskaddr(2);
	
	// Set "journal" to the journal data
	journal = diskaddr(3);

	check_super();
	check_bitmap();
	#ifdef JOURNALING
	fsck();
	#endif
}

// Find the disk block number slot for the 'filebno'th block in file 'f'.
// Set '*ppdiskbno' to point to that slot.
// The slot will be one of the f->f_direct[] entries,
// or an entry in the indirect block.
// When 'alloc' is set, this function will allocate an indirect block
// if necessary.
//
// Returns:
//	0 on success (but note that *ppdiskbno might equal 0).
//	-E_NOT_FOUND if the function needed to allocate an indirect block, but
//		alloc was 0.
//	-E_NO_DISK if there's no space on the disk for an indirect block.
//	-E_INVAL if filebno is out of range (it's >= NDIRECT + NINDIRECT).
//
// Analogy: This is like pgdir_walk for files.  
// Hint: Don't forget to clear any block you allocate.
static int
file_block_walk(struct File *f, uint32_t filebno, uint32_t **ppdiskbno, bool alloc)
{
	// LAB 5: Your code here.
	if(filebno >= NDIRECT + NINDIRECT)
		return -E_INVAL;
	if(filebno < NDIRECT)
	{
		*ppdiskbno = &(f->f_direct[filebno]);
		return 0;
	}
	if(f->f_indirect == 0 && !alloc)
		return -E_NOT_FOUND;
	if(f->f_indirect == 0)
	{
		int status = 0;
		if((status = alloc_block()) < 0)
		{
			return status;
		}
		f->f_indirect = status;
		memset((int*)diskaddr(f->f_indirect), 0, BLKSIZE);
	}
	uint32_t* baddr = (uint32_t*)diskaddr(f->f_indirect);

	*ppdiskbno = (uint32_t*)(baddr + filebno - NDIRECT);
	return 0;

}

// Set *blk to point at the filebno'th block in file 'f'.
// Allocate the block if it doesn't yet exist.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_NO_DISK if a block needed to be allocated but the disk is full.
//	-E_INVAL if filebno is out of range.
//
// Hint: Use file_block_walk and alloc_block.
int
file_get_block(struct File *f, uint32_t filebno, char **blk)
{
	// LAB 5: Your code here.
	if(filebno >= NDIRECT + NINDIRECT)
		return -E_INVAL;
	uint32_t* baddr;
	int status = 0;
	if((status = file_block_walk(f, filebno, &baddr, 1)) < 0)
		return status;
	if(*baddr == 0)
	{
		if((status = alloc_block()) < 0)
			return status;
		*baddr = status;
	}
	char* addr = (char*)diskaddr(*baddr);
	*blk = addr;
	return 0;
}

// Try to find a file named "name" in dir.  If so, set *file to it.
//
// Returns 0 and sets *file on success, < 0 on error.  Errors are:
//	-E_NOT_FOUND if the file is not found
static int
dir_lookup(struct File *dir, const char *name, struct File **file)
{
	int r;
	uint32_t i, j, nblock;
	char *blk;
	struct File *f;

	// Search dir for name.
	// We maintain the invariant that the size of a directory-file
	// is always a multiple of the file system's block size.
	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct File*) blk;
		for (j = 0; j < BLKFILES; j++)
			if (strcmp(f[j].f_name, name) == 0) {
				*file = &f[j];
				return 0;
			}
	}
	return -E_NOT_FOUND;
}

// Set *file to point at a free File structure in dir.  The caller is
// responsible for filling in the File fields.
static int
dir_alloc_file(struct File *dir, struct File **file)
{
	int r;
	uint32_t nblock, i, j;
	char *blk;
	struct File *f;

	assert((dir->f_size % BLKSIZE) == 0);
	nblock = dir->f_size / BLKSIZE;
	for (i = 0; i < nblock; i++) {
		if ((r = file_get_block(dir, i, &blk)) < 0)
			return r;
		f = (struct File*) blk;
		for (j = 0; j < BLKFILES; j++)
			if (f[j].f_name[0] == '\0') {
				*file = &f[j];
				return 0;
			}
	}
	dir->f_size += BLKSIZE;
	if ((r = file_get_block(dir, i, &blk)) < 0)
		return r;
	f = (struct File*) blk;
	*file = &f[0];
	return 0;
}

// Skip over slashes.
static const char*
skip_slash(const char *p)
{
	while (*p == '/')
		p++;
	return p;
}

// Evaluate a path name, starting at the root.
// On success, set *pf to the file we found
// and set *pdir to the directory the file is in.
// If we cannot find the file but find the directory
// it should be in, set *pdir and copy the final path
// element into lastelem.
static int
walk_path(const char *path, struct File **pdir, struct File **pf, char *lastelem)
{
	const char *p;
	char name[MAXNAMELEN];
	struct File *dir, *f;
	int r;

	// if (*path != '/')
	//	return -E_BAD_PATH;
	path = skip_slash(path);
	f = &super->s_root;
	dir = 0;
	name[0] = 0;

	if (pdir)
		*pdir = 0;
	*pf = 0;
	while (*path != '\0') {
		dir = f;
		p = path;
		while (*path != '/' && *path != '\0')
			path++;
		if (path - p >= MAXNAMELEN)
			return -E_BAD_PATH;
		memmove(name, p, path - p);
		name[path - p] = '\0';
		path = skip_slash(path);

		if (dir->f_type != FTYPE_DIR)
			return -E_NOT_FOUND;

		if ((r = dir_lookup(dir, name, &f)) < 0) {
			if (r == -E_NOT_FOUND && *path == '\0') {
				if (pdir)
					*pdir = dir;
				if (lastelem)
					strcpy(lastelem, name);
				*pf = 0;
			}
			return r;
		}
	}

	if (pdir)
		*pdir = dir;
	*pf = f;
	return 0;
}

// --------------------------------------------------------------
// File operations
// --------------------------------------------------------------

// Create "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_create(const char *path, struct File **pf)
{
	char name[MAXNAMELEN];
	int r, prev;
	struct File *dir, *f;
	
	if ((r = walk_path(path, &dir, &f, name)) == 0)
		return -E_FILE_EXISTS;
	if (r != -E_NOT_FOUND || dir == 0)
		return r;
	if (dir_alloc_file(dir, &f) < 0)
		return r;
	strcpy(f->f_name, name);
	*pf = f;
	file_flush(dir);
	return 0;
}

// Open "path".  On success set *pf to point at the file and return 0.
// On error return < 0.
int
file_open(const char *path, struct File **pf)
{
	int status = walk_path(path, 0, pf, 0);
	return status;
}

// Read count bytes from f into buf, starting from seek position
// offset.  This meant to mimic the standard pread function.
// Returns the number of bytes read, < 0 on error.
ssize_t
file_read(struct File *f, void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	if (offset >= f->f_size)
		return 0;

	count = MIN(count, f->f_size - offset);

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(buf, blk + pos % BLKSIZE, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// Write count bytes from buf into f, starting at seek position
// offset.  This is meant to mimic the standard pwrite function.
// Extends the file if necessary.
// Returns the number of bytes written, < 0 on error.
int
file_write(struct File *f, const void *buf, size_t count, off_t offset)
{
	int r, bn;
	off_t pos;
	char *blk;

	// Extend file if necessary
	if (offset + count > f->f_size)
		if ((r = file_set_size(f, offset + count)) < 0)
			return r;

	for (pos = offset; pos < offset + count; ) {
		if ((r = file_get_block(f, pos / BLKSIZE, &blk)) < 0)
			return r;
		bn = MIN(BLKSIZE - pos % BLKSIZE, offset + count - pos);
		memmove(blk + pos % BLKSIZE, buf, bn);
		pos += bn;
		buf += bn;
	}

	return count;
}

// Remove a block from file f.  If it's not there, just silently succeed.
// Returns 0 on success, < 0 on error.
static int
file_free_block(struct File *f, uint32_t filebno)
{
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0)
		return r;
	if (*ptr) {
		free_block(*ptr);
		*ptr = 0;
	}
	return 0;
}

// Remove any blocks currently used by file 'f',
// but not necessary for a file of size 'newsize'.
// For both the old and new sizes, figure out the number of blocks required,
// and then clear the blocks from new_nblocks to old_nblocks.
// If the new_nblocks is no more than NDIRECT, and the indirect block has
// been allocated (f->f_indirect != 0), then free the indirect block too.
// (Remember to clear the f->f_indirect pointer so you'll know
// whether it's valid!)
// Do not change f->f_size.
static void
file_truncate_blocks(struct File *f, off_t newsize)
{
	int r;
	uint32_t bno, old_nblocks, new_nblocks;

	old_nblocks = (f->f_size + BLKSIZE - 1) / BLKSIZE;
	new_nblocks = (newsize + BLKSIZE - 1) / BLKSIZE;
	for (bno = new_nblocks; bno < old_nblocks; bno++)
		if ((r = file_free_block(f, bno)) < 0)
			cprintf("warning: file_free_block: %e", r);

	if (new_nblocks <= NDIRECT && f->f_indirect) {
		free_block(f->f_indirect);
		f->f_indirect = 0;
	}
}

// Set the size of file f, truncating or extending as necessary.
int
file_set_size(struct File *f, off_t newsize)
{
	if (f->f_size > newsize)
		file_truncate_blocks(f, newsize);
	f->f_size = newsize;
	flush_block(f);
	return 0;
}


// Flush the contents and metadata of file f out to disk.
// Loop over all the blocks in file.
// Translate the file block number into a disk block number
// and then check whether that disk block is dirty.  If so, write it out.
void
file_flush(struct File *f)
{
	int i;
	uint32_t *pdiskbno;

	for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
		if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
		    pdiskbno == NULL || *pdiskbno == 0)
			continue;
		flush_block(diskaddr(*pdiskbno));
	}
	flush_block(f);
	if (f->f_indirect)
		flush_block(diskaddr(f->f_indirect));
}


// Remove a file by truncating it and then zeroing the name.
int
file_remove(const char *path)
{
	int r;
	struct File *f;

	if ((r = walk_path(path, 0, &f, 0)) < 0)
		return r;

	file_truncate_blocks(f, 0);
	f->f_name[0] = '\0';
	f->f_size = 0;
	flush_block(f);

	return 0;
}

// Sync the entire file system.  A big hammer.
void
fs_sync(void)
{
	int i;
	for (i = 1; i < super->s_nblocks; i++)
		flush_block(diskaddr(i));
}

/*	
	Functions used for Journaling
*/


// Comment the below line to turn off journaling
#define JOURNALING 1

#define IS_JE_FREE(x) (!(journal->j_entry_bitmap[(x)/32] & (1<<((x)%32))))
#define TOGGLE_JE_BITMAP(x) (journal->j_entry_bitmap[(x)/32] ^= (1<<((x)%32)))

int
j_flush_all()
{
	// Flush all pending journal entries
	// We enter this function when we have been
	// very lazy in writing journal entries to disk
	// Check using the je_ondisk member in the JournalEntry struct
	// Return the first free journal entry
	
	// TODO: Write this

	return 0;
}

void
j_flush_je(int je_num, int strictly)
{
	// TODO:
	// If strictly != 1, it may not flush the journal
	// immediately. Thus, allowing the user to fine-tune
	// the risk. For now, we flush the journal entry
	// immediately.
	
	journal->j_entries[je_num].je_ondisk = 1;
	flush_block(&(journal->j_entries[je_num]));
	// Flush the bitmap later
	flush_block(journal);
}

void
j_postop_write(int je_num, int strictly)
{
	// Flush the JE after the operation is done
	// TODO:
	// Can this be done lazily? Or combined with the
	// previously pending journal flushes?
	TOGGLE_JE_BITMAP(je_num);
	flush_block(journal);
}


int
j_write(struct JournalEntry * je)
{
	// Write the journal entry
	int je_num = -1;
	for(je_num = 0; je_num < MAXJENTRIES; je_num++)
		if(IS_JE_FREE(je_num))
		{ break; }
	
	if(je_num == MAXJENTRIES)
	{
		// Ran out of Journal Entry space;
		// This case would not arise unless we are being very lazy
		je_num = j_flush_all();
	}
	journal->j_entries[je_num] = *je;
	TOGGLE_JE_BITMAP(je_num);
	j_flush_je(je_num, 1);
	return je_num;
}


/* 
	Below are the crash-prone versions
	of the functions above. They are to
	demonstrate potential crashes, and
	their effect on the File System.
*/

// Demonstrates the crashing on file creation
// And orphaning of files.
int
crash_on_file_create(const char *path, struct File **pf)
{
	char name[MAXNAMELEN];
	int r, prev;
	struct File *dir, *f;
	
	#ifdef JOURNALING
	struct JournalEntry je;
	strcpy(je.je_desc.desc_filecreate.path, path);
	je.je_type = JE_FILECREATE;
	#endif
	
	if ((r = walk_path(path, &dir, &f, name)) == 0)
		return -E_FILE_EXISTS;
	if (r != -E_NOT_FOUND || dir == 0)
		return r;
	
	#ifdef JOURNALING
	int je_num = j_write(&je);
	#endif
	
	prev = dir->f_size;
	if (dir_alloc_file(dir, &f) < 0)
		return r;
	strcpy(f->f_name, name);
	*pf = f;
	

	int crash = (prev < dir->f_size);
	crash_on_file_flush(dir, crash);
		
	#ifdef JOURNALING
	j_postop_write(je_num, 1);		
	#endif
	return 0;
}

// Ideally, as per soft updates, the pointer should be
// nullified first. But its good, we demonstrate yet
// another crash-unsafe point. :-P
static int
crash_on_file_free_block(struct File *f, uint32_t filebno, int crash)
{
	int r;
	uint32_t *ptr;

	if ((r = file_block_walk(f, filebno, &ptr, 0)) < 0)
		return r;
	if (*ptr) {
		free_block(*ptr);
		if(crash)
			panic("Crash environment set up. Please restart.");
		*ptr = 0;
	}
	return 0;
}


static void
crash_on_file_truncate_blocks(struct File *f, off_t newsize, int crash)
{
	int r;
	uint32_t bno, old_nblocks, new_nblocks;

	old_nblocks = (f->f_size + BLKSIZE - 1) / BLKSIZE;
	new_nblocks = (newsize + BLKSIZE - 1) / BLKSIZE;
	for (bno = new_nblocks; bno < old_nblocks; bno++)
		if ((r = crash_on_file_free_block(f, bno, crash)) < 0)
			cprintf("warning: file_free_block: %e", r);

	if (new_nblocks <= NDIRECT && f->f_indirect) {
		free_block(f->f_indirect);
		f->f_indirect = 0;
	}
}

int
crash_on_file_set_size(struct File *f, off_t newsize, int crash)
{
	#ifdef JOURNALING
	struct JournalEntry je;
	je.je_desc.desc_fileresize.file_ptr = (uintptr_t)f;
	je.je_type = JE_FILERESIZE;
	je.je_desc.desc_fileresize.new_size = newsize;
	int je_num = j_write(&je);
	#endif

	if (f->f_size > newsize)
		crash_on_file_truncate_blocks(f, newsize, crash);
	f->f_size = newsize;
	if(crash)
		panic("Crash environment set up. Please restart");
	flush_block(f);
	#ifdef JOURNALING
	j_postop_write(je_num, 1);	
	#endif
	return 0;
}

// This is to demonstrate what happens when the
// system breaks while a big flush is on.
void
crash_on_file_flush(struct File *f, int crash)
{
	//cprintf("Flush called for File: %s %d\n", f->f_name, crash);
	int i;
	uint32_t *pdiskbno;

	for (i = 0; i < (f->f_size + BLKSIZE - 1) / BLKSIZE; i++) {
		if (file_block_walk(f, i, &pdiskbno, 0) < 0 ||
		    pdiskbno == NULL || *pdiskbno == 0)
			continue;
		flush_block(diskaddr(*pdiskbno));
	}
	if(crash)
		panic("Crash environment setup. Please restart.");
	flush_block(f);
	if (f->f_indirect)
		flush_block(diskaddr(f->f_indirect));
}

int
crash_on_file_remove(const char *path)
{
	int r;
	struct File *f;
	
	#ifdef JOURNALING
	struct JournalEntry je;
	strcpy(je.je_desc.desc_fileremove.path, path);
	je.je_type = JE_FILEREMOVE;
	#endif


	if ((r = walk_path(path, 0, &f, 0)) < 0)
		return r;
	
	#ifdef JOURNALING
	int je_num = j_write(&je);	
	#endif

	file_truncate_blocks(f, 0);
	f->f_name[0] = '\0';
	f->f_size = 0;
	panic("Crash environment set up. Please restart.");
	flush_block(f);
	
	#ifdef JOURNALING
	j_postop_write(je_num, 1);
	#endif

	return 0;
}

// fsck to verify the journal and correct it
void
fsck(void)
{
	cprintf("fsck\n");
	journal->j_entries = diskaddr(4);
	/*
	
	cprintf("nentries: %d, jentries: %p, sizeof(JournalEntry): %d\n", \
		journal->j_nentries, journal->j_entries, sizeof(struct JournalEntry));
	
	uint32_t used_blocks = 0, blockno;
	for(blockno = 0; blockno < super->s_nblocks; blockno++)
		if(!block_is_free(blockno))
			used_blocks++;
	
	cprintf("used_blocks: %d\n", used_blocks);
	*/
	
	uint32_t je, cnt = 0;
	for(je = 0; je < (journal->j_nentries); je++) {
		if(!(journal->j_entry_bitmap[je/32] & (1<<(je%32))))
		{
			// Needs to be fixed
			cnt++;
			// TODO: Fill this up to replay the work
			cprintf("Inconsistency in je_num: %d\n", je);
			struct File * pf;
			switch(journal->j_entries[je].je_type) {
				case JE_FILECREATE:
					cprintf("Creating file %s\n", journal->j_entries[je].je_desc.desc_filecreate.path);
					file_create(journal->j_entries[je].je_desc.desc_filecreate.path, &pf);
					break;
				case JE_FILEREMOVE:
					cprintf("Removing file %s\n", journal->j_entries[je].je_desc.desc_fileremove.path);
					file_remove(journal->j_entries[je].je_desc.desc_fileremove.path);
					break;
				case JE_FILERESIZE:
					pf = (struct File *)(journal->j_entries[je].je_desc.desc_fileresize.file_ptr);
					cprintf("Resizing file %s\n", pf->f_name);
					file_set_size(pf, journal->j_entries[je].je_desc.desc_fileresize.new_size);
					break;
			};
		}
	}
	// Clear the bitmap
	memset(journal->j_entry_bitmap, 0xFF, sizeof(journal->j_entry_bitmap));
	flush_block(journal);
	cprintf("%d inconsistencies found and fixed using fsck\n", cnt);
}




