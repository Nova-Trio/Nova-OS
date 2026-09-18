#pragma once
#include <stddef.h>
#include <stdint.h>

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int64_t read(int fd, void *buf, size_t count);
int64_t write(int fd, const void *buf, size_t count);
int close(int fd);
int64_t lseek(int fd, int64_t offset, int whence);
