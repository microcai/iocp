
#pragma once

#include <cstdint>

namespace fcontext {

typedef void*   fcontext_t;

struct transfer_t {
    fcontext_t  fctx;
    void    *   data; // pass jump_info_t pointer
};

struct jump_info_t {
	void* (*func)(fcontext_t, void*);
	void* arguments;
};

extern "C" transfer_t jump_fcontext( fcontext_t const to, void * vp);
extern "C" fcontext_t make_fcontext( void * sp, std::size_t size, void (* fn)( transfer_t) );

}
