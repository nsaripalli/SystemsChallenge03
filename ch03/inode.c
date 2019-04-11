#include "inode.h"
#include "pages.h"
#include "assert.h"

/*typedef struct inode {
    int refs; // reference count
    int mode; // permission & type
    int size; // bytes
    int ptrs[2]; // direct pointers
    int iptr; // single indirect pointer
} inode;*/

void print_inode(inode* node) {
    printf("inode -- refs: %d, mode: %d, size: %d, ptrs[%d, %d], iptr: %d\n",
		    node->refs, node->mode, node->size, node->ptrs[0], node->ptrs[1], node->iptr);
}

inode* get_inode(int inum) {
    assert(inum >= 0);
    inode* inodePg = (inode*)pages_get_page(1);
    return inodePg + inum;
}

int alloc_inode();

void free_inode();

int grow_inode(inode* node, int size);

int shrink_inode(inode* node, int size);

int inode_get_pnum(inode* node, int fpn);
