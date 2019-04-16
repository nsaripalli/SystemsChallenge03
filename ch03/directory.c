// based on cs3650 starter code


#define DIR_NAME 48

#include <string.h>
#include "directory.h"
#include "util.h"
#include <errno.h>
#include "slist.h"
#include "pages.h"
#include "inode.h"
#include "bitmap.h"


/*typedef struct dirent {
    char name[DIR_NAME];
    int  inum;
    char _reserved[12];
} dirent;*/

static const int MAX_DIR_ENTRIES = 4096 / sizeof(dirent);


void directory_init() {
    inode *inodes = (inode *) pages_get_page(1);
    if (inodes[0].refs == 1) {
//        puts("directory already init'd");
        return;
    }

    struct timespec ts;
    int rv = clock_getres(CLOCK_REALTIME, &ts);

    if (rv < 0) {
        return;
    }

    //Root node can never be deleted
    inodes[0].refs = 1;
    inodes[0].mode = 040755;
    inodes[0].size = 4096;
    inodes[0].ptrs[0] = alloc_page();
    inodes[0].ptrs[1] = 0;
    inodes[0].iptr = 0;
    inodes[0].last_change = ts.tv_sec;
    inodes[0].last_view = ts.tv_sec;
    inodes[0].creation_time = ts.tv_sec;


    bitmap_put(get_inode_bitmap(), 0, 1);
    bitmap_put(get_pages_bitmap(), inodes[0].ptrs[0], 1);

    // mark all entries in this directory as empty/unset
    void *datapg = pages_get_page(inodes[0].ptrs[0]);
    dirent *cur = (dirent *) datapg;
    for (int i = 0; i < MAX_DIR_ENTRIES; ++i) {
        cur[i].inum = -1;
    }
}

int directory_lookup_page(void *entryPage, const char *name) {
    dirent *cur = (dirent *) entryPage;
    int cntr = 0;
    while (cntr < MAX_DIR_ENTRIES) {
        if (cur->inum != -1 && streq(cur->name, name)) {
            return cntr;
        }
        cur++;
        cntr++;
    }
    return -1;
}

// Returns the index in the directory
int directory_lookup(inode *dd, const char *name) {
    // Look in first direct page
    int pgRes = directory_lookup_page(pages_get_page(dd->ptrs[0]), name);
    if (pgRes == -1 && dd->size >= (4096 * 2)) {
        //Didn't find in first direct page, check second
        pgRes = directory_lookup_page(pages_get_page(dd->ptrs[1]), name);
        if (pgRes != -1) {
            return MAX_DIR_ENTRIES + pgRes;
        }
    }
    return pgRes;
}

// Returns the inode of the item in the directory, or -1
int directory_lookup_inode(inode *dd, const char *name) {
    // Look in first direct page
    int pgRes = directory_lookup_page(pages_get_page(dd->ptrs[0]), name);
    if (pgRes != -1) {
        return ((dirent *) pages_get_page(dd->ptrs[0]))[pgRes].inum;
    }
    if (dd->size >= (4096 * 2)) {
        //Didn't find in first direct page, check second
        pgRes = directory_lookup_page(pages_get_page(dd->ptrs[1]), name);
        if (pgRes != -1) {
            return ((dirent *) pages_get_page(dd->ptrs[1]))[pgRes].inum;
        }
    }
    return pgRes;
}



//int tree_lookup(const char* path);


int directory_put_page(int dataPgIdx, const char *name, int inum) {
    void *dataPgPtr = pages_get_page(dataPgIdx);
    dirent *cur = (dirent *) dataPgPtr;
    int cntr = 0;
    int spotFound = 0;
    while (cntr < MAX_DIR_ENTRIES) {
        if (cur->inum == -1 /*|| cur->inum == 0*/) {
            strcpy(cur->name, name);
            cur->inum = inum;
            return cntr;
        }
        cur++;
        cntr++;
    }
    return -1;
}

// Returns the index where we put it in the directory
int directory_put(inode *dd, const char *name, int inum) {
    int tryPg = directory_put_page(dd->ptrs[0], name, inum);
    if (tryPg == -1 && dd->size <= 4096  /*&& dd->ptrs[1]==0*/) { //this was a ?
        dd->ptrs[1] = alloc_page();

        // mark all entries in this directory as empty/unset
        void *datapg = pages_get_page(dd->ptrs[1]);
        dirent *cur = (dirent *) datapg;
        for (int i = 0; i < MAX_DIR_ENTRIES; ++i) {
            cur[i].inum = -1;
        }


        dd->size += 4096;
    }
    if (tryPg == -1 && dd->size >= (4096 * 2)) {
        tryPg = directory_put_page(dd->ptrs[1], name, inum);
        if (tryPg == -1) {
            return -1;
        } else {
            return MAX_DIR_ENTRIES + tryPg;
        }
    }
    return tryPg;
}

int directory_delete_page(int dataPgIdx, const char *name) {
    void *dataPgPtr = pages_get_page(dataPgIdx);
    dirent *cur = (dirent *) dataPgPtr;
    int oldINum = -1;
    int cntr = 0;
    while (cntr < MAX_DIR_ENTRIES) {
        if (cur->inum != -1 && streq(cur->name, name)) {
            oldINum = cur->inum;
            cur->inum = -1;
            break;
        }
        cur++;
        cntr++;
    }
    return oldINum;
}

// delete the corresponding entry (no leading /) from the directory datapage
// returns the inode number. -1 if wasn't found
int directory_delete(inode *dd, const char *name) {
    int PAGE_SIZE = 4096;

    int tryPg1 = directory_delete_page(dd->ptrs[0], name);
    if (tryPg1 == -1 && dd->size >= (PAGE_SIZE * 2)) {
        return directory_delete_page(dd->ptrs[1], name);
    }
    return tryPg1;
}


// Adds to input slist* the list for the given dir page
slist *directory_list_page(slist *list, int dataPgIdx) {
    void *dataPgPtr = pages_get_page(dataPgIdx);
    dirent *cur = (dirent *) dataPgPtr;
    int cntr = 0;
    while (cntr < MAX_DIR_ENTRIES) {
        if (/*cur->inum != 0 && */cur->inum != -1) {
            printf("inum = %d . added |%s| to list\n", cur->inum, cur->name);
            list = s_cons(cur->name, list);
        }
        cntr++;
        cur++;
    }
    return list;
}


// from https://www.tutorialspoint.com/c_standard_library/c_function_strtok.htm
// This gets the inode of the last item in path
inode *pathToDir(const char *path) {
    slist *p = s_split(path, '/');
    inode *rootDir = (inode *) pages_get_page(1);
    inode *cur = rootDir;
    inode *prev = rootDir;

    if (streq(p->data, "")) {
        p = p->next;
    }
    while (p != NULL) {
        //printf("Looking for |%s|\n", p->data);
        int dirIdx = directory_lookup(cur, p->data);
        void *dirData = NULL;
        if (dirIdx < MAX_DIR_ENTRIES) {
            dirData = pages_get_page(cur->ptrs[0]);
        } else {
            dirData = pages_get_page(cur->ptrs[1]);
            dirIdx -= MAX_DIR_ENTRIES; //indx in 2nd pg
        }
        dirent *curEntries = (dirent *) dirData;
        //printf("found entry --- %s\n", curEntries[dirIdx].name);
        int inodeNum = curEntries[dirIdx].inum;
        prev = cur;
        cur = /*rootDir*/ ((inode *) pages_get_page(1)) + inodeNum; // use the inode page!
        print_inode(cur);
        p = p->next;
    }
    return cur;
}


//i know this is almost identical to above. just want to see if it works
//abstraction later....
//This function gets the inode the contains the last item
inode *pathToLastItemContainer(const char *path) {
    slist *p = s_split(path, '/');
    inode *rootDir = (inode *) pages_get_page(1);
    inode *cur = rootDir;
    inode *prev = rootDir;

    if (streq(p->data, "")) {
        p = p->next;
    }
    while (p != NULL) {
        //printf("Looking for |%s|\n", p->data);
        int dirIdx = directory_lookup(cur, p->data);
        void *dirData = NULL;
        if (dirIdx < MAX_DIR_ENTRIES) {
            dirData = pages_get_page(cur->ptrs[0]);
        } else {
            dirData = pages_get_page(cur->ptrs[1]);
            dirIdx -= MAX_DIR_ENTRIES; //indx in 2nd pg
        }
        dirent *curEntries = (dirent *) dirData;
        //printf("found entry --- %s\n", curEntries[dirIdx].name);
        int inodeNum = curEntries[dirIdx].inum;
        prev = cur;
        cur = /*rootDir*/ ((inode *) pages_get_page(1)) + inodeNum; //use inode page
        print_inode(cur);
        p = p->next;
    }
    return prev;
}


// Lists directory
slist *directory_list(const char *path) {
    //printf("making directory list for path: %s\n", path);
    inode *dirptr = pathToDir(path);
    //inode* dirptr = (inode*)pages_get_page(1);
    slist *out = NULL; //keep like this
    out = directory_list_page(out, dirptr->ptrs[0]);
    if (dirptr->size >= (2 * 4096)) {
        out = directory_list_page(out, dirptr->ptrs[1]);
    }
    return out;
}

//void print_directory(inode* dd);

