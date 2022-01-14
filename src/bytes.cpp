#include "bytes.hpp"

#include "allocator.hpp"
#include "memory.hpp"

namespace fn {

u8 code_chunk::read_byte(u32 where) const {
    return code[where];
}

u16 code_chunk::read_short(u32 where) const {
    u16 lo = code[where];
    u16 hi = code[where + 1];
    return (hi << 8) | lo;
}

void code_chunk::write_byte(u8 data) {
    code.push_back(data);
}

void code_chunk::write_byte(u8 data, u32 where) {
    code[where] = data;
}

void code_chunk::write_short(u16 data) {
    u8 lo = data & 255;
    u8 hi = (data >> 8) & 255;
    write_byte(lo);
    write_byte(hi);
}

void code_chunk::write_short(u16 data, u32 where) {
    u8 lo = data & 255;
    u8 hi = (data >> 8) & 255;
    code[where] = lo;
    code[where + 1] = hi;
}

constant_id code_chunk::add_constant(value v) {
    auto id = constant_arr.size;
    constant_arr.push_back(v);
    return id;
}

value code_chunk::get_constant(constant_id id) const {
    return constant_arr[id];
}

void code_chunk::add_source_loc(const source_loc& s) {
    if (code.size == source_info->start_addr) {
        auto tmp = source_info->prev;
        delete source_info;
        source_info = tmp;
    }
    if (source_info == nullptr || source_info->loc != s) {
        source_info = new chunk_source_info{code.size, s, source_info};
    }
}

source_loc code_chunk::location_of(u32 addr) {
    auto x = source_info;
    for (; x->prev != nullptr; x = x->prev) {
        if (x->start_addr <= addr) {
            return x->loc;
        }
    }
    return x->loc;
}

// used to initialize the dynamic arrays
static constexpr size_t init_array_size = 32;
code_chunk* mk_code_chunk(symbol_id ns_id) {
    auto res = new code_chunk{
        .ns_id=ns_id,
        .source_info = new chunk_source_info{
            .start_addr=0,
            .loc={.line=0, .col=0},
            .prev=nullptr
        }
    };
    return res;
}

void free_code_chunk(code_chunk* chunk) {
    auto i = chunk->source_info;
    while (i != nullptr) {
        auto prev = i->prev;
        delete i;
        i = prev;
    }

    delete chunk;
}

local_address function_stub::add_upvalue(u8 addr, bool direct) {
    upvals.push_back(addr);
    upvals_direct.push_back(direct);
    return num_upvals++;
}

}
