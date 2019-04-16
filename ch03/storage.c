// based on cs3650 starter code

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "storage.h"
#include "pages.h"
#include "directory.h"

void storage_init(const char *path) {
    pages_init(path);
    //setup root dir
    directory_init();
}