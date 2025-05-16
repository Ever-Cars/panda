#pragma once

#define BUF_MAX 256

static char buf[BUF_MAX];
static int head = 0;
static int tail = 0;
static int size = 0;

void putc(const char c)
{
    buf[head++] = c;
    if (head == BUF_MAX)
        head = 0;
}

void puts(const char *s)
{
    for (const char *i = s; *i; i++) {
        if (*i == '\n')
            putc('\r');
        putc(*i);
        if (size < BUF_MAX)
            size++;
    }
}

bool getc(char *c)
{
    if (size == 0)
        return false;
    *c = buf[tail++];
    if (tail == BUF_MAX)
        tail = 0;
    size--;

    return true; 
}