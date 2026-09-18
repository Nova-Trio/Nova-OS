#include "include/stdio.h"
#include "string.h"
#include "unistd.h"
#include <stdlib.h>

static void formatNum(uint64_t val, int base, int uppercase, char *out, size_t *out_idx, size_t max_len) {
  char buf[64];
  size_t i = 0;
  const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

  if (val == 0) {
    buf[i++] = '0';
  } else {
    while (val > 0) {
      buf[i++] = digits[val % base];
      val /= base;
    }
  }

  while (i > 0 && *out_idx + 1 < max_len) {
    out[(*out_idx)++] = buf[--i];
  }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  if (!str || size == 0) return 0;

  size_t idx = 0;
  for (size_t f = 0; format[f] != '\0' && idx + 1 < size; f++) {
    if (format[f] != '%') {
      str[idx++] = format[f];
      continue;
    }

    f++;
    if (format[f] == '\0') break;

    int isLong = 0;
    if (format[f] == 'l') {
      isLong = 1;
      f++;
      if (format[f] == 'l') {
        f++;
      }
    }

    switch (format[f]) {
      case 'c': {
        char c = (char)va_arg(ap, int);
        if (idx + 1 < size) str[idx++] = c;
        break;
      }
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (!s) s = "(null)";
        while (*s && idx + 1 < size) {
          str[idx++] = *s++;
        }
        break;
      }
      case 'd':
      case 'i': {
        int64_t v = isLong ? va_arg(ap, int64_t) : (int64_t)va_arg(ap, int);
        if (v < 0) {
          if (idx + 1 < size) str[idx++] = '-';
          v = -v;
        }
        formatNum((uint64_t)v, 10, 0, str, &idx, size);
        break;
      }
      case 'u': {
        uint64_t v = isLong ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
        formatNum(v, 10, 0, str, &idx, size);
        break;
      }
      case 'x': {
        uint64_t v = isLong ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
        formatNum(v, 16, 0, str, &idx, size);
        break;
      }
      case 'X': {
        uint64_t v = isLong ? va_arg(ap, uint64_t) : (uint64_t)va_arg(ap, unsigned int);
        formatNum(v, 16, 1, str, &idx, size);
        break;
      }
      case 'p': {
        uint64_t v = (uint64_t)va_arg(ap, void *);
        if (idx + 2 < size) {
          str[idx++] = '0';
          str[idx++] = 'x';
        }
        formatNum(v, 16, 0, str, &idx, size);
        break;
      }
      case '%': {
        if (idx + 1 < size) str[idx++] = '%';
        break;
      }
      default:
        if (idx + 1 < size) str[idx++] = format[f];
        break;
    }
  }

  str[idx] = '\0';
  return (int)idx;
}

int snprintf(char *str, size_t size, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  int ret = vsnprintf(str, size, format, ap);
  va_end(ap);
  return ret;
}

int printf(const char *format, ...) {
  char stackBuf[256];
  va_list ap;

  va_start(ap, format);
  va_list apCopy;
  va_copy(apCopy, ap);

  int len = vsnprintf(stackBuf, sizeof(stackBuf), format, ap);
  va_end(ap);

  if (len < (int)sizeof(stackBuf)) {
    va_end(apCopy);
    if (len > 0) {
      write(STDOUT_FILENO, stackBuf, (size_t)len);
    }
    return len;
  }

  char *dynBuf = (char *)malloc((size_t)len + 1);
  if (!dynBuf) {
    va_end(apCopy);
    write(STDOUT_FILENO, stackBuf, sizeof(stackBuf) - 1);
    return sizeof(stackBuf) - 1;
  }

  vsnprintf(dynBuf, (size_t)len + 1, format, apCopy);
  va_end(apCopy);

  write(STDOUT_FILENO, dynBuf, (size_t)len);
  free(dynBuf);

  return len;
}

int puts(const char *s) {
  size_t len = strlen(s);
  write(STDOUT_FILENO, s, len);
  write(STDOUT_FILENO, "\n", 1);
  return (int)(len + 1);
}

int putchar(int c) {
  char ch = (char)c;
  write(STDOUT_FILENO, &ch, 1);
  return c;
}
