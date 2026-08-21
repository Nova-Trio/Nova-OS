#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>

#define VGA ((volatile uint16_t*)0xB8000)
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_FGWHITE_BGBLACK 0x0F

uint32_t vga_cursorX = 0, vga_cursorY = 0;

// Scrolls down by one line (previous first line not reserved)
void vga_scroll()
{
    // Move lines up by one row
    for (uint16_t y = 1; y < VGA_HEIGHT; y++)
        for (uint16_t x = 0; x < VGA_WIDTH; x++)
            VGA[(y -1) *VGA_WIDTH +x] = VGA[y *VGA_WIDTH +x];
    
    // Clear the last line
    for (int x = 0; x < VGA_WIDTH; x++)
        VGA[(VGA_HEIGHT -1) *VGA_WIDTH +x] = ' ' | (VGA_FGWHITE_BGBLACK << 8);
    
    vga_cursorY = VGA_HEIGHT - 1;
}

// Prints a single character
void vga_printC(const char c)
{
    if (c == '\n')
    {
        vga_cursorX = 0;
        vga_cursorY++;
    }
    else if (c == '\t')
    {
        for (uint8_t i = 0; i < 4; i++)
            vga_printC(' ');
        return;
    }
    else if (c < ' ') return;
    else
    {
        uint16_t index = (vga_cursorY * VGA_WIDTH) + vga_cursorX;
        VGA[index] = c | (VGA_FGWHITE_BGBLACK << 8);
        vga_cursorX++;
    }

    // Handle horizontal edge wrapping
    if (vga_cursorX >= VGA_WIDTH)
    {
        vga_cursorX = 0;
        vga_cursorY++;
    }
    // Handle vertical overflow (scroll)
    if (vga_cursorY >= VGA_HEIGHT)
    {
        vga_scroll();
    }
}

// Prints hexadecimal, binary, and decimal
void vga_printN(uint64_t value, const uint8_t base, const bool isSigned)
{
    char buffer[65]; // Enough for 64-bit binary representation + null
    uint8_t i = 0;

    if (isSigned && (int64_t)value < 0)
    {
        vga_printC('-');
        value = -(int64_t)value;
    }
    if (value == 0)
    {
        vga_printC('0');
        return;
    }

    // Fill string buffer in reverse
    while (value > 0)
    {
        uint64_t remainder = value % base;
        if (remainder < 10)
        {
            buffer[i] = '0' + remainder;
            i++;
        }
        else
        {
            buffer[i] = 'A' + (remainder -10);
            i++;
        }
        value /= base;
    }

    // Print buffer
    for (int j = i -1; j >= 0; j--)
        vga_printC(buffer[j]);
}

// Supports #c (character) #s (string) #sl (string with length)
// #ui (unsigned decimal) #si (signed decimal) #b (binary) #x (hexadecimal)
void vga_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);

    while (*format != '\0')
    {
        // Normal character output
        if (*format != '#')
        {
            // Print the character
            vga_printC(*format);
            // Pass the character
            format++;
        }
        // # special indicator
        else
        {
            // Pass the #
            format++;
            // Single # at end
            if (*format == '\0')
            {
                // Print #
                vga_printC('#');
            }
            // ## (becomes #)
            else if (*format == '#')
            {
                // Pass the second '#'
                format++;
                // Print #
                vga_printC('#');
            }
            // S for string or signed
            else if (*format == 's')
            {
                // Pass the 's'
                format++;
                // Signed decimal #si
                if (*format == 'i')
                {
                    // Pass the 'i'
                    format++;
                    // Get the integer
                    int64_t val = va_arg(args, int64_t);
                    uint64_t uVal = *( (uint64_t*)(&val) );
                    // Print the integer
                    vga_printN(uVal, 10, true);
                }
                // String with length #sl
                else if (*format == 'l')
                {
                    // Pass the 'l'
                    format++;
                    // Get the string
                    const char* str = va_arg(args, const char*);
                    uint64_t len = va_arg(args, uint64_t);
                    // Print the string
                    for (uint64_t i = 0; i < len && str[i] != '\0'; i++)
                        vga_printC(str[i]);
                }
                // Standard null-terminated string #s
                else
                {
                    // Get the string
                    const char* str = va_arg(args, const char*);
                    // Print the string
                    for (; *str != '\0'; str++)
                        vga_printC(*str);
                }
            }
            // Unsigned decimal #ui
            else if (*format == 'u' && *(format +1) == 'i') 
            {
                // Pass the 'u' and 'i'
                format+= 2;
                // Get the integer
                uint64_t val = va_arg(args, uint64_t);
                // Print the integer
                vga_printN(val, 10, false);
            }
            // Hexadecimal #x
            else if (*format == 'x')
            {
                // Pass the 'x'
                format++;
                // Get the integer
                uint64_t val = va_arg(args, uint64_t);
                // Print the integer
                vga_printN(val, 16, false);
            }
            // Binary #b
            else if (*format == 'b') 
            {
                // Pass the 'b'
                format++;
                // Get the integer
                uint64_t val = va_arg(args, uint64_t);
                // Print the integer
                vga_printN(val, 2, false);
            }
            // Single Character #c
            else if (*format == 'c') 
            {
                // Pass the 'c'
                format++;
                // Get the character (va_arg makes it an int)
                char c = (char)va_arg(args, int);
                // Print the character
                vga_printC(c);
            }
            // Loose '#' without a programmed character
            else
            {
                // Print '#' and the random character
                vga_printC('#');
                vga_printC(*format);
                // Pass the random character
                format++;
            }
        }
    }
    va_end(args);
}
