// based on cs3650 starter code

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <bsd/string.h>
#include <assert.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "pages.h"
#include "storage.h"
#include "util.h"
#include "inode.h"
#include "bitmap.h"
#include "directory.h"

static const int NUM_INODES = 4096 / sizeof(inode);

// Get the inode* at path, else NULL
// Starts from /
inode* pathToINode(const char* path) {
    inode* dirnode = (inode*)pages_get_page(1); //root dir in inode 0
    if(strcmp(path, "/") == 0) {
        return dirnode;
    }
    int dirIdx = directory_lookup(dirnode, (path + 1));
    if(dirIdx == -1) {
        //not found in directory
        return NULL;
    }
    dirent* dirData = (dirent*)pages_get_page(dirnode->ptrs[0]);
    int inum = dirData[dirIdx].inum;
    return get_inode(inum);
}

// implementation for: man 2 access
// ONLY Checks if a file exists.
int
nufs_access(const char *path, int mask)
{
    int rv = 0;
    inode* fptr = pathToINode(path);
    if(fptr != NULL) {
	rv = 0;
    } else {
        rv = -ENOENT;
    }
    //printf("access(%s, %04o) -> %d\n", path, mask, rv);
    return rv;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nufs_getattr(const char *path, struct stat *st)
{
    int rv = 0;
    if (strcmp(path, "/") == 0) {
        st->st_mode = 040755; // directory
        st->st_size = 0;
        st->st_uid = getuid();
    } else {
        inode* fptr = pathToINode(path);
        if(fptr != NULL) {
	    st->st_dev = 1; //arbitrary
	    st->st_mode = fptr->mode;
	    st->st_nlink = 1; //not doing this yet
	    st->st_uid = getuid();
	    st->st_gid = getgid(); //group id
	    st->st_rdev = 0;
	    st->st_size = fptr->size;
	    st->st_blksize = 4096;
	    st->st_blocks = bytes_to_pages(fptr->size);
	    //st->st_mode = 0100644; // regular file
        } else {
	    rv = -ENOENT;
        }
    }
    //printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, rv, st->st_mode, st->st_size);
    return rv;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
    struct stat st;
    int rv;
    rv = nufs_getattr("/", &st);
    assert(rv == 0);

    slist* contents = directory_list(path);
    slist* cur = contents;
    while(cur != NULL) {
	    filler(buf, cur->data, &st, 0);
	    // 0 => filler manages its own offsets 
	    // https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/unclear.html
	    cur = cur->next;
    }

    //printf("readdir(%s) -> %d\n", path, rv);
    return 0;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nufs_mknod(const char *path, mode_t mode, dev_t rdev)
{    
    int rv = -1;
    inode* inodes = (inode*)pages_get_page(1);
    void* inodeBitmap = get_inode_bitmap();

    int i = 0;
    int bit = bitmap_get(inodeBitmap, i);
    while(i < NUM_INODES) {
	if(bit == 0) {
	    //found free spot
            inodes[i].refs = 1;
	    inodes[i].mode = mode;
	    inodes[i].size = 0;
	    inodes[i].ptrs[0] = 0;
	    inodes[i].ptrs[1] = 0;
	    inodes[i].iptr = 0;
	    rv = 0;
	    bitmap_put(inodeBitmap, i, 1);
            //now set in "/" folder	    
	    int a = directory_put(&inodes[0], (path + 1), i);
	    //printf("put %s at dirent %d\n", (path + 1), a);
            break;
	}
	i++;
	bit = bitmap_get(inodeBitmap, i);
    }
    //printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode)
{
    int rv = nufs_mknod(path, mode | 040000, 0);
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_unlink(const char *path)
{
    int rv = 0;
    //remove from directory entry
    inode* dirptr = (inode*)pages_get_page(1); //root dir in inode 0

    int inodeNum = directory_delete(dirptr, (path + 1)); //strip leading /
    assert(inodeNum >= 0);

    inode* fileptr = get_inode(inodeNum);
    //assume files are only 1 page of data
    if(fileptr->size > 0) {
        free_page(fileptr->ptrs[0]);
    }
    bitmap_put(get_inode_bitmap(), inodeNum, 0);

    //printf("unlink(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_link(const char *from, const char *to)
{
    int rv = -1;
    printf("link(%s => %s) -> %d\n", from, to, rv);
	return rv;
}

int
nufs_rmdir(const char *path)
{
    int rv = -1;
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to)
{
    int rv = -1;
    void* inodes = pages_get_page(1);
    inode* dirInode = (inode*)inodes;
    dirent* dirEntries = (dirent*)pages_get_page(dirInode->ptrs[0]);
    
    int idx = directory_lookup(dirInode, (from + 1)); //first inode is /
    if(idx != -1) {
	//found the from entry
        dirent* entry = dirEntries + idx;
	strcpy(entry->name, (to + 1));
	rv = 0;
    }

    //printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_chmod(const char *path, mode_t mode)
{
    int rv = -1;
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int
nufs_truncate(const char *path, off_t size)
{
    int rv = -1;
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi)
{
    int rv = 0;
    //printf("open(%s) -> %d\n", path, rv);
    return rv;
}

// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = -1;
    inode* fptr = pathToINode(path);
    if(fptr != NULL) {
	//we assume files are just one block for now
	void* data = pages_get_page(fptr->ptrs[0]);
	data += offset;
	strncpy(buf, data, fptr->size);
	rv = fptr->size;
    }

    //printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Actually write data
int
nufs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = -1;
    inode* fptr = pathToINode(path);
    if(fptr != NULL) {
	//currently assume just writing 1 page or less
	fptr->refs = 1;
	//mode is set in mknod
	fptr->size = size;
	fptr->ptrs[0] = alloc_page();
	void* pg = pages_get_page(fptr->ptrs[0]);
	memcpy(pg, buf, size);
	//assert the copy didn't fail?
	rv = size;
    }

    //printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Update the timestamps on a file or directory.
int
nufs_utimens(const char* path, const struct timespec ts[2])
{
    int rv = -1;
    printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
           path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
    return rv;
}

// Extended operations
int
nufs_ioctl(const char* path, int cmd, void* arg, struct fuse_file_info* fi,
           unsigned int flags, void* data)
{
    int rv = -1;
    printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
    return rv;
}

void
nufs_init_ops(struct fuse_operations* ops)
{
    memset(ops, 0, sizeof(struct fuse_operations));
    ops->access   = nufs_access;
    ops->getattr  = nufs_getattr;
    ops->readdir  = nufs_readdir;
    ops->mknod    = nufs_mknod;
    ops->mkdir    = nufs_mkdir;
    ops->link     = nufs_link;
    ops->unlink   = nufs_unlink;
    ops->rmdir    = nufs_rmdir;
    ops->rename   = nufs_rename;
    ops->chmod    = nufs_chmod;
    ops->truncate = nufs_truncate;
    ops->open	  = nufs_open;
    ops->read     = nufs_read;
    ops->write    = nufs_write;
    ops->utimens  = nufs_utimens;
    ops->ioctl    = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int
main(int argc, char *argv[])
{
    assert(argc > 2 && argc < 6);
    storage_init(argv[--argc]);
    nufs_init_ops(&nufs_ops);
    return fuse_main(argc, argv, &nufs_ops, NULL);
}

