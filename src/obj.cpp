#include "base.hpp"
#include "obj.hpp"

namespace fn {

void init_gc_header(gc_header* dest, u8 bits) {
    if (dest == nullptr) {
        dest = new gc_header;
    }
    new(dest) gc_header {
        .mark = 0,
        .bits=bits,
        .pin_count=0
    };
}

void update_code_info(function_stub* to, const source_loc& loc) {
    auto c = new code_info {
        .start_addr = to->code.size,
        .loc = loc,
        .prev = to->ci_head
    };
    to->ci_head = c;
}

code_info* instr_loc(function_stub* stub, u32 pc) {
    auto c = stub->ci_head;
    while (c != nullptr) {
        if (c->start_addr <= pc) {
            break;
        }
        c = c->prev;
    }
    return c;
}

}
