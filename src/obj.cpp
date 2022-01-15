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

}
