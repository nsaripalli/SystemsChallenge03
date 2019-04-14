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
inode *pathToINode(const char *path) {
    inode* dirnode = pathToLastItemContainer(path);// get the last directory

    if (strcmp(path, "/") == 0) {
        return dirnode;
    }
    char* filename = getTextAfterLastSlash(path);
    int dirIdx = directory_lookup(dirnode, filename);
    //printf("pathToINode ------ looking for %s\n", filename);
    if (dirIdx == -1) {
        //not found in directory
	//puts("not found in dir");
        return NULL;
    }
    dirent *dirData = (dirent *) pages_get_page(dirnode->ptrs[0]);
    int inum = dirData[dirIdx].inum;
    return get_inode(inum);
}

// implementation for: man 2 access
// ONLY Checks if a file exists.
int
nufs_access(const char *path, int mask) {
    int rv = 0;
    inode *fptr = pathToINode(path);
    if (fptr != NULL) {
        rv = 0;
    } else {
        rv = -ENOENT;
    }
    printf("access(%s, %04o) -> %d\n", path, mask, rv);
    return rv;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nufs_getattr(const char *path, struct stat *st) {
    int rv = 0;
    inode *fptr = pathToINode(path);

    if (fptr != NULL) {
        st->st_dev = 1; //arbitrary
        st->st_mode = fptr->mode;
        st->st_nlink = 1; //not doing this yet
        st->st_uid = getuid();
        st->st_gid = getgid(); //group id
        st->st_rdev = 0;
        st->st_size = fptr->size;
        st->st_blksize = 4096;
        st->st_blocks = bytes_to_pages(fptr->size);
        st->st_ctim = fptr->creation_time;
        st->st_atim = fptr->last_view;
        st->st_mtim = fptr->last_change;
    } else {
        rv = -ENOENT;
    }
    printf("getattr(%s) -> (%d) {mode: %04o, size: %ld}\n", path, rv, st->st_mode, st->st_size);
    return rv;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nufs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi) {
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);

    inode *node = pathToINode(path);
    if (node == 0 || rv2 < 0) {
        printf("readdir(%s) -> %d with clock result %i and node result %p\n", path, -1, rv2, node);
        return -1;
    }
    node->last_view = ts;

    struct stat st;
    int rv;
    rv = nufs_getattr(path, &st);
    assert(rv == 0);

    filler(buf, ".", &st, 0);

    slist *contents = directory_list(path);
    slist *cur = contents;
    while (cur != NULL) {
        filler(buf, cur->data, &st, 0);
        // 0 => filler manages its own offsets
        // https://www.cs.nmsu.edu/~pfeiffer/fuse-tutorial/html/unclear.html
        cur = cur->next;
    }

    printf("readdir(%s) -> %d\n", path, rv);
    return rv;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nufs_mknod(const char *path, mode_t mode, dev_t rdev) {
    int rv = -1;
    inode *inodes = (inode *) pages_get_page(1);
    void *inodeBitmap = get_inode_bitmap();

    int i = 0;
    int bit = bitmap_get(inodeBitmap, i);
    while (i < NUM_INODES) {
        if (bit == 0) {
            struct timespec ts;
            rv = clock_getres(CLOCK_REALTIME, &ts);

            if (rv < 0) {
                return -1;
            }

            //found free spot, set empty inode info
            inodes[i].refs = 1;
            inodes[i].mode = mode;
            inodes[i].size = 0;
            inodes[i].ptrs[0] = 0;
            inodes[i].ptrs[1] = 0;
            inodes[i].iptr = 0;
            inodes[i].creation_time = ts;
            inodes[i].last_change = ts;
            inodes[i].last_view = ts;
            rv = 0;
            bitmap_put(inodeBitmap, i, 1);
           
	    //Set directory entry to point to the above inode
	    inode* dirPtr = pathToLastItemContainer(path);//pathToDirFile(path);
	    char* fileName = getTextAfterLastSlash(path);
	    int a = directory_put(dirPtr, fileName, i);
            //printf("put %s at dirent %d\n", fileName, a); --this is now success code
            break;
        }
        i++;
        bit = bitmap_get(inodeBitmap, i);
    }
    assert(i != NUM_INODES);
    printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode) {
    int rv = nufs_mknod(path, mode | 040000, 0);
    inode *ptr = pathToINode(path);
    //todo error check
    ptr->ptrs[0] = alloc_page();
    void *dataPg = pages_get_page(ptr->ptrs[0]);
    if (dataPg != NULL) {
        dirent *cur = (dirent *) dataPg;
        for (int i = 0; i < (4096 / sizeof(dirent)); ++i) {
            cur[i].inum = -1;
        }
    }
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}

void clear_file(inode* file) {
    //assume files are only 1 page of data
    //todo this needs to be removed, and this has to delete all the
    //   contents of the file (Depending on file size)
    if (file->size > 0) {
        free_page(file->ptrs[0]);
    }
}



int
nufs_unlink(const char *path) {
    int rv = 0;

    inode* dirPtr = pathToLastItemContainer(path);
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);
    dirPtr->last_change = ts;

    char* fileName = getTextAfterLastSlash(path);
    int inodeNum = directory_lookup_inode(dirPtr, fileName);
    //int inodeNum = directory_delete(dirPtr, fileName);
    assert(inodeNum >= 0);

    inode *fileptr = get_inode(inodeNum);
    fileptr->last_change = ts;

    // Decrement ref counter
    // If ref counter == 0 {
    directory_delete(dirPtr, fileName);
    clear_file(fileptr);
    bitmap_put(get_inode_bitmap(), inodeNum, 0);
    // }

    printf("unlink(%s) -> %d\n", path, rv);
    return rv;
}

int
nufs_link(const char *from, const char *to) {
    int rv = -1;
    printf("link(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

//note this is only called by nufs when directory is empty
int
nufs_rmdir(const char *path) {
    int rv = -1;
    //todo check if its a directory
    slist* contents = directory_list(path);
    if (contents != NULL) {
	return -1;
    }
    rv = nufs_unlink(path);
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to) {
       int rv = -1;
//    void *inodes = pages_get_page(1);
//    inode *dirInode = (inode *) inodes;

    inode* dirInodeFrom = pathToLastItemContainer(from);
    inode* dirInodeTo = pathToLastItemContainer(to);

    char* fileNameFrom = getTextAfterLastSlash(from);
    char* fileNameTo = getTextAfterLastSlash(to);

    int fromINode = directory_lookup_inode(dirInodeFrom , fileNameFrom);
    int toINode = directory_lookup_inode(dirInodeTo , fileNameTo);


    //-- Check errors --
    
    // Both directory paths exist?
    if(dirInodeFrom == NULL || dirInodeTo == NULL) {
	return -1;
    }
    // from directory has the file, to directory does not
    if(!(fromINode >= 0 && toINode < 0)) {
	return -1;
    }
   
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }
    dirInodeFrom->last_view = ts;
    dirInodeFrom->last_change = ts;

    dirInodeTo->last_view = ts;
    dirInodeTo->last_change = ts;

    rv = directory_delete(dirInodeFrom, fileNameFrom);
    if (rv < 0) {
	puts("Rename - directory_delete failed");
	return -1;
    }
    rv = directory_put(dirInodeTo, fileNameTo, fromINode);
    if (rv < 0) {
	puts("Rename - directory_put failed");
	return -1;
    }
    
    /* old stuff
    int idx = directory_lookup(dirInode, fileName);//(from + 1));
    if (idx != -1) {
        //found the from entry
        dirent *entry = dirEntries + idx;
        strcpy(entry->name, (to + 1));
        rv = 0;
    }*/
    rv = 0;
    printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nufs_chmod(const char *path, mode_t mode) {
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }

    inode *node = pathToINode(path);
    node->mode = mode;
    node->last_change = ts;
    int rv = 0;
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

size_t PAGE_SIZE = 4096;

int
nufs_truncate(const char *path, off_t size) {
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }
    inode *fptr = pathToINode(path);
    fptr->last_change = ts;

    size_t curr_size = fptr->size;

    int numPages = curr_size / size;
    while (numPages > 0) {
        //UPDATE THE SIZE
        if (numPages == 1) {//numPages == 1) {
            void *pg = pages_get_page(fptr->ptrs[0]);
            memset(pg, 0, PAGE_SIZE);//sizeLeft);
            free_page(fptr->ptrs[0]);
            fptr->ptrs[0] = 0;
            fptr->size -= size;

        } else if (numPages == 2) {//numPages == 2) {
            void *pg = pages_get_page(fptr->ptrs[1]);
            memset(pg, 0, PAGE_SIZE);//sizeLeft);
            free_page(fptr->ptrs[1]);
            fptr->ptrs[1] = 0;
            fptr->size -= size;
        } else {
            int indirectPoints = fptr->iptr;
            int *indirectPage = pages_get_page(indirectPoints);

            int index = -1;
            void *curr_index_pointer = indirectPage + index;
            while (curr_index_pointer != 0) {
                index++;
                curr_index_pointer = indirectPage + index;
            }
            indirectPage = pages_get_page(indirectPoints);

            while (index >= 0) {
                int *currPointer = indirectPage + index;
                int currPAge = *(currPointer);
                void *pg = pages_get_page(currPAge);
                memset(pg, 0, PAGE_SIZE);//sizeLeft);
                free_page(currPAge);
                memset(currPointer, 0, sizeof(int));//sizeLeft);
                fptr->size -= size;
                index--;
            }
        }

        numPages--;
    }

    fptr->size = size;
    int rv = 0;
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi) {
    struct timespec ts;
    int rv2 = clock_getres(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }

    inode *node = pathToINode(path);
    node->last_view = ts;

    int rv = 0;
    printf("open(%s) -> %d\n", path, rv);
    return rv;
}


int read_indirect_page(inode *fptr, char *buf, size_t size, off_t offset) {//sizeLeft) {
    //printf("---indirect---- size = %ld || off = %ld\n", size, offset);
    off_t offHere = offset;// - (2*4096); //already checked 2 pages by the time we got here
    void *metaPg = pages_get_page(fptr->iptr);
    if (metaPg < 0) {
        return -1;
    }
    int *metaPgInt = (int *) metaPg;
    off_t i = offHere / 4096;

    //printf("offset = %ld ||| offhere = %ld\n", offset, offHere);
    size_t sizeLeft = size;
    size_t sizeRead = 0;
    // Add the data
    while (sizeLeft > 0) {
        //printf("sizeleft = %ld\n", sizeLeft);
        int nextPgIdx = metaPgInt[i];
        void *curPg = pages_get_page(nextPgIdx);
        int s = min(sizeLeft, 4096);
        //memcpy(curPg, buf, s);
        //printf("memcpy info -> nextPgIdx = %d, curPg = %p, s = %d\n", nextPgIdx, curPg, s);
        strncpy(buf, curPg, s);
        sizeRead += s;
        buf += s;
        i++;
        sizeLeft -= s;
    }
    return sizeRead;
}

//analogous to writing (ie. in 4096 byte chunks)
int read_pages(inode *fptr, char *buf, size_t size, off_t offset) {
    //todo handle offsets > 4096?
    int numPages = bytes_to_pages(fptr->size);
    int sizeLeft = size;//fptr->size;
    int sizeRead = 0;

    if (numPages == 1 && offset < 4096) { //todo incorporate page size into how much is read inside the if
        //puts("read from first page");
        void *pg = pages_get_page(fptr->ptrs[0]);
        //memcpy(pg, buf, sizeLeft); todo use this instead?? not sure
        strncpy(buf, pg, sizeLeft);
	sizeRead += sizeLeft;
        return sizeRead;//fptr->size;
    }
    if (numPages > 1 && offset < 4096) {
        //puts("other case");
        void *pg = pages_get_page(fptr->ptrs[0]);
        //memcpy(pg, buf, 4096);
        strncpy(buf, pg, 4096);
	sizeRead += 4096;
        sizeLeft -= 4096;
    }
    if (numPages == 2 && offset < (2 * 4096)) {
        //puts("read from second page");
        void *pg = pages_get_page(fptr->ptrs[1]);
        //memcpy(pg, buf, sizeLeft);
        strncpy(buf + 4096, pg, sizeLeft);
	sizeRead += sizeLeft;
        return sizeRead;
    }
    if (numPages > 2 && offset < (2 * 4096)) {
        void *pg = pages_get_page(fptr->ptrs[1]);
        strncpy(buf + 4096, pg, 4096);
        sizeLeft -= 4096;
	sizeRead += 4096;
    }
    int rv = read_indirect_page(fptr, buf + (2 * 4096), sizeLeft, offset);//size, offset);//sizeLeft);
    //assert(rv > 0); todo better error check here

    return sizeRead + rv;// + 4096 + 4096;
}


// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int rv = -1;
    inode *fptr = pathToINode(path);
    if (fptr != NULL) {
        rv = read_pages(fptr, buf, size, offset);
    }

    printf("read(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

//could probably use offset bytes_to_page to figure out where we write
//instead of checking for == 0
int write_indirect_page(inode *fptr, const char *buf, size_t size, off_t offset) {//sizeLeft) {
    if (fptr->iptr == 0) {
        fptr->iptr = alloc_page();
    }
    void *metaPg = pages_get_page(fptr->iptr);
    if (metaPg < 0) {
        return -1;
    }
    int *metaPgInt = (int *) metaPg;
    // Find first empty spot
    int i = 0;
    while (metaPgInt[i] != 0) {
        i++;
    }
    //todo add bounds checking, ^^^ is not an infinite array
    // Add the data
    int sizeLeft = size;
    while (sizeLeft > 0) {
        int nextPgIdx = alloc_page();
        if (nextPgIdx == -1) {
            return -1; //todo set errno????
        }
        void *curPg = pages_get_page(nextPgIdx);
        int s = min(sizeLeft, 4096);
        memcpy(curPg, buf, s);
        fptr->size += s;
        metaPgInt[i] = nextPgIdx;
        i++;
        sizeLeft -= s;
    }
    return 0; //success
}


//this doesn't really use offset.... is that a problem?
//see note above, could probably  use to figure out what page to write to immediately
int write_pages(inode *fptr, const char *buf, size_t size, off_t offset) {
    if (fptr->ptrs[0] == 0) {
        fptr->ptrs[0] = alloc_page();
        void *pg = pages_get_page(fptr->ptrs[0]);
        memcpy(pg, buf, size);;
        fptr->size += size;
        return size;
    }
    /*if(numPages > 1) {
	fptr->ptrs[0] = alloc_page();
        void* pg = pages_get_page(fptr->ptrs[0]);
        memcpy(pg, buf, 4096);
	sizeLeft -= 4096;
    }*/
    if (fptr->ptrs[1] == 0) {
        fptr->ptrs[1] = alloc_page();
        void *pg = pages_get_page(fptr->ptrs[1]);
        memcpy(pg, buf, size);
        fptr->size += size;
        return size;
    }
    /*if(numPages > 2) {
        fptr->ptrs[1] = alloc_page();
        void* pg = pages_get_page(fptr->ptrs[1]);
        memcpy(pg, buf, 4096);
	sizeLeft -= 4096;
    }*/

    int rv = write_indirect_page(fptr, buf, size, offset);
    //assert(rv > 0);

    return size;//fptr->size;
}

// Actually write data
// This function is called with size being how much to write at a time
// ie. for a 5K file, it's called twice with appropriate sizes and offsets
int
nufs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int rv = -1;
    inode *fptr = pathToINode(path);
    if (fptr != NULL) {
        //currently assume just writing 1 page or less
        //mode is set in mknod
        //fptr->size = size;

        struct timespec ts;
        rv = clock_getres(CLOCK_REALTIME, &ts);
        if (rv < 0) {
            return -1;
        }

        rv = write_pages(fptr, buf, size, offset);


        fptr->last_change = ts;

        //assert the copy didn't fail?
        //rv = size;
    }

    printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

int changeTimeStamp(const char *path, const struct timespec ts[2]) {
    inode *thing = pathToINode(path);
    thing->last_view = ts[0];
    thing->last_change = ts[1];
    return 0;
}

// Update the timestamps on a file or directory.
int
nufs_utimens(const char *path, const struct timespec ts[2]) {
    int rv = changeTimeStamp(path, ts);
    printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
           path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
    return rv;
}

// Extended operations
int
nufs_ioctl(const char *path, int cmd, void *arg, struct fuse_file_info *fi,
           unsigned int flags, void *data) {
    int rv = -1;
    printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
    return rv;
}

void
nufs_init_ops(struct fuse_operations *ops) {
    memset(ops, 0, sizeof(struct fuse_operations));
    ops->access = nufs_access;
    ops->getattr = nufs_getattr;
    ops->readdir = nufs_readdir;
    ops->mknod = nufs_mknod;
    ops->mkdir = nufs_mkdir;
    ops->link = nufs_link;
    ops->unlink = nufs_unlink;
    ops->rmdir = nufs_rmdir;
    ops->rename = nufs_rename;
    ops->chmod = nufs_chmod;
    ops->truncate = nufs_truncate;
    ops->open = nufs_open;
    ops->read = nufs_read;
    ops->write = nufs_write;
    ops->utimens = nufs_utimens;
    ops->ioctl = nufs_ioctl;
};

struct fuse_operations nufs_ops;

int
main(int argc, char *argv[]) {
    assert(argc > 2 && argc < 6);
    storage_init(argv[--argc]);
    nufs_init_ops(&nufs_ops);
    return fuse_main(argc, argv, &nufs_ops, NULL);
}

