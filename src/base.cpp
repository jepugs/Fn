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

bool source_loc::operator==(const source_loc& other) {
    return this->line == other.line
        && this->col == other.col
        && this->filename == other.filename;
}

bool source_loc::operator!=(const source_loc& other) {
    return !(*this == other);
}

template<>
u32 hash<u8>(const u8& v) {
    return v;
}

}
