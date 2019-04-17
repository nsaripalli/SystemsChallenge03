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


static const size_t PAGE_SIZE = 4096;
static const int NUM_INODES = (2 * 4096) / sizeof(inode);

// Get the inode* at path, else NULL
// Starts from /
inode *pathToINode(const char *path) {
    inode *dirnode = pathToLastItemContainer(path);// get the last directory

    if (strcmp(path, "/") == 0) {
        return dirnode;
    }
    char *filename = getTextAfterLastSlash(path);
    //int dirIdx = directory_lookup(dirnode, filename);
    int inum = directory_lookup_inode(dirnode, filename);
    //printf("pathToINode ------ looking for %s\n", filename);
    if (inum == -1) {
        //not found in directory
        puts("not found in dir");
        return NULL;
    }
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
        st->st_ctime = fptr->creation_time;
        st->st_atime = fptr->last_view;
        st->st_mtime = fptr->last_change;
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
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);

    inode *node = pathToINode(path);
    if (node == 0 || rv2 < 0) {
        printf("readdir(%s) -> %d with clock result %i and node result %p\n", path, -1, rv2, node);
        return -1;
    }
    node->last_view = ts.tv_sec;

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
    puts("MKNOD has been summoned");

    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);

    if (rv2 < 0) {
        puts("Time error in MKNOD");
        return -1;
    }


    int rv = -1;
    inode *inodes = (inode *) pages_get_page(1);
    if (inodes == NULL) {
        puts("Unable to get first page");
        return -1;
    }

    inodes->last_change = ts.tv_sec;
    //printf("current time is %li %li \n", ts.tv_sec, ts.tv_sec);

    void *inodeBitmap = get_inode_bitmap();


    int i = 0;
    int bit = bitmap_get(inodeBitmap, i);
    while (i < NUM_INODES) {
        if (bit == 0) {

            //found free spot, set empty inode info
            inodes[i].refs = 1;
            inodes[i].mode = mode;
            inodes[i].size = 0;
            inodes[i].ptrs[0] = 0;
            inodes[i].ptrs[1] = 0;
            inodes[i].iptr = 0;
            inodes[i].creation_time = ts.tv_sec;
            inodes[i].last_change = ts.tv_sec;
            inodes[i].last_view = ts.tv_sec;

            rv = 0;
            bitmap_put(inodeBitmap, i, 1);

            //Set directory entry to point to the above inode
            inode *dirPtr = pathToLastItemContainer(path);
            char *fileName = getTextAfterLastSlash(path);
            int a = directory_put(dirPtr, fileName, i);
            //printf("put %s at dirent %d\n", fileName, a);
            break;
        }
        i++;
        bit = bitmap_get(inodeBitmap, i);
    }
    if (i == NUM_INODES) {
        puts("MKNOD FAILED");
        printf("mknod(%s, %04o) -> %d\n", path, mode, -1);
        return -1;
    }
    printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    fflush(stdout);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nufs_mkdir(const char *path, mode_t mode) {
    int rv = nufs_mknod(path, mode | 040000, 0);
    inode *ptr = pathToINode(path);
    if (ptr == 0) {
        perror("Mkdir path lookup failed");
        return -1;
    }
    ptr->ptrs[0] = alloc_page();
    ptr->size = 4096;
    void *dataPg = pages_get_page(ptr->ptrs[0]);
    if (dataPg != NULL) {
        dirent *cur = (dirent *) dataPg;
        for (int i = 0; i < (PAGE_SIZE / sizeof(dirent)); ++i) {
            cur[i].inum = -1;
        }
    }
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}


int
nufs_link(const char *from, const char *to) {
    int rv = -1;

    inode *dd = pathToLastItemContainer(from);

    int iNodeNumber = directory_lookup_inode(dd, getTextAfterLastSlash(from));

    inode *file = pathToINode(from);
    if (file == 0) {
        perror("Unable to find matching file in link");
        return -1;
    }

    inode *toDir = pathToLastItemContainer(to);


    int dirOut = directory_put(toDir, getTextAfterLastSlash(to), iNodeNumber);

    if (dirOut < 0) {
        return -1;
    }


    file->refs++;
    rv = 0;

    printf("link(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nufs_rename(const char *from, const char *to) {
    int rv = -1;
//    void *inodes = pages_get_page(1);
//    inode *dirInode = (inode *) inodes;

    inode *dirInodeFrom = pathToLastItemContainer(from);
    inode *dirInodeTo = pathToLastItemContainer(to);

    char *fileNameFrom = getTextAfterLastSlash(from);
    char *fileNameTo = getTextAfterLastSlash(to);

    int fromINode = directory_lookup_inode(dirInodeFrom, fileNameFrom);
    int toINode = directory_lookup_inode(dirInodeTo, fileNameTo);


    //-- Check errors --

    // Both directory paths exist?
    if (dirInodeFrom == NULL || dirInodeTo == NULL) {
        return -1;
    }
    // from directory has the file, to directory does not
    if (!(fromINode >= 0 && toINode < 0)) {
        return -1;
    }

    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }
    dirInodeFrom->last_view = ts.tv_sec;
    dirInodeFrom->last_change = ts.tv_sec;

    dirInodeTo->last_view = ts.tv_sec;
    dirInodeTo->last_change = ts.tv_sec;

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
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }

    inode *node = pathToINode(path);
    node->mode = mode;
    node->last_change = ts.tv_sec;
    int rv = 0;
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int
nufs_truncate_expand(const char *path, off_t size) {
    puts("CALL TO TRUNCATE EXPAND");
    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        puts("time error");
        return -1;
    }
    inode *fptr = pathToINode(path);
    if (fptr == 0) {
        perror("path not found");
        return -1;
    }

    fptr->last_change = ts.tv_sec;

    int currPage = bytes_to_pages(fptr->size);
    int goalPages = bytes_to_pages(size);
    int pagesToAdd = goalPages - currPage;
    int sizeToAdd = size - fptr->size;

    printf("currNodeSize: %zu and size %li\n", fptr->size, size);
    printf("currPage %i, goalPages %i, pagesToAdd %i, sizeToAdd %i\n", currPage, goalPages, pagesToAdd,
           sizeToAdd);

    while (pagesToAdd > 0) {
        printf("loop on line 366 with pagesTo Add: %i and size to Add:%i\n", pagesToAdd, sizeToAdd);

        if (currPage == 0) {
            fptr->ptrs[0] = alloc_page();
            sizeToAdd = max(sizeToAdd - PAGE_SIZE, 0);
        }

        //UPDATE THE SIZE
        if (currPage == 1) {
            puts("currPAge 1");

            if (fptr->ptrs[0] == 0) {
                fptr->ptrs[0] = alloc_page();
            }


            void *pg = pages_get_page(fptr->ptrs[0]);

            long currSizeToAdd = min(sizeToAdd, PAGE_SIZE - fptr->size);

            memset(pg + fptr->size, 0, currSizeToAdd);//sizeLeft);

            fptr->size += currSizeToAdd;
            sizeToAdd -= currSizeToAdd;

        } else if (currPage == 2) {//numPages == 2) {
            puts("currPAge 2");

            if (fptr->ptrs[1] == 0) {
                fptr->ptrs[1] = alloc_page();
            }

            void *pg = pages_get_page(fptr->ptrs[1]);

            long currSizeToAdd = min(sizeToAdd, (2 * PAGE_SIZE) - fptr->size);


            memset(pg + (fptr->size - PAGE_SIZE), 0, currSizeToAdd);//sizeLeft);
            //                     ^^^^ how far into 2nd pg we are

            fptr->size += currSizeToAdd;
            sizeToAdd -= currSizeToAdd;

        } else {
            puts("currPAge Else");

            if (fptr->iptr == 0) {
                fptr->iptr = alloc_page();
            }

            int indirectPoints = fptr->iptr;
            int *indirectPage = pages_get_page(indirectPoints);

            if (indirectPage == 0) {
                perror("Page lookup failed");
                return -1;
            }

            int *curr_index_pointer = indirectPage;
            int index = 0;
            while (*curr_index_pointer != 0) {
                printf("loop of while (%i != 0) wiht index %i\n", *curr_index_pointer, index);
                curr_index_pointer = curr_index_pointer + 1;
                index++;

                if (index == PAGE_SIZE / sizeof(int)) {
                    puts("PAGE OVERFLOW");
                    return -1;
                }
            }
            index--;

            //index is last allocated index on indirect page

            if (index == -1) {
                indirectPage[0] = alloc_page();
                index = 0;

            }


            int *currPointer = indirectPage + index;
            int currPAge = *(currPointer);
            void *pg = pages_get_page(currPAge);

            long currSizeToAdd = min(sizeToAdd, (PAGE_SIZE * (index + 3)) - fptr->size);


            memset(pg + (fptr->size % PAGE_SIZE), 0, currSizeToAdd);//sizeLeft);

            if (sizeToAdd + (fptr->size % PAGE_SIZE) > PAGE_SIZE) {
                printf("if (%li == PAGE_SIZE)", currSizeToAdd);
                indirectPage[index + 1] = alloc_page();
            }

            fptr->size += currSizeToAdd;
            sizeToAdd -= currSizeToAdd;
        }

        currPage++;
        pagesToAdd--;
    }

    fptr->size = size;
    int rv = 0;
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;

}

int
nufs_truncate_remove(const char *path, off_t size) {
    puts("CALL TO TRUNCATE");
    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        puts("time error");
        return -1;
    }
    inode *fptr = pathToINode(path);
    if (fptr == 0) {
        perror("path not found");
        return -1;
    }

    fptr->last_change = ts.tv_sec;

    size_t currNodeSize = fptr->size;
    int currPage = bytes_to_pages(currNodeSize);
    int goalPages = bytes_to_pages(size);
    int pagesToRemove = currPage - goalPages;
    int sizeToRemove = currNodeSize - size;

    printf("currNodeSize: %zu and size %li\n", currNodeSize, size);
    printf("currPage %i, goalPages %i, pagesToRemove %i, sizeToRemove  %i\n", currPage, goalPages, pagesToRemove,
           sizeToRemove);

    while (pagesToRemove > 0) {
        printf("loop on line 366 with pagesToRemove: %i and size to remove:%i\n", pagesToRemove, sizeToRemove);

        //UPDATE THE SIZE
        if (currPage == 1) {
            puts("currPAge 1");
            void *pg = pages_get_page(fptr->ptrs[0]);

            long currSizeToRemove = min(sizeToRemove, PAGE_SIZE);

            memset(pg + PAGE_SIZE - currSizeToRemove, 0, currSizeToRemove);//sizeLeft);

            if (currSizeToRemove == PAGE_SIZE) {
                free_page(fptr->ptrs[0]);
                fptr->ptrs[0] = 0;
            }

            fptr->size -= currSizeToRemove;
            sizeToRemove -= currSizeToRemove;

        } else if (currPage == 2) {//numPages == 2) {
            puts("currPAge 2");
            void *pg = pages_get_page(fptr->ptrs[1]);

            long currSizeToRemove = min(sizeToRemove, PAGE_SIZE);


            memset(pg + PAGE_SIZE - currSizeToRemove, 0, currSizeToRemove);//sizeLeft);

            if (currSizeToRemove == PAGE_SIZE) {
                free_page(fptr->ptrs[1]);
                fptr->ptrs[1] = 0;
            }

            fptr->size -= currSizeToRemove;
            sizeToRemove -= currSizeToRemove;

            if (fptr->iptr != 0) {
                free_page(fptr->iptr);
                fptr->iptr = 0;
            }

        } else {
            puts("currPAge Else");
            int indirectPoints = fptr->iptr;
            int *indirectPage = pages_get_page(indirectPoints);

            if (indirectPage == 0) {
                perror("Page lookup failed");
                return -1;
            }

            int *curr_index_pointer = indirectPage;
            int index = 0;
            while (*curr_index_pointer != 0) {
                printf("loop of while (%i != 0) wiht index %i\n", *curr_index_pointer, index);
                curr_index_pointer = curr_index_pointer + 1;
                index++;

                if (index == PAGE_SIZE / sizeof(int)) {
                    puts("PAGE OVERFLOW");
                    return -1;
                }
            }
            index--;

            //index is last allocated indx on indirect page
            if (index == -1) {
                int indirect = fptr->iptr;
                pages_free(indirect);
                fptr->iptr = 0;

                currPage--;
                pagesToRemove--;
                continue;
            }


            int *currPointer = indirectPage + index;
            int currPAge = *(currPointer);
            void *pg = pages_get_page(currPAge);

            long currSizeToRemove = min(sizeToRemove, PAGE_SIZE);

            memset(pg + PAGE_SIZE - currSizeToRemove, 0, currSizeToRemove);//sizeLeft);

            if (currSizeToRemove == PAGE_SIZE) {
                printf("if (%li == PAGE_SIZE)", currSizeToRemove);
                free_page(currPAge);
                memset(currPointer, 0, sizeof(int));//sizeLeft);
            }

            fptr->size -= currSizeToRemove;
            sizeToRemove -= currSizeToRemove;
        }

        currPage--;
        pagesToRemove--;
    }

    fptr->size = size;
    int rv = 0;
    printf("truncate(%s, %ld bytes) -> %d\n", path, size, rv);
    return rv;
}


int
nufs_truncate(const char *path, off_t size) {
    inode *node = pathToINode(path);
    if (node == 0) {
        return -1;
    }

    if (size > node->size) {
        nufs_truncate_expand(path, size);
    } else if (size == node->size) {
        return 0;
    } else {
        nufs_truncate_remove(path, size);
    }
}

int
nufs_unlink(const char *path) {
    int rv = -1;

    inode *dirPtr = pathToLastItemContainer(path);
    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    dirPtr->last_change = ts.tv_sec;

    char *fileName = getTextAfterLastSlash(path);
    int inodeNum = directory_lookup_inode(dirPtr, fileName);
    //int inodeNum = directory_delete(dirPtr, fileName);
    assert(inodeNum >= 0);

    inode *fileptr = get_inode(inodeNum);
    fileptr->last_change = ts.tv_sec;

    fileptr->refs--;

    if (fileptr->refs == 0) {
        directory_delete(dirPtr, fileName);

        nufs_truncate_remove(path, 0);

        bitmap_put(get_inode_bitmap(), inodeNum, 0);
    }

    rv = 0;
    printf("unlink(%s) -> %d\n", path, rv);
    return rv;
}

//note this is only called by nufs when directory is empty
int
nufs_rmdir(const char *path) {
    int rv = -1;

    slist *contents = directory_list(path);
    if (contents != NULL) {
        return -1;
    }
    rv = nufs_unlink(path);
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nufs_open(const char *path, struct fuse_file_info *fi) {
    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);
    if (rv2 < 0) {
        return -1;
    }

    inode *node = pathToINode(path);
    node->last_view = ts.tv_sec;

    int rv = 0;
    printf("open(%s) -> %d\n", path, rv);
    return rv;
}


int read_indirect_page(inode *fptr, char *buf, size_t size, off_t offset) {//sizeLeft) {
    //printf("---indirect---- size = %ld || off = %ld\n", size, offset);
    off_t offHere = offset; //- (2 * PAGE_SIZE); //already checked 2 pages by the time we got here
    void *metaPg = pages_get_page(fptr->iptr);
    if (metaPg < 0) {
        return -1;
    }
    int *metaPgInt = (int *) metaPg;
    off_t i = offHere / PAGE_SIZE;

    //printf("offset = %ld ||| offhere = %ld\n", offset, offHere);
    size_t sizeLeft = size;
    size_t sizeRead = 0;
    // Add the data
    while (sizeLeft > 0) {
        //printf("sizeleft = %ld\n", sizeLeft);
        int nextPgIdx = metaPgInt[i];
        void *curPg = pages_get_page(nextPgIdx);
        if (offHere < PAGE_SIZE) {
            offHere = max(0, offHere);

            size_t num_to_Read = min(PAGE_SIZE, max(sizeLeft + offHere, 0));
            printf("reading in %li page of size %zu\n", i, num_to_Read);

            memcpy(buf, curPg + offHere, num_to_Read);

            sizeRead += num_to_Read;
            buf += num_to_Read;
            i++;
            sizeLeft -= num_to_Read;

            offHere = 0;

        } else {
            offHere -= PAGE_SIZE;
        }
    }
    return sizeRead;
}

//analogous to writing (ie. in PAGE_SIZE byte chunks)
int read_pages(inode *fptr, char *buf, size_t size, off_t offset) {
    printf("Reading in size and offset %zu %li \n", size, offset);

    int numPages = bytes_to_pages(fptr->size + offset);
    int sizeLeft = size;//fptr->size;
    int sizeRead = 0;

    size_t mutated_offset = offset;

    if (numPages == 1 && offset < PAGE_SIZE) {
        puts("read from first page");
        void *pg = pages_get_page(fptr->ptrs[0]);
        size_t num_to_Read = min(PAGE_SIZE, max(sizeLeft + mutated_offset, 0));
        printf("reading in first page of size %zu and returning\n", num_to_Read);


        memcpy(buf, pg + mutated_offset, num_to_Read);
        sizeRead += num_to_Read;
        return sizeRead;//fptr->size;
    }
    if (numPages > 1 && offset < PAGE_SIZE) {
        void *pg = pages_get_page(fptr->ptrs[0]);

        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("reading in first page of size %zu\n", num_to_Read);

        memcpy(buf, pg + mutated_offset, num_to_Read);

        sizeRead += num_to_Read;
        sizeLeft -= num_to_Read;
        buf += num_to_Read;
    }

    mutated_offset = max(0, (mutated_offset - PAGE_SIZE));

    if (numPages == 2 && offset < (2 * PAGE_SIZE)) {
        puts("read from second page");
        void *pg = pages_get_page(fptr->ptrs[1]);
        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("reading in second page of size %zu and returning \n", num_to_Read);

        memcpy(buf, pg + mutated_offset, num_to_Read);

        sizeRead += num_to_Read;
        return sizeRead;
    }
    if (numPages > 2 && offset < (2 * PAGE_SIZE)) {
        void *pg = pages_get_page(fptr->ptrs[1]);

        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("reading in second page of size %zu\n", num_to_Read);

        memcpy(buf, pg + mutated_offset, num_to_Read);

        sizeLeft -= num_to_Read;
        sizeRead += num_to_Read;
        buf += num_to_Read;
    }

    mutated_offset = max(0, (mutated_offset - PAGE_SIZE));

    int rv = read_indirect_page(fptr, buf, sizeLeft, max(0, mutated_offset));//size, offset);//sizeLeft);

    if (rv < 0) {
        perror("read indirect pages failed");
        return -1;
    }

    printf("size read %i and rv is %i", sizeRead, rv);
    return sizeRead + rv;// + PAGE_SIZE + PAGE_SIZE;
}


// Actually read data
int
nufs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("Call to read of size %zu and offset%li\n", size, offset);

    int rv = -1;
    inode *fptr = pathToINode(path);

    struct timespec ts;
    int rv2 = clock_gettime(CLOCK_REALTIME, &ts);

    if (rv2 < 0) {
        printf("readdir(%s) -> %d with clock restult %i and node result\n", path, -1, rv2);
        return -1;
    }

    if (fptr != NULL) {

        if (fptr->size < offset) {
            perror("File offset for read was greater than the file size");
            return 0;
        }

        fptr->last_change = ts.tv_sec;
        rv = read_pages(fptr, buf, size, offset);
    }

    printf("read(%s, %ld bytes, @+%ld) -> %d\n\n", path, size, offset, rv);
    return rv;
}

//could probably use offset bytes_to_page to figure out where we write
//instead of checking for == 0
int write_indirect_page(inode *fptr, const char *buf, size_t size, off_t offset) {//sizeLeft) {
    off_t offHere = offset; //- (2 * PAGE_SIZE); //already checked 2 pages by the time we got here

    if (fptr->iptr == 0) {
        fptr->iptr = alloc_page();
    }

    void *metaPg = pages_get_page(fptr->iptr);
    if (metaPg < 0) {
        return -1;
    }
    int *metaPgInt = (int *) metaPg;
    off_t i = offHere / PAGE_SIZE;

    //printf("offset = %ld ||| offhere = %ld\n", offset, offHere);
    long sizeLeft = size;
    size_t sizeRead = 0;
    // Add the data
    while (sizeLeft > 0) {
        printf("sizeleft = %ld\n", sizeLeft);
        if (metaPgInt[i] == 0) {


            int nextPgIdx = alloc_page();

            if (nextPgIdx < 0) {
                return -1;
            }

            metaPgInt[i] = nextPgIdx;
        }

        void *curPg = pages_get_page(metaPgInt[i]);
        if (offHere < PAGE_SIZE) {
            offHere = max(0, offHere);

            size_t num_to_Write = min(PAGE_SIZE, max(sizeLeft + offHere, 0));
//            printf("size plus offset is %lu\n", sizeLeft);
            printf("writing in %li page of size %zu\n", i, num_to_Write);

            memcpy(curPg + offHere, buf, num_to_Write);

            sizeRead += num_to_Write;
            buf += num_to_Write;
            i++;
            sizeLeft -= num_to_Write;

            offHere = 0;

        } else {
            puts("skipping becasue of offset");
            offHere -= PAGE_SIZE;
        }
    }
    return sizeRead;
}


//this doesn't really use offset.... is that a problem?
//see note above, could probably  use to figure out what page to write to immediately
int write_pages(inode *fptr, const char *buf, size_t size, off_t offset) {
    printf("TO WRITE %zu with offset %zu\n", size, offset);

    int numPages = bytes_to_pages(size + offset);
    int sizeLeft = size;//fptr->size;
    int sizeRead = 0;

    size_t mutated_offset = offset;

    if (numPages == 1 && offset < PAGE_SIZE) {
        puts("write to first page");
        if (fptr->ptrs[0] == 0) {
            fptr->ptrs[0] = alloc_page();
        }
        void *pg = pages_get_page(fptr->ptrs[0]);
        size_t num_to_Read = min(PAGE_SIZE, max(sizeLeft + mutated_offset, 0));
        printf("write in first page of size %zu and returning\n", num_to_Read);


        memcpy(pg + mutated_offset, buf, num_to_Read);
        sizeRead += num_to_Read;
        return sizeRead;//fptr->size;
    }
    if (numPages > 1 && offset < PAGE_SIZE) {
        if (fptr->ptrs[0] == 0) {
            fptr->ptrs[0] = alloc_page();
        }
        void *pg = pages_get_page(fptr->ptrs[0]);

        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("write in first page of size %zu\n", num_to_Read);

        memcpy(pg + mutated_offset, buf, num_to_Read);

        sizeRead += num_to_Read;
        sizeLeft -= num_to_Read;
        buf += num_to_Read;
    }

    mutated_offset = max(0, (mutated_offset - PAGE_SIZE));


    if (numPages == 2 && offset < (2 * PAGE_SIZE)) {
        puts("write from second page");
        if (fptr->ptrs[1] == 0) {
            fptr->ptrs[1] = alloc_page();
        }
        void *pg = pages_get_page(fptr->ptrs[1]);
        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("write in second page of size %zu and returning \n", num_to_Read);

        memcpy(pg + mutated_offset, buf, num_to_Read);

        sizeRead += num_to_Read;
        return sizeRead;
    }
    if (numPages > 2 && offset < (2 * PAGE_SIZE)) {
        if (fptr->ptrs[1] == 0) {
            fptr->ptrs[1] = alloc_page();
        }
        void *pg = pages_get_page(fptr->ptrs[1]);

        size_t num_to_Read = max(0, min(sizeLeft + mutated_offset, PAGE_SIZE));
        printf("write in second page of size %zu\n", num_to_Read);

        memcpy(pg + mutated_offset, buf, num_to_Read);

        sizeLeft -= num_to_Read;
        sizeRead += num_to_Read;
        buf += num_to_Read;
    }

    mutated_offset = max(0, (mutated_offset - PAGE_SIZE));


    int rv = write_indirect_page(fptr, buf, sizeLeft, max(0, mutated_offset));//size, offset);//sizeLeft);
    if (rv < 0) {
        perror("write indirect pages failed");
        return -1;
    }

    return sizeRead + rv;// + PAGE_SIZE + PAGE_SIZE;
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


        struct timespec ts;
        rv = clock_gettime(CLOCK_REALTIME, &ts);
        if (rv < 0) {
            puts("nufs write time stamp error");
            return -1;
        }

        rv = write_pages(fptr, buf, size, offset);

        if (offset + size >= fptr->size) {
            fptr->size = offset + size;
        }
        fptr->last_change = ts.tv_sec;

        //assert the copy didn't fail?
        //rv = size;
    }

    printf("write(%s, %ld bytes, @+%ld) -> %d\n", path, size, offset, rv);
    return rv;
}

int changeTimeStamp(const char *path, const struct timespec ts[2]) {
    inode *thing = pathToINode(path);
    thing->last_view = ts[0].tv_sec;
    thing->last_change = ts[1].tv_sec;
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

const int symLinkModeNumber = 0120000;

int nufs_symlink(const char *to, const char *from) {

    if (strlen(from) == 0 || strlen(to) == 0) {
        perror("to or from is too short in symlink");
        return -ENOENT;
    }

    int rv = -1;
    rv = nufs_mknod(from, symLinkModeNumber, 0);
    if (rv != 0) {
        perror("symLink creating not sucessful");
        return -1;
    }

    printf("length of to is %lu", strlen(to));
    rv = nufs_write(from, to, strlen(to), 0, 0);
    if (rv <= 0) {
        rv = -1;
    } else {
        rv = 0;
    }

    printf("symlink to %s from %s -> %i\n", to, from, rv);
    return rv;
}


int nufs_readlink(const char *path, char *buf, size_t size) {
    int rv = -1;

    printf("size in readLink %zu\n", size);

    rv = nufs_read(path, buf, size, 0, 0);

    rv = 0;
    printf("readLinek path %s size %zu -> -%i\n", path, size, rv);
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
    ops->readlink = nufs_readlink;
    ops->symlink = nufs_symlink;
};

struct fuse_operations nufs_ops;

int
main(int argc, char *argv[]) {
    assert(argc > 2 && argc < 6);
    storage_init(argv[--argc]);
    nufs_init_ops(&nufs_ops);
    return fuse_main(argc, argv, &nufs_ops, NULL);
}

