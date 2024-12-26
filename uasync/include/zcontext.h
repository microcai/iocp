
#pragma once

#include <cstdint>
#include <float.h>

struct zcontext_t
{
    void* sp;// pointer to active stack buttom
};

extern "C" void* zcontext_swap(zcontext_t* from, zcontext_t* to, void* argument);
extern "C" void* zcontext_entry_point();

inline void zcontext_setup(zcontext_t* target, void (*func)(void*arg), void* argument)
{
    struct startup_stack_structure
    {
#ifdef _WIN32
        void* fc_x87_cw;
        void* fc_mxcsr;
        void* mmx_reg[20];
        void* pad;
        void* general_reg[8];
#else
        void* fc_x87_cw;
        void* fc_mxcsr;
        void* general_reg[6];
#endif
        void* ret_address;
        void* param1;
        void* param2;

#ifdef _WIN32
        uint32_t* padding[4];
#endif
    };

    char* sp = reinterpret_cast<char*>(target->sp);

    sp -= sizeof(startup_stack_structure);

    target->sp = sp;

    auto startup_stack = reinterpret_cast<startup_stack_structure*>(sp);

    startup_stack->ret_address = (void*) zcontext_entry_point;
    startup_stack->param1 = (void*) func;
    startup_stack->param2 = argument;
    startup_stack->fc_mxcsr = startup_stack->fc_x87_cw = 0;
}
