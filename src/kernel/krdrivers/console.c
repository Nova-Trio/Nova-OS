#include "console.h"

#define FLAG_LEFT (1 << 0)
#define FLAG_PLUS (1 << 1)
#define FLAG_SPACE (1 << 2)
#define FLAG_HASH (1 << 3)
#define FLAG_ZERO (1 << 4)
#define FLAG_UPPER (1 << 5)

enum {
  LEN_DEFAULT,
  LEN_HH,
  LEN_H,
  LEN_L,
  LEN_LL,
  LEN_Z
};

static BootInfo *g_boot_info = 0;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t color_fg = 0x00FFFFFF;
static uint32_t color_bg = 0x00000000;

void console_init(BootInfo *boot_info) {
  g_boot_info = boot_info;
  cursor_x = 0;
  cursor_y = 0;
}

void console_set_color(uint32_t fg, uint32_t bg) {
  color_fg = fg;
  color_bg = bg;
}

void console_clear(void) {
  if (!g_boot_info) return;

  uint32_t *fb = (uint32_t *)g_boot_info->framebuffer.base;
  uint32_t pitch = g_boot_info->framebuffer.pixels_per_scanline;
  uint32_t width = g_boot_info->framebuffer.width;
  uint32_t height = g_boot_info->framebuffer.height;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      fb[y * pitch + x] = color_bg;
    }
  }

  cursor_x = 0;
  cursor_y = 0;
}

static void console_scroll(void) {
  uint32_t *fb = (uint32_t *)g_boot_info->framebuffer.base;
  uint32_t pitch = g_boot_info->framebuffer.pixels_per_scanline;
  uint32_t width = g_boot_info->framebuffer.width;
  uint32_t height = g_boot_info->framebuffer.height;
  uint8_t char_size = g_boot_info->font.header->charsize;

  uint32_t copy_height = height - char_size;
  for (uint32_t y = 0; y < copy_height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      fb[y * pitch + x] = fb[(y + char_size) * pitch + x];
    }
  }

  for (uint32_t y = copy_height; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      fb[y * pitch + x] = color_bg;
    }
  }

  cursor_y -= char_size;
}

static void draw_char(char c, uint32_t cx, uint32_t cy) {
  uint32_t *fb = (uint32_t *)g_boot_info->framebuffer.base;
  uint32_t pitch = g_boot_info->framebuffer.pixels_per_scanline;
  uint8_t char_size = g_boot_info->font.header->charsize;
  uint8_t *glyph = (uint8_t *)g_boot_info->font.glyph_buffer + ((uint8_t)c * char_size);

  for (uint32_t y = 0; y < char_size; y++) {
    for (uint32_t x = 0; x < 8; x++) {
      if ((glyph[y] >> (7 - x)) & 1) {
        fb[(cy + y) * pitch + (cx + x)] = color_fg;
      } else {
        fb[(cy + y) * pitch + (cx + x)] = color_bg;
      }
    }
  }
}

void console_putchar(char c) {
  if (!g_boot_info) return;

  uint8_t char_size = g_boot_info->font.header->charsize;

  if (c == '\n') {
    cursor_x = 0;
    cursor_y += char_size;
    if (cursor_y + char_size > g_boot_info->framebuffer.height) {
      console_scroll();
    }
    return;
  }

  if (c == '\r') {
    cursor_x = 0;
    return;
  }

  if (c == '\t') {
    cursor_x = (cursor_x + 8 * 4) & ~(8 * 4 - 1);
    if (cursor_x + 8 > g_boot_info->framebuffer.width) {
      cursor_x = 0;
      cursor_y += char_size;
      if (cursor_y + char_size > g_boot_info->framebuffer.height) {
        console_scroll();
      }
    }
    return;
  }

  if (cursor_x + 8 > g_boot_info->framebuffer.width) {
    cursor_x = 0;
    cursor_y += char_size;
  }

  if (cursor_y + char_size > g_boot_info->framebuffer.height) {
    console_scroll();
  }

  draw_char(c, cursor_x, cursor_y);
  cursor_x += 8;
}

static int print_padding(char pad_char, int count) {
  for (int i = 0; i < count; i++) {
    console_putchar(pad_char);
  }
  return count > 0 ? count : 0;
}

static int format_number(uint64_t val, int is_signed, int is_negative, int base, int flags, int width, int prec) {
  char buf[65];
  const char *digits = (flags & FLAG_UPPER) ? "0123456789ABCDEF" : "0123456789abcdef";
  int pos = 0;
  int written = 0;

  if (val == 0 && prec != 0) {
    buf[pos++] = '0';
  } else {
    while (val > 0) {
      buf[pos++] = digits[val % base];
      val /= base;
    }
  }

  char sign = 0;
  if (is_signed) {
    if (is_negative) sign = '-';
    else if (flags & FLAG_PLUS) sign = '+';
    else if (flags & FLAG_SPACE) sign = ' ';
  }

  const char *prefix = "";
  if (flags & FLAG_HASH) {
    if (base == 16 && pos > 0) prefix = (flags & FLAG_UPPER) ? "0X" : "0x";
    else if (base == 2 && pos > 0) prefix = "0b";
    else if (base == 8 && buf[pos - 1] != '0') prefix = "0";
  }

  int prefix_len = 0;
  while (prefix[prefix_len]) prefix_len++;
  if (sign) prefix_len++;

  int zeros = (prec > pos) ? (prec - pos) : 0;
  int total_len = prefix_len + zeros + pos;
  int pad = (width > total_len) ? (width - total_len) : 0;

  if (!(flags & FLAG_LEFT) && !(flags & FLAG_ZERO)) {
    written += print_padding(' ', pad);
  }

  if (sign) {
    console_putchar(sign);
    written++;
  }
  for (int i = 0; prefix[i]; i++) {
    console_putchar(prefix[i]);
    written++;
  }

  if (!(flags & FLAG_LEFT) && (flags & FLAG_ZERO)) {
    written += print_padding('0', pad);
  }

  written += print_padding('0', zeros);

  for (int i = pos - 1; i >= 0; i--) {
    console_putchar(buf[i]);
    written++;
  }

  if (flags & FLAG_LEFT) {
    written += print_padding(' ', pad);
  }

  return written;
}

int kvprintf(const char *fmt, va_list args) {
  int written = 0;

  while (*fmt) {
    if (*fmt != '%') {
      console_putchar(*fmt++);
      written++;
      continue;
    }

    fmt++;
    if (*fmt == '%') {
      console_putchar('%');
      written++;
      fmt++;
      continue;
    }

    int flags = 0;
    int parsing_flags = 1;
    while (parsing_flags) {
      switch (*fmt) {
        case '-': flags |= FLAG_LEFT; fmt++; break;
        case '+': flags |= FLAG_PLUS; fmt++; break;
        case ' ': flags |= FLAG_SPACE; fmt++; break;
        case '#': flags |= FLAG_HASH; fmt++; break;
        case '0': flags |= FLAG_ZERO; fmt++; break;
        default: parsing_flags = 0; break;
      }
    }

    int width = 0;
    if (*fmt == '*') {
      width = va_arg(args, int);
      if (width < 0) {
        flags |= FLAG_LEFT;
        width = -width;
      }
      fmt++;
    } else {
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt++ - '0');
      }
    }

    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      if (*fmt == '*') {
        prec = va_arg(args, int);
        fmt++;
      } else {
        prec = 0;
        while (*fmt >= '0' && *fmt <= '9') {
          prec = prec * 10 + (*fmt++ - '0');
        }
      }
    }

    int len = LEN_DEFAULT;
    if (*fmt == 'h') {
      fmt++;
      if (*fmt == 'h') { len = LEN_HH; fmt++; }
      else len = LEN_H;
    } else if (*fmt == 'l') {
      fmt++;
      if (*fmt == 'l') { len = LEN_LL; fmt++; }
      else len = LEN_L;
    } else if (*fmt == 'z') {
      len = LEN_Z;
      fmt++;
    }

    char spec = *fmt++;
    switch (spec) {
      case 'c': {
        char c = (char)va_arg(args, int);
        if (!(flags & FLAG_LEFT)) written += print_padding(' ', width - 1);
        console_putchar(c);
        written++;
        if (flags & FLAG_LEFT) written += print_padding(' ', width - 1);
        break;
      }

      case 's': {
        const char *s = va_arg(args, const char *);
        if (!s) s = "(null)";
        int s_len = 0;
        while (s[s_len]) s_len++;
        if (prec >= 0 && prec < s_len) s_len = prec;

        if (!(flags & FLAG_LEFT)) written += print_padding(' ', width - s_len);
        for (int i = 0; i < s_len; i++) {
          console_putchar(s[i]);
          written++;
        }
        if (flags & FLAG_LEFT) written += print_padding(' ', width - s_len);
        break;
      }

      case 'd':
      case 'i': {
        int64_t v = 0;
        if (len == LEN_HH) v = (signed char)va_arg(args, int);
        else if (len == LEN_H) v = (short)va_arg(args, int);
        else if (len == LEN_L) v = va_arg(args, long);
        else if (len == LEN_LL) v = va_arg(args, long long);
        else if (len == LEN_Z) v = va_arg(args, int64_t);
        else v = va_arg(args, int);

        int is_neg = (v < 0);
        uint64_t uv = is_neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
        written += format_number(uv, 1, is_neg, 10, flags, width, prec);
        break;
      }

      case 'u':
      case 'x':
      case 'X':
      case 'b':
      case 'o': {
        uint64_t uv = 0;
        if (len == LEN_HH) uv = (unsigned char)va_arg(args, unsigned int);
        else if (len == LEN_H) uv = (unsigned short)va_arg(args, unsigned int);
        else if (len == LEN_L) uv = va_arg(args, unsigned long);
        else if (len == LEN_LL) uv = va_arg(args, unsigned long long);
        else if (len == LEN_Z) uv = va_arg(args, uint64_t);
        else uv = va_arg(args, unsigned int);

        if (spec == 'X') flags |= FLAG_UPPER;
        int base = 10;
        if (spec == 'x' || spec == 'X') base = 16;
        else if (spec == 'b') base = 2;
        else if (spec == 'o') base = 8;

        written += format_number(uv, 0, 0, base, flags, width, prec);
        break;
      }

      case 'p': {
        uint64_t ptr = (uint64_t)va_arg(args, void *);
        flags |= FLAG_HASH;
        written += format_number(ptr, 0, 0, 16, flags, width, (prec == -1) ? 16 : prec);
        break;
      }

      default:
        console_putchar(spec);
        written++;
        break;
    }
  }

  return written;
}

int kprintf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int ret = kvprintf(fmt, args);
  va_end(args);
  return ret;
}
