
#pragma once

struct zcontext_t
{
    void* sp;// pointer to active stack buttom
};

void* zcontext_swap(zcontext_t* from, zcontext_t* to, void* argument) asm("zcontext_swap");

void zcontext_setup(zcontext_t* target, void (*func)(void*arg), void* argument) asm("zcontext_setup");
