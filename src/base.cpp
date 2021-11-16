#include "base.hpp"

namespace fn {

gc_header* mk_gc_header(u8 bits, gc_header* dest) {
    if (dest == nullptr) {
        dest = new gc_header;
    }
    return new(dest) gc_header {
        .bits=bits,
        .pin_count=0,
        .global_count=0
    };
}

}
