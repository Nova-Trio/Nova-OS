#pragma once

#define O_RDONLY 00000000
#define O_WRONLY 00000001
#define O_RDWR 00000002

int open(const char *pathname, int flags, ...);
