// based on cs3650 starter code

#ifndef UTIL_H
#define UTIL_H

#include <string.h>

static int
streq(const char* aa, const char* bb)
{
    return strcmp(aa, bb) == 0;
}

static int
min(int x, int y)
{
    return (x < y) ? x : y;
}

static int
max(int x, int y)
{
    return (x > y) ? x : y;
}

static int
clamp(int x, int v0, int v1)
{
    return max(v0, min(x, v1));
}

static int
bytes_to_pages(int bytes)
{
    int quo = bytes / 4096;
    int rem = bytes % 4096;
    if (rem == 0) {
        return quo;
    }
    else {
        return quo + 1;
    }
}

static void
join_to_path(char* buf, char* item)
{
    int nn = strlen(buf);
    if (buf[nn - 1] != '/') {
        strcat(buf, "/");
    }
    strcat(buf, item);
}

//https://stackoverflow.com/questions/4770985/how-to-check-if-a-string-starts-with-another-string-in-c
static int startsWith(const char *pre, const char *str) {
    size_t lenpre = strlen(pre),
            lenstr = strlen(str);
    return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

static int startsWithOneNestedButNotEquel(const char *pre, const char *str) {
    if (streq(pre, str) == 1) {
        return 0;
    } else if (startsWith(pre, str)) {
        int letOfPre = strlen(pre);
        const char *strStartAfterPre = str + letOfPre + 1;
        char *indexOfFirstSlash = strrchr(strStartAfterPre, '/');
        if (indexOfFirstSlash == 0) {
            return 1;
        }
        return 0;
    } else {
        return 0;
    }
}


static char *getTextAfterLastSlash(const char *txt) {
    char *out = strrchr(txt, '/');
    if (out == 0) {
        perror("unable to get last /");
    }
    return out + 1;
}



#endif
