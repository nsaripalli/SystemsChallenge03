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
    inode* inodes = (inode*)pages_get_page(1);
    if(inodes[0].refs == 1) {
        //puts("directory already init'd");
	return;
    }
    
    inodes[0].refs = 1;
    inodes[0].mode = 040755;
    inodes[0].size = 4096;
    inodes[0].ptrs[0] = alloc_page();
    inodes[0].ptrs[1] = 0;
    inodes[0].iptr = 0;

    bitmap_put(get_inode_bitmap(), 0, 1);
    bitmap_put(get_pages_bitmap(), inodes[0].ptrs[0], 1);

    // mark all entries in this directory as empty/unset
    void* datapg = pages_get_page(inodes[0].ptrs[0]);
    dirent* cur = (dirent*)datapg;
    for(int i = 0; i < MAX_DIR_ENTRIES; ++i) {
	cur[i].inum = -1;
    }
}

int directory_lookup_page(void* entryPage, const char* name) {
    dirent* cur = (dirent*)entryPage;
    int cntr = 0;
    while(cntr < MAX_DIR_ENTRIES) {
        if(cur->inum != -1 && streq(cur->name, name)) {
	    return cntr;
	}
	cur++;
	cntr++;
    }
    return -1;
}

// Returns the index in the directory
int directory_lookup(inode* dd, const char* name) {
    // Look in first direct page
    int pgRes = directory_lookup_page(pages_get_page(dd->ptrs[0]), name);
    if(pgRes == -1 && dd->size >= (4096 * 2)) {
        //Didn't find in first direct page, check second
	pgRes = directory_lookup_page(pages_get_page(dd->ptrs[1]), name);
    }
    return pgRes;

/*    dirent* cur = (dirent*)pages_get_page(dd->ptrs[0]);
    int cntr = 0;
    while(cntr < MAX_DIR_ENTRIES && cur->inum != -1) {
        if(streq(cur->name, name)) { return cntr; }
	cur++;	cntr++;
    }
    return -1;*/
}

//int tree_lookup(const char* path);


int directory_put_page(int dataPgIdx, const char* name, int inum) {
    void* dataPgPtr = pages_get_page(dataPgIdx);
    dirent* cur = (dirent*)dataPgPtr;
    int cntr = 0;
    int spotFound = 0;
    while(cntr < MAX_DIR_ENTRIES) {
	if(cur->inum == -1) {
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
int directory_put(inode* dd, const char* name, int inum) {
    int tryPg = directory_put_page(dd->ptrs[0], name, inum);
    if(tryPg == -1 && dd->size >= (4096 * 2)) {
        tryPg = directory_put_page(dd->ptrs[1], name, inum);
	if(tryPg == -1) {
	    return -1;
	} else {
	    return MAX_DIR_ENTRIES + tryPg;
	}
    }
    return tryPg;
}


/* Now the directory can be left in a fragmented (ie. with gaps) state.
 * This is no longer needed.
 *
 *
// This coallesces a directory's data page
// That is, moves all data as far left as possible -> fills gaps, preserves order
void coallesce_dir(inode* dd) {
    int dataPgIdx = dd->ptrs[0];
    void* dataPgPtr = pages_get_page(dataPgIdx);
    dirent* cur = (dirent*)dataPgPtr;
    dirent* emptySpot = NULL;
    int cntr = 0;
    while(cntr < 64) {
        if(cur->inum == -1) {
	    if(emptySpot == NULL) {
		//found a gap
	        emptySpot = cur;
	    } else {
		//we've reached the end and are done
		//have an empty spot and cur (also empty) spot with nothing between
		//invariant - delete 1 file at a time, at most one gap in dir
		return;
	    }
	} else {
            // if emptySpot == NULL: we haven't found a gap yet
	    if(emptySpot != NULL) {
		//found a entry and have an empty spot to put it in
	        strncpy(emptySpot->name, cur->name, DIR_NAME);
		emptySpot->inum = cur->inum;
		//after the "move", we are left with a new empty spot here
		emptySpot = cur;
	    }
	}
	cur++;
	cntr++;
    }
}*/


int directory_delete_page(int dataPgIdx, const char* name) {
    //int dataPgIdx = dd->ptrs[0];
    void* dataPgPtr = pages_get_page(dataPgIdx);
    dirent* cur = (dirent*)dataPgPtr;
    int oldINum = -1;
    int cntr = 0;
    while(cntr < MAX_DIR_ENTRIES ) {
        if(cur->inum != -1 && streq(cur->name, name)) {
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
int directory_delete(inode* dd, const char* name) {
    int tryPg1 = directory_delete_page(dd->ptrs[0], name);
    if(tryPg1 == -1 && dd->size >= (4096 * 2)) {
        return directory_delete_page(dd->ptrs[1], name);
    }
    return tryPg1;
}


// Adds to input slist* the list for the given dir page
slist* directory_list_page(slist* list, int dataPgIdx) {
    void* dataPgPtr = pages_get_page(dataPgIdx);
    dirent* cur = (dirent*)dataPgPtr;
    int cntr = 0;
    while(cntr < MAX_DIR_ENTRIES) {
        if(cur->inum != -1) {
	    list = s_cons(cur->name, list);
	}
	cntr++;
	cur++;
    }
    return list;
}


// Lists directory
slist* directory_list(const char* path) {
    inode* dirptr = (inode*)pages_get_page(1);
    slist* out = NULL; //keep like this
    out = directory_list_page(out, dirptr->ptrs[0]);
    if(dirptr->size >= (2 * 4096)) {
        out = directory_list_page(out, dirptr->ptrs[1]);
    }
    return out;
}

//void print_directory(inode* dd);

