#include "api.hpp"

#include "alloc.hpp"
#include "gc.hpp"
#include "istate.hpp"

namespace fn { 

static void type_error(istate* S, const string& expected_type) {
    ierror(S, "Got a different type while expecting a "
            + expected_type + ".");
}

// get a local stack value, i.e. one indexed from the base pointer
static inline value& lget(istate* S, u8 i) {
    return S->stack[S->bp + i];
}

static inline const value& lget(const istate* S, u8 i) {
    return S->stack[S->bp + i];
}

u8 get_frame_pointer(istate* S) {
    return S->sp - S->bp;
}

u8 stack_space(istate* S) {
    return STACK_SIZE - S->sp;
}

void push_copy(istate* S, u8 i) {
    push(S, lget(S, i));
}

void pop(istate* S, u8 times) {
    S->sp -= times;
}

void pop_to_local(istate* S, u8 dest) {
    lget(S, dest) = S->stack[S->sp - 1];
    pop(S);
}

void push_num(istate* S, f64 num) {
    push(S, vbox_number(num));
}
void push_str(istate* S, u32 size) {
    push_nil(S);
    alloc_string(S, S->sp - 1, size);
}
void push_str(istate* S, const string& str)  {
    push_nil(S);
    alloc_string(S, S->sp - 1, str);
}
void push_sym(istate* S, symbol_id sym) {
    push(S, vbox_symbol(sym));
}
void push_intern(istate* S, const string& str) {
    push(S, vbox_symbol(intern_id(S, str)));
}
void push_symname(istate* S, symbol_id sym) {
    push_str(S, symname(S, sym));
}
void push_nil(istate* S) {
    push(S, V_NIL);
}
void push_yes(istate* S) {
    push(S, V_YES);
}
void push_no(istate* S) {
    push(S, V_NO);
}

void pop_to_list(istate* S, u32 n) {
    push(S, V_EMPTY);
    for (u32 i = 0; i < n; ++i) {
        alloc_cons(S, S->sp - 2 - i, S->sp - 2 - i, S->sp - 1 - i);
    }
    S->sp -= n;
}

void push_cons(istate* S, u8 head_index, u8 tail_index) {
    push_nil(S);
    alloc_cons(S, S->sp - 1, head_index, tail_index);
}

bool ppush_cons(istate* S, u8 head_index, u8 tail_index) {
    if (!is_number(S, tail_index)) {
        ierror(S, "Cons tail must be a list.");
        return false;
    }
    push_cons(S, head_index, tail_index);
    return true;
}

void push_empty_table(istate* S, u32 init_cap) {
    push_nil(S);
    alloc_table(S, S->sp - 1, init_cap);
}

void push_table(istate* S, u8 num_args) {
    u32 base = S->sp - num_args;
    push_nil(S);
    // using num_args as initial capacity will give us 2x the table size we
    // need, allowing a couple more keys to be inserted before rehashing
    alloc_table(S, S->sp - 1, num_args);
    for (u32 i = 0; i < num_args; i += 2) {
        table_insert(S, S->sp-1, base + i, base + i + 1);
    }
    S->stack[base] = S->stack[S->sp - 1];
    S->sp = base + 1;
}

void push_foreign_function(istate* S, void (*foreign) (istate*), u8 num_args,
        bool vari, const string& name) {
    push_nil(S);
    alloc_foreign_fun(S, S->sp - 1, foreign, num_args, vari, name);
}

void get_number(f64& out, const istate* S, u8 i) {
    out = vnumber(lget(S, i));
}
bool pget_number(f64& out, istate* S, u8 i) {
    if (!vis_number(lget(S, i))) {
        type_error(S, "number");
        return false;
    }
    out = vnumber(lget(S, i));
    return true;
}
void get_string(string& out, const istate* S, u8 i) {
    out = convert_fn_str(vstr(lget(S, i)));
}
bool pget_string(string& out, istate* S, u8 i) {
    if (!vis_string(S->stack[S->bp + i])) {
        type_error(S, "string");
        return false;
    }
    out = convert_fn_str(vstr(lget(S, i)));
    return true;
}
void get_symbol_id(symbol_id& out, const istate* S, u8 i) {
    out = vsymbol(lget(S, i));
}
bool pget_symbol_id(symbol_id& out, istate* S, u8 i) {
    if (!vis_symbol(lget(S, i))) {
        type_error(S, "symbol");
        return false;
    }
    out = vsymbol(S->stack[S->bp + i]);
    return true;
}
void get_bool(bool& out, const istate* S, u8 i) {
    out = vtruth(lget(S, i));
}

bool is_number(istate* S, u8 i) {
    return vis_number(lget(S, i));
}
bool is_string(istate* S, u8 i) {
    return vis_string(lget(S, i));
}
bool is_symbol(istate* S, u8 i) {
    return vis_symbol(lget(S, i));
}
bool is_bool(istate* S, u8 i) {
    return vis_bool(lget(S, i));
}
bool is_nil(istate* S, u8 i) {
    return vis_nil(lget(S, i));
}
bool is_cons(istate* S, u8 i) {
    return vis_cons(lget(S, i));
}
bool is_list(istate* S, u8 i) {
    auto& val = lget(S, i);
    return vis_cons(val) || vis_emptyl(val);
}
bool is_empty_list(istate* S, u8 i) {
    return vis_emptyl(lget(S, i));
}
bool is_table(istate* S, u8 i) {
    return vis_table(lget(S, i));
}
bool is_function(istate* S, u8 i) {
    return vis_function(lget(S, i));
}

bool values_are_equal(istate* S, u8 index1, u8 index2) {
    return lget(S, index1) == lget(S, index2);
}

bool values_are_same(istate* S, u8 index1, u8 index2) {
    return vsame(lget(S, index1), lget(S, index2));
}

void push_metatable(istate* S, u8 i) {
    push(S, get_metatable(S, lget(S, i)));
}

void pop_set_table_metatable(istate* S, u8 i) {
    auto t = vtable(lget(S, i));
    t->metatable = peek(S);
    pop(S);
}

bool ppop_set_table_metatable(istate* S, u8 i) {
    if (!vis_table(lget(S, i))) {
        type_error(S, "table");
        return false;
    }
    pop_set_table_metatable(S, i);
    return true;
}

// FIXME: this code duplicates code in get_method() in vm.hpp. Maybe should find
// a way to merge these functions.
bool push_method(istate* S, u8 obj_index, symbol_id name) {
    auto m = get_metatable(S, lget(S, obj_index));
    if (!vis_table(m)) {
        return false;
    }
    auto x = table_get(vtable(m), vbox_symbol(name));
    if (!x) {
        return false;
    }
    push(S, x[1]);
    return true;
}

void push_head(istate* S, u8 i) {
    push(S, vhead(lget(S, i)));
}

bool ppush_head(istate* S, u8 i) {
    if (!vis_cons(lget(S,i))) {
        type_error(S, "cons");
        return false;
    }
    push_head(S, i);
    return true;
}

void push_tail(istate* S, u8 i) {
    if (vis_emptyl(lget(S, i))) {
        push(S, V_EMPTY);
    } else {
        push(S, vtail(lget(S, i)));
    }
}

bool ppush_tail(istate* S, u8 i) {
    auto& v = lget(S, i);
    if (vis_emptyl(v)) {
        push(S, V_EMPTY);
        return true;
    } else if (vis_cons(v)) {
        push(S, vtail(v));
        return true;
    } else {
        type_error(S, "list");
        return false;
    }
}

void get_string_length(u32& out, const istate* S, u8 i) {
    out = vstr(lget(S,i))->size;
}

bool pget_string_length(u32& out, istate* S, u8 i) {
    if (!vis_string(lget(S,i))) {
        type_error(S, "string");
        return false;
    }
    get_string_length(out, S, i);
    return true;
}

void concat_strings(istate* S, u8 n) {
    u32 base = S->sp - S->bp - n;
    u32 len = 0;
    for (u32 i = 0; i < n; ++i) {
        u32 x;
        get_string_length(x, S, base + i);
        len += x;
    }
    alloc_string(S, S->sp - 1, len);
    u32 ptr = 0;
    for (u32 i = 0; i < n; ++i) {
        u32 x;
        get_string_length(x, S, base + i);
        memcpy(&vstr(peek(S))->data[ptr],
                vstr(lget(S, base + i))->data,
                x);
        ptr += x;
    }
    S->stack[base] = peek(S);
    S->sp = base + 1;
}

bool pconcat_strings(istate* S, u8 n) {
    u32 base = S->sp - S->bp - n;
    for (u32 i = 0; i < n; ++i) {
        if (!vis_string(lget(S, base + i))) {
            type_error(S, "string");
            return false;
        }
    }
    concat_strings(S, n);
    return true;
}

void push_substring(istate* S, u8 i, u32 start, u32 stop) {
    auto& total_size = vstr(lget(S, i))->size;
    stop = stop > total_size ? total_size : stop;
    push_nil(S);
    alloc_string(S, S->sp - 1, stop - start);
    memcpy(vstr(peek(S))->data, vstr(lget(S, i))->data, stop - start);
}

bool ppush_substring(istate* S, u8 i, u32 start, u32 stop) {
    if (!vis_string(lget(S, i))) {
        type_error(S, "string");
        return false;
    }
    push_substring(S, i, start, stop);
    return true;
}

void push_table_entry(istate* S, u8 table_index, u8 key_index) {
    auto x = table_get(vtable(lget(S, table_index)),
            S->stack[S->bp + key_index]);
    if (!x) {
        push_nil(S);
    } else {
        push(S, x[1]);
    }
}

bool ppush_table_entry(istate* S, u8 table_index, u8 key_index) {
    if (!vis_table(lget(S, table_index))) {
        type_error(S, "table");
        return false;
    }
    push_table_entry(S, table_index, key_index);
    return true;
}

void pop_insert(istate* S, u8 table_index, u8 key_index) {
    table_insert(S, S->bp + table_index, S->bp + key_index, S->sp - 1);
    pop(S);
}
bool ppop_insert(istate* S, u8 table_index, u8 key_index) {
    if (!vis_table(lget(S, table_index))) {
        type_error(S, "table");
        return false;
    }
    pop_insert(S, table_index, key_index);
    return true;
}

symbol_id intern_id(istate* S, const string& str) {
    return S->symtab->intern(str);
}

symbol_id gensym_id(istate* S) {
    return S->symtab->gensym();
}

string symname(istate* S, symbol_id sym) {
    return S->symtab->symbol_name(sym);
}

// call() is defined in vm.cpp

// TODO: implement
// bool pcall(istate* S, u8 num_args) {
// }

void set_error(istate* S, const string& message) {
    ierror(S, message);
}

void clear_error(istate* S) {
    S->bp = 0;
    S->sp = 0;
    clear_error_info(S->err);
}

void set_namespace_id(istate* S, symbol_id new_ns_id) {
    S->ns_id = new_ns_id;
}

void set_namespace_name(istate* S, const string& name) {
    S->ns_id = intern_id(S, name);
}

void pop_to_global(istate* S, symbol_id name) {
    auto fqn = resolve_symbol(S, name);
    set_global(S, fqn, peek(S));
    pop(S);
}

void pop_to_fqn(istate* S, symbol_id fqn) {
    set_global(S, fqn, peek(S));
    pop(S);
}

bool push_global(istate* S, symbol_id name) {
    auto fqn = resolve_symbol(S, name);
    value out;
    if (get_global(out, S, fqn)) {
        push(S, out);
        return true;
    }
    return false;
}

bool push_macro(istate* S, symbol_id name) {
    auto fqn = resolve_symbol(S, name);
    value out;
    if (get_macro(out, S, fqn)) {
        push(S, out);
        return true;
    }
    return false;
}

}
