#include "bytes.hpp"

namespace fn {

void code_chunk::ensure_code_capacity(code_address min_cap) {
    if (min_cap <= code_capacity) {
        return;
    }
    while (min_cap > code_capacity) {
        code_capacity *= 2;
    }
    code = (u8*)realloc(code, code_capacity*sizeof(u8));
}

void code_chunk::ensure_constant_capacity(constant_id min_cap) {
    if (min_cap <= constant_capacity) {
        return;
    }
    while (min_cap > constant_capacity) {
        constant_capacity *= 2;
    }
    constant_table =
        (value*)realloc(code, constant_capacity*sizeof(value));
}

void code_chunk::ensure_function_capacity(constant_id min_cap) {
    if (min_cap <= function_capacity) {
        return;
    }
    while (min_cap > function_capacity) {
        function_capacity *= 2;
    }
    function_table =
        (function_stub**)realloc(code,function_capacity*sizeof(function_stub*));
}

u8 code_chunk::read_byte(u32 where) const {
    return code[where];
}

u16 code_chunk::read_short(u32 where) const {
    u16 lo = code[where];
    u16 hi = code[where + 1];
    return (hi << 8) | lo;
}

void code_chunk::write_byte(u8 data) {
    ensure_code_capacity(code_size + 1);
    code[code_size++] = data;
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
    ensure_constant_capacity(num_constants + 1);
    constant_table[num_constants] = v;
    return num_constants++;
}

value code_chunk::get_constant(constant_id id) const {
    return constant_table[id];
}

u16 code_chunk::add_function(const vector<symbol_id>& pparams,
                             local_address req_args,
                             optional<symbol_id> vl_param,
                             optional<symbol_id> vt_param) {
    auto s = new function_stub {
        .pos_params=pparams,
        .req_args=req_args,
        .vl_param=vl_param,
        .vt_param=vt_param,
        .chunk=this,
        .addr=code_size,
        .num_upvals=0,
        .upvals=vector<local_address>{},
        .upvals_direct=vector<bool>{}};
    ensure_function_capacity(num_functions + 1);
    function_table[num_functions] = s;
    return num_functions++;
}

function_stub* code_chunk::get_function(u16 id) {
    return function_table[id];
}

const function_stub* code_chunk::get_function(u16 id) const {
    return function_table[id];
}

void code_chunk::add_source_loc(const source_loc& s) {
    source_info = new chunk_source_info{code_size, s, source_info};
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
    return new(dest) code_chunk {
        .ns_id = ns_id,
        .code = new u8[init_array_size],
        .code_size = 0,
        .code_capacity = init_array_size,
        .constant_table = new value[init_array_size],
        .num_constants = 0,
        .constant_capacity = init_array_size,
        .function_table = new function_stub*[init_array_size],
        .source_info = source_info
    };
}

void free_code_chunk(code_chunk* chunk) {
    delete chunk->code;
    delete chunk->constant_table;
    for (auto i = 0; i < chunk->num_functions; ++i) {
        delete chunk->function_table[i];
    }
    delete chunk->function_table;

    auto i = chunk->source_info;
    while (i != nullptr) {
        auto prev = i->prev;
        delete i;
        i = prev;
    }
}

}
