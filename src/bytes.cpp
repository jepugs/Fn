#include "bytes.hpp"
#include "allocator.hpp"

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
    auto start_size = code.capacity;
    code.push_back(data);
    alloc->mem_usage += (code.capacity - start_size);
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
    auto x = constant_table.get(v);
    if (x.has_value()) {
        return *x;
    }

    auto start_size = constant_arr.capacity;
    constant_arr.push_back(v);
    alloc->mem_usage += (constant_arr.capacity - start_size)*sizeof(value);

    auto id = constant_arr.size - 1;
    constant_table.insert(v, id);
    return id;
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
        .req_args=req_args,
        .vl_param=vl_param,
        .vt_param=vt_param,
        .foreign=nullptr,
        .chunk=this,
        .name=name,
        .addr=code.size,
        .num_upvals=0
    };
    for (u32 i = 0; i < num_pos; ++i) {
        s->pos_params.push_back(pos_params[i]);
    }

    auto start_size = function_arr.capacity;
    function_arr.push_back(s);
    alloc->mem_usage += (function_arr.capacity - start_size)*sizeof(value)
        + sizeof(function_stub)
        + s->pos_params.data_size()
        + s->name.size();

    return function_arr.size - 1;
}

u16 code_chunk::add_foreign_function(local_address num_pos,
        symbol_id* pos_params,
        local_address req_args,
        optional<symbol_id> vl_param,
        optional<symbol_id> vt_param,
        value (*foreign_func)(fn_handle*, value*),
        const string& name) {
    auto s = new function_stub {
        .req_args=req_args,
        .vl_param=vl_param,
        .vt_param=vt_param,
        .foreign=foreign_func,
        .chunk=this,
        .name=name,
        .num_upvals=0
    };
    for (u32 i = 0; i < num_pos; ++i) {
        s->pos_params.push_back(pos_params[i]);
    }

    auto start_size = function_arr.capacity;
    function_arr.push_back(s);
    alloc->mem_usage += (function_arr.capacity - start_size)*sizeof(value)
        + sizeof(function_stub)
        + s->pos_params.data_size()
        + s->name.size();

    return function_arr.size - 1;
}


function_stub* code_chunk::get_function(u16 id) {
    return function_arr[id];
}

const function_stub* code_chunk::get_function(u16 id) const {
    return function_arr[id];
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
code_chunk* mk_code_chunk(allocator* use_alloc, symbol_id ns_id) {
    auto res = new code_chunk{
        .alloc=use_alloc,
        .ns_id=ns_id,
        .source_info = new chunk_source_info{
            .start_addr=0,
            .loc={.line=0, .col=0},
            .prev=nullptr
        }
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
