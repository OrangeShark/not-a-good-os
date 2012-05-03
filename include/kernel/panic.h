#ifndef _PANIC_H
#define _PANIC_H

#include <kernel/stdio.h>
#include <kernel/types.h>

#define PANIC(message) panic(message, __FILE__, __LINE__)
#define BUG(message) bug(message, __FILE__, __LINE__)

void panic(const char *message, const char *file_name, uint32_t line_num);
void bug(const char *message, const char *file_name, uint32_t line_num);

#endif /* _PANIC_H */