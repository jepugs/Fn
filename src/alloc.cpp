#include "alloc.hpp"
#include "values.hpp"

namespace fn {

gc_bytes* alloc_gc_bytes(istate* S, u64 nbytes) {
    auto sz = round_to_align(sizeof(gc_bytes) + nbytes);
    gc_bytes* res;
    // FIXME: allocate in the correct generation
    res = (gc_bytes*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_GC_BYTES, sz);
    res->data = (u8*)(sizeof(gc_bytes) + (u64)res);
    return res;
}

gc_bytes* realloc_gc_bytes(istate* S, gc_bytes* src, u64 new_size) {
    auto sz = round_to_align(sizeof(gc_bytes) + new_size);
    auto res = alloc_gc_bytes(S, new_size);
    memcpy(res, src, src->h.size);
    // the memcpy overwrites the size
    res->h.size = sz;
    res->data = (u8*)(sizeof(gc_bytes) + (u64)res);
    return res;
}

fn_str* create_string(istate* S, u32 len) {
    auto sz = round_to_align(sizeof(fn_str) + len + 1);
    auto res = (fn_str*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_STR, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_str));
    res->data[len] = 0;
    return res;
}

void alloc_string(istate* S, u32 where, u32 len) {
    S->stack[where] = vbox_string(create_string(S, len));
}

// create a string without doing collection first
fn_str* create_string(istate* S, const string& str) {
    auto len = str.size();
    auto res = create_string(S, len);
    memcpy(res->data, str.c_str(), len);
    return res;
}

void alloc_string(istate* S, u32 where, const string& str) {
    auto len = str.size();
    auto sz = round_to_align(sizeof(fn_str) + len + 1);
    auto res = (fn_str*)alloc_nursery_object(S, sz);
    res->size = len;
    init_gc_header(&res->h, GC_TYPE_STR, sz);
    res->data = (u8*) (((u8*)res) + sizeof(fn_str));
    memcpy(res->data, str.c_str(), len);
    res->data[len] = 0;
    S->stack[where] = vbox_string(res);
}

void alloc_cons(istate* S, u32 stack_pos, u32 hd, u32 tl) {
    auto sz = round_to_align(sizeof(fn_cons));
    auto res = (fn_cons*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_CONS, sz);
    res->head = S->stack[hd];
    res->tail = S->stack[tl];
    S->stack[stack_pos] = vbox_cons(res);
}

void alloc_table(istate* S, u32 stack_pos, u32 init_cap) {
    init_cap = init_cap == 0 ? FN_TABLE_INIT_CAP : init_cap;
    // allocate the array first
    auto data_handle = get_handle(S->alloc,
            alloc_gc_bytes(S, 2*init_cap*sizeof(value)));
    auto sz =  round_to_align(sizeof(fn_table));
    auto res = (fn_table*)alloc_nursery_object(S, sz);
    for (u32 i = 0; i < 2*init_cap; i += 2) {
        ((value*)(data_handle->obj->data))[i] = V_UNIN;
    }
    init_gc_header(&res->h, GC_TYPE_TABLE, sz);
    res->size = 0;
    res->cap = init_cap;
    res->rehash = 3 * init_cap / 4;
    res->data = data_handle->obj;
    release_handle(data_handle);
    res->metatable = V_NIL;
    S->stack[stack_pos] = vbox_table(res);
}

static void reify_bc_const(istate* S, const scanner_string_table& sst,
        const bc_output_const& k) {
    switch(k.kind) {
    case bck_number:
        push_num(S, k.d.num);
        break;
    case bck_string:
        push_str(S, scanner_name(sst, k.d.str_id));
        break;
    case bck_symbol:
        push_sym(S, intern_id(S, scanner_name(sst, k.d.str_id)));
        break;
    case bck_quoted:
        push_quoted(S, sst, k.d.quoted);
        return;
    }
}

gc_handle<function_stub>* gen_function_stub(istate* S,
        const scanner_string_table& sst, const bc_compiler_output& compiled) {
    auto& alloc = S->alloc;
    // compute the size of the object
    // FIXME: there might be a better way to write this...
    // FIXME: also these only need to be aligned to 8 bytes, not 32
    auto code_sz = round_to_align(compiled.code.size);
    auto const_sz = sizeof(value) * compiled.const_table.size;
    auto sub_funs_sz = sizeof(function_stub*) * compiled.sub_funs.size;
    auto upvals_sz = sizeof(upvalue_cell*) * compiled.num_upvals;
    auto upvals_direct_sz = round_to_align(sizeof(bool) * compiled.num_upvals);
    auto code_info_sz = round_to_align(sizeof(code_info) * compiled.ci_arr.size);
    auto sz = round_to_align(sizeof(function_stub) + code_sz + const_sz
            + sub_funs_sz + upvals_sz + upvals_direct_sz + code_info_sz);

    // set up the object
    auto o = (function_stub*)alloc_nursery_object(S, sz);
    init_gc_header(&o->h, GC_TYPE_FUN_STUB, sz);
    o->foreign = nullptr;
    o->num_params = compiled.params.size;
    o->num_opt = compiled.num_opt;
    o->vari = compiled.has_vari;
    o->space = compiled.stack_required;
    o->ns_id = S->ns_id;
    // TODO: sort this out
    o->name = nullptr;
    o->filename = S->filename;

    // we have to take care when setting up the arrays, as the garbage collector
    // could be invoked while we're building them. Therefore we initialize the
    // traversable arrays (e.g. const array) with length 0, so the collector
    // won't try to traverse an undefined pointer.

    // FIXME: There... there has to be a better way... (to set up these arrays)
    // FIXME: Maybe I can use a template that depends on the array types
    o->code_length = compiled.code.size;
    o->code = (u8*)raw_ptr_add(o, sizeof(function_stub));
    o->num_const = compiled.const_table.size;
    o->const_arr = (value*)raw_ptr_add(o, sizeof(function_stub) + code_sz);
    for (u32 i = 0; i < compiled.const_table.size; ++i) {
        o->const_arr[i] = V_NIL;
    }
    o->num_sub_funs = compiled.sub_funs.size;
    o->sub_funs = (function_stub**)raw_ptr_add(o, sizeof(function_stub)
            + code_sz + const_sz);
    for (u32 i = 0; i < compiled.sub_funs.size; ++i) {
        o->sub_funs[i] = nullptr;
    }
    o->num_upvals = compiled.num_upvals;
    o->upvals = (u8*)raw_ptr_add(o, sizeof(function_stub) + code_sz + const_sz
            + sub_funs_sz);
    o->upvals_direct = (bool*)raw_ptr_add(o, sizeof(function_stub) + code_sz
            + const_sz + sub_funs_sz + upvals_sz);
    o->ci_length = compiled.ci_arr.size;
    o->ci_arr = (code_info*)raw_ptr_add(o, sizeof(function_stub) + code_sz
            + const_sz + sub_funs_sz + upvals_sz + upvals_direct_sz);
    memcpy(o->upvals, compiled.upvals.data,
            compiled.upvals_direct.size*sizeof(u8));
    memcpy(o->upvals_direct, compiled.upvals_direct.data,
            compiled.upvals_direct.size*sizeof(bool));
    memcpy(o->ci_arr, compiled.ci_arr.data,
            compiled.ci_arr.size*sizeof(code_info));

    // fill out the arrays. Now we need to make a handle since we might trigger
    // garbage collection.
    auto h = get_handle(alloc, o);
    memcpy(h->obj->code, compiled.code.data, compiled.code.size * sizeof(u8));
    for (u32 i = 0; i < compiled.const_table.size; ++i) {
        reify_bc_const(S, sst, compiled.const_table[i]);
        auto v = peek(S);
        if (vhas_header(v)) {
            write_guard(get_gc_card_header((gc_header*)h->obj), vheader(v));
        }
        h->obj->const_arr[i] = v;
        pop(S);
    }
    for (u32 i = 0; i < compiled.sub_funs.size; ++i) {
        auto h2 = gen_function_stub(S, sst, compiled.sub_funs[i]);
        h->obj->sub_funs[i] = h2->obj;
        write_guard(get_gc_card_header((gc_header*)h->obj), &h2->obj->h);
        release_handle(h2);
    }

    // set the function name
    push_str(S, scanner_name(sst, compiled.name_id));
    h->obj->name = vstr(peek(S));
    pop(S);

    return h;
}

bool reify_function(istate* S, const scanner_string_table& sst,
        const bc_compiler_output& bco) {
    auto stub_handle = gen_function_stub(S, sst, bco);
    // TODO: should error if num_upvals or parameter information is nontrivial.
    // Toplevel compiled functions should have neither arguments nor upvalues.

    auto sz = sizeof(fn_function);
    auto res = (fn_function*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_FUN, sz);
    res->stub = stub_handle->obj;
    push(S, vbox_function(res));
    release_handle(stub_handle);
    return true;
}

static upvalue_cell* alloc_open_upval(istate* S, u32 pos) {
    auto sz = round_to_align(sizeof(upvalue_cell));
    auto res = (upvalue_cell*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_UPVALUE, sz);
    res->closed = false;
    res->datum.pos = pos;
    return res;
}

void alloc_foreign_fun(istate* S,
        u32 where,
        void (*foreign)(istate*),
        u32 num_params,
        bool vari,
        const string& name) {
    push_str(S, name);
    auto stub_sz = round_to_align(sizeof(function_stub) + sizeof(code_info));
    auto stub = (function_stub*)alloc_nursery_object(S, stub_sz);
    init_gc_header(&stub->h, GC_TYPE_FUN_STUB, stub_sz);
    stub->num_upvals = 0;
    stub->name = vstr(peek(S));
    pop(S);
    stub->filename = S->filename;
    stub->foreign = foreign;
    stub->num_params = num_params;
    stub->vari = vari;

    stub->code_length = 0;
    stub->num_const = 0;
    stub->num_opt = 0;
    stub->num_sub_funs = 0;
    stub->ci_length = 1;
    stub->ci_arr = (code_info*)raw_ptr_add(stub,
            sizeof(function_stub));
    stub->ci_arr[0] = code_info{
        0,
        source_loc{0, 0, false, 0}
    };
    auto stub_handle = get_handle(S->alloc, stub);

    auto sz = round_to_align(sizeof(fn_function));
    auto res = (fn_function*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_FUN, sz);
    res->stub = stub_handle->obj;
    res->init_vals = nullptr;
    res->upvals = nullptr;
    release_handle(stub_handle);

    // FIXME: allocate foreign fun upvals
    S->stack[where] = vbox_function(res);
}

static upvalue_cell* open_upval(istate* S, u32 pos) {
    for (u32 i = 0; i < S->open_upvals.size; ++i) {
        if (S->open_upvals[i]->datum.pos == pos) {
            return S->open_upvals[i];
        } else if (S->open_upvals[i]->datum.pos > pos) {
            // insert a new upvalue cell
            S->open_upvals.push_back(S->open_upvals[S->open_upvals.size - 1]);
            for (u32 j = S->open_upvals.size - 2; j > i; --j) {
                S->open_upvals[j] = S->open_upvals[j - 1];
            }
            S->open_upvals[i] = alloc_open_upval(S, pos);
            return S->open_upvals[i];
        }
    }
    S->open_upvals.push_back(alloc_open_upval(S, pos));
    return S->open_upvals[S->open_upvals.size - 1];
}

void alloc_fun(istate* S, u32 enclosing, constant_id fid) {
    // FIXME: either put the function in a handle here, or change allocation
    // order to be open upvals first, then function

    // open upvalues first so that no collections will happen while we add them
    // later
    auto stub = vfunction(S->stack[enclosing])->stub->sub_funs[fid];
    auto m = stub->num_upvals;
    for (u32 i = 0; i < m; ++i) {
        if (stub->upvals_direct[i]) {
            open_upval(S, S->bp + stub->upvals[i]);
            // refresh in case of garbage collection
            stub = vfunction(S->stack[enclosing])->stub->sub_funs[fid];
        }
    }

    // size of upvals + initvals arrays
    auto sz = round_to_align(sizeof(fn_function)
            + stub->num_opt*sizeof(value)
            + stub->num_upvals*sizeof(upvalue_cell*));
    auto res = (fn_function*)alloc_nursery_object(S, sz);
    init_gc_header(&res->h, GC_TYPE_FUN, sz);

    auto enc_fun = vfunction(S->stack[enclosing]);
    // in case the gc moved the stub
    stub = enc_fun->stub->sub_funs[fid];

    res->init_vals = (value*)((u8*)res + sizeof(fn_function));
    for (u32 i = 0; i < stub->num_opt; ++i) {
        res->init_vals[i] = V_NIL;
    }
    res->upvals = (upvalue_cell**)(stub->num_opt*sizeof(value) + (u8*)res->init_vals);
    res->stub = stub;

    // set up upvalues
    for (u32 i = 0; i < res->stub->num_upvals; ++i) {
        if (res->stub->upvals_direct[i]) {
            res->upvals[i] = open_upval(S, S->bp + stub->upvals[i]);
        } else {
            res->upvals[i] = enc_fun->upvals[stub->upvals[i]];
        }
    }

    push(S, vbox_function(res));
}

static void setup_symcache(istate* S) {
    for (u32 i = 0; i < SYMCACHE_SIZE; ++i) {
        S->symcache->syms[i] = intern_id(S, sc_names[i]);
    }
}

istate* alloc_istate(const string& filename, const string& wd) {
    auto res = new istate;
    res->alloc = new allocator;
    init_allocator(*res->alloc, res);
    // TODO: allocate this through the allocator instead
    res->symtab = new symbol_table;
    res->symcache = new symbol_cache;
    setup_symcache(res);
    res->G = new global_env;
    res->G->list_meta = V_NIL;
    res->G->string_meta = V_NIL;
    res->ns_id = intern_id(res, "fn/user");
    res->pc = 0;
    res->bp = 0;
    res->sp = 0;
    res->callee = nullptr;
    res->filename = nullptr;
    res->wd = nullptr;
    res->filename = create_string(res, filename);
    res->wd = create_string(res, wd);
    res->err.happened = false;
    res->err.message = nullptr;
    // set up namespace
    add_ns(res, res->ns_id);
    return res;
}

}
