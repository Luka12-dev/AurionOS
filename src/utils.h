#ifndef UTILS_H
#define UTILS_H
#include <stddef.h>
#include <stdint.h>
void puts(const char* str);
void putc(char c);
void cls();
void beep();
int getkey_block();
void set_attr(int attr);
#endif