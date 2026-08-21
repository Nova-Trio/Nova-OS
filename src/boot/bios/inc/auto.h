#pragma once
#include <stdint.h>

typedef struct StrSlice
{
    uint16_t len;
    const char* str;
} StrSlice;

const StrSlice DRIVER_LIST[] = {
    {9, "something"}
};

const StrSlice COMMANDLINE_ARGS[] = {
    {4, "bits"}, {2, "64"}
};
