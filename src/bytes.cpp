#include "bytes.hpp"

namespace fn_bytes {

using namespace fn;

u16 code_chunk::add_function(const vector<symbol_id>& pparams,
                             local_addr req_args,
                             bool var_list,
                             bool var_table) {
    functions.push_back(new func_stub {
            .positional=pparams,
            .optional_index=req_args,
            .var_list=var_list,
            .var_table=var_table,
            .num_upvals=0,
            .upvals=vector<upvalue>(),
            .addr=size()});
    return (u16) (functions.size() - 1);
}


code_chunk::code_chunk(symbol_table* st)
    : st{st} {
    add_source_loc(source_loc{std::shared_ptr<string>(new string{""}),0,0});
}

u32 code_chunk::size() {
    return static_cast<u32>(code.size());
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

const_id code_chunk::add_const(value k) {
    consts.push_back(k);
    return consts.size() - 1;
}

}
