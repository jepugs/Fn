#include "bytes.hpp"

namespace fn {

chunk_source_info::chunk_source_info(u32 end_addr, const source_loc& loc)
    : end_addr{end_addr}
    , loc{loc} {
}

code_chunk::code_chunk(symbol_id use_ns)
    : ns_id{use_ns} {
    add_source_loc(source_loc{std::shared_ptr<string>(new string{""}),0,0});
}

code_chunk::~code_chunk() {
    for (auto p : const_conses) {
        delete p;
    }
    for (auto p : const_strings) {
        delete p;
    }

    // deallocate function stubs
    for (auto stub : functions) {
        delete stub;
    }
}

u32 code_chunk::size() const {
    return static_cast<u32>(code.size());
}

symbol_id code_chunk::get_ns_id() {
    return ns_id;
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
    code.push_back(data);
}

void code_chunk::write_byte(u8 data, u32 where) {
    code[where] = data;
}

void code_chunk::write_short(u16 data) {
    u8 lo = data & 255;
    u8 hi = (data >> 8) & 255;
    code.push_back(lo);
    code.push_back(hi);
}

void code_chunk::write_short(u16 data, u32 where) {
    u8 lo = data & 255;
    u8 hi = (data >> 8) & 255;
    code[where] = lo;
    code[where + 1] = hi;
}

const_id code_chunk::const_num(f64 num) {
    const_table.push_back(as_value(num));
    return const_table.size() - 1;
}

const_id code_chunk::const_sym(symbol_id id) {
    const_table.push_back(as_sym_value(id));
    return const_table.size() - 1;
}

const_id code_chunk::const_string(const fn_string& s) {
    auto p = new fn_string{s};
    const_strings.push_back(p);
    const_table.push_back(as_value(p));
    return const_table.size() - 1;
}

value code_chunk::quote_helper(const fn_parse::ast_node* node) {
    if (node->kind == fn_parse::ak_atom) {
        auto& atom = *node->datum.atom;
        fn_string* p;
        switch (atom.type) {
        case fn_parse::at_number:
            return as_value(atom.datum.num);
        case fn_parse::at_symbol:
            return as_sym_value(atom.datum.sym);
        case fn_parse::at_string:
            p = new fn_string{*atom.datum.str};
            const_strings.push_back(p);
            return as_value(p);
        default:
            // FIXME: maybe should raise an exception?
            return V_NULL;
        }
    } else if (node->kind == fn_parse::ak_list) {
        auto tl = V_EMPTY;
        for (i32 i = node->datum.list->size() - 1; i >= 0; --i) {
            auto hd = quote_helper(node->datum.list->at(i));
            auto p = new cons{hd, tl};
            const_conses.push_back(p);
            tl = as_value(p);
        }
        return tl;
    } else {
        // FIXME: probably should be an error
        return V_NULL;
    }
}

const_id code_chunk::const_quote(const fn_parse::ast_node* node) {
    value res = quote_helper(node);
    const_table.push_back(res);
    return const_table.size() - 1;
}

value code_chunk::get_const(const_id id) const {
    return const_table[id];
}

u32 code_chunk::num_consts() const {
    return const_table.size();
}

u16 code_chunk::add_function(const vector<symbol_id>& pparams,
                             local_address req_args,
                             optional<symbol_id> vl_param,
                             optional<symbol_id> vt_param) {
    functions.push_back(new function_stub {
            .pos_params=pparams,
            .req_args=req_args,
            .vl_param=vl_param,
            .vt_param=vt_param,
            .foreign_func = nullptr,
            .chunk=this,
            .addr=size(),
            .num_upvals=0,
            .upvals=vector<i32>()});
    return (u16) (functions.size() - 1);
}

function_stub* code_chunk::get_function(u16 id) {
    return functions[id];
}

const function_stub* code_chunk::get_function(u16 id) const {
    return functions[id];
}

void code_chunk::add_source_loc(const source_loc& s) {
    source_info.push_back(chunk_source_info{size(), s});
}

source_loc code_chunk::location_of(u32 addr) {
    for (auto x : source_info) {
        if (x.end_addr > addr) {
            return x.loc;
        }
    }
    return source_info.front().loc;
}

}
