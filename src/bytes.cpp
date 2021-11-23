#include "bytes.hpp"

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
    constant_arr.push_back(v);
    return constant_arr.size - 1;
}

value code_chunk::get_constant(constant_id id) const {
    return constant_arr[id];
}

u16 code_chunk::add_function(local_address num_pos,
        symbol_id* pos_params,
        local_address req_args,
        optional<symbol_id> vl_param,
        optional<symbol_id> vt_param,
        const string& name) {
    auto s = new function_stub {
        .pos_params=vector<symbol_id>{},
        .req_args=req_args,
        .vl_param=vl_param,
        .vt_param=vt_param,
        .foreign=nullptr,
        .name=name,
        .chunk=this,
        .addr=code.size,
        .num_upvals=0,
        .upvals=vector<local_address>{},
        .upvals_direct=vector<bool>{}};
    for (u32 i = 0; i < num_pos; ++i) {
        s->pos_params.push_back(pos_params[i]);
    }
    function_arr.push_back(s);
    return function_arr.size - 1;
}


function_stub* code_chunk::get_function(u16 id) {
    return function_arr[id];
}

const function_stub* code_chunk::get_function(u16 id) const {
    return function_arr[id];
}

void code_chunk::add_source_loc(const source_loc& s) {
    source_info = new chunk_source_info{code.size, s, source_info};
}

source_loc code_chunk::location_of(u32 addr) {
    for (auto x = source_info; x != nullptr; x = x->prev) {
        if (x->end_addr > addr) {
            return x->loc;
        }
    }
    return source_info->loc;
}

// used to initialize the dynamic arrays
static constexpr size_t init_array_size = 32;
code_chunk* mk_code_chunk(symbol_id ns_id, code_chunk* dest) {
    if (dest == nullptr) {
        dest = new code_chunk;
    }
    auto source_info = new chunk_source_info{
        .end_addr=0,
        .loc={.filename="", .line=0, .col=0},
        .prev=nullptr
    };
    // gotta use malloc here for portability since we use realloc to resize
    // arrays.
    auto res = new(dest) code_chunk {
        .ns_id = ns_id,
        .source_info = source_info
    };
    mk_gc_header(GC_TYPE_CHUNK, &res->h);
    return res;
}

void free_code_chunk(code_chunk* chunk) {
    for (auto x : chunk->function_arr) {
        delete x;
    }

    auto i = chunk->source_info;
    while (i != nullptr) {
        auto prev = i->prev;
        delete i;
        i = prev;
    }

    delete chunk;
}

}
