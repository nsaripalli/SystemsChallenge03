cmake_minimum_required(VERSION 3.13)
project(starter_hw10_v2)

set(CMAKE_CXX_STANDARD 14)

include(FindPkgConfig)

#target_compile_options(-g `pkg-config fuse --cflags`)

add_executable(SystemsChallenge03
        bitmap.c
        directory.h
        inode.h
        pages.c
        pages.h
        slist.c
        slist.h
        storage.h
        util.h
        Makefile
        nufs.c
        test.pl)