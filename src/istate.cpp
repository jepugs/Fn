#include "allocator.hpp"
#include "istate.hpp"
#include "namespace.hpp"
#include "parse.hpp"
#include "vm.hpp"
#include <cstring>

namespace fn {

static void setup_symcache(istate* S) {
    for (u32 i = 0; i < SYMCACHE_SIZE; ++i) {
        S->symcache->syms[i] = intern(S, sc_names[i]);
    }
}

istate* init_istate() {
    auto res = new istate;
    res->alloc = new allocator{res};
    // TODO: allocate this through the allocator instead
    res->symtab = new symbol_table;
    res->symcache = new symbol_cache;
    setup_symcache(res);
    res->G = new global_env;
    res->ns_id = intern(res, "fn/user");
    res->pc = 0;
    res->bp = 0;
    res->sp = 0;
    res->callee = nullptr;
    push_string(res, "<internal>");
    res->filename = vstring(peek(res));
    pop(res);
    res->err_happened = false;
    res->err_msg = nullptr;
    // set up namespace
    add_ns(res, res->ns_id);
    return res;
}

void free_istate(istate* S) {
    delete S->G;
    delete S->alloc;
    delete S->symtab;
    delete S->symcache;
    delete S;
}

void set_filename(istate* S, const string& name) {
    push_string(S, name);
    S->filename = vstring(peek(S));
    pop(S);
}

void ierror(istate* S, const string& message) {
    push_string(S, message);
    S->err_msg = vstring(peek(S));
    pop(S);
    S->err_happened = true;
}

void push(istate* S, value v) {
    S->stack[S->sp++] = v;
}

void pop(istate* S) {
    --S->sp;
}

void pop(istate* S, u32 n) {
    S->sp -= n;
}

value peek(istate* S) {
    return S->stack[S->sp - 1];
}

value peek(istate* S, u32 offset) {
    return S->stack[S->sp - offset - 1];
}

value get(istate* S, u32 index) {
    return S->stack[S->bp + index];
}

void set(istate* S, u32 index, value v) {
    S->stack[S->bp + index] = v;
}

symbol_id intern(istate* S, const string& str) {
    return S->symtab->intern(str);
}

symbol_id gensym(istate* S) {
    return S->symtab->gensym();
}

string symname(istate* S, symbol_id sym) {
    return S->symtab->symbol_name(sym);
}

symbol_id cached_sym(istate* S, sc_index i) {
    return S->symcache->syms[i];
}

void push_number(istate* S, f64 num) {
    push(S, vbox_number(num));
}
void push_string(istate* S, u32 size) {
    push_nil(S);
    alloc_string(S, S->sp - 1, size);
}
void push_string(istate* S, const string& str)  {
    push_nil(S);
    alloc_string(S, S->sp - 1, str);
}
void push_sym(istate* S, symbol_id sym) {
    push(S, vbox_symbol(sym));
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

void push_cons(istate* S, u32 hd, u32 tl) {
    push_nil(S);
    alloc_cons(S, S->sp - 1, hd, tl);
}

void push_table(istate* S) {
    push_nil(S);
    alloc_table(S, S->sp - 1);
}

static value* find_table_slot(fn_table* tab, value k) {
    auto h = hash(k);
    auto m = 2 * tab->cap;
    auto start = 2 * (h % tab->cap);
    auto data = (value*)tab->data->data;
    for (u32 i = start; i < m; i += 2) {
        if (data[i] == V_UNIN
                || data[i] == k) {
            return &data[i];
        }
    }
    // restart search from the beginning of the tree
    for (u32 i = 0; i < start; ++i) {
        if (data[i] == V_UNIN
                || data[i] == k) {
            return &data[i];
        }
    }
    return nullptr;
}

value* table_get(istate* S, fn_table* tab, value k) {
    auto h = hash(k);
    auto m = 2 * tab->cap;
    auto start = 2 * (h % tab->cap);
    auto data = (value*)tab->data->data;
    for (u32 i = start; i < m; i += 2) {
        if (data[i] == V_UNIN) {
            return nullptr;
        } else if (data[i] == k) {
            return &data[i];
        }
    }
    // restart search from the beginning of the tree
    for (u32 i = 0; i < start; ++i) {
        if (data[i] == V_UNIN) {
            return nullptr;
        } else if (data[i] == k) {
            return &data[i];
        }
    }
    return nullptr;
}

void table_set(istate* S, fn_table* tab, value k, value v) {
    // grow the table if necessary. This uses a 3/4 threshold
    if (tab->size >= tab->rehash) {
        auto old_cap = tab->cap;
        tab->cap = 2 * tab->cap;
        tab->rehash = tab->cap * 3 / 4;
        auto old_arr = (value*)tab->data->data;
        tab->data = alloc_gc_bytes(S->alloc, 2*tab->cap*sizeof(value));
        tab->size = 0;

        // initialize new array
        auto m = 2 * tab->cap;
        for (u32 i = 0; i < m; i += 2) {
            ((value*)tab->data->data)[i] = V_UNIN;
        }

        // insert old elements
        m = 2 * old_cap;
        for (u32 i = 0; i < m; i += 2) {
            if (old_arr[i] != V_UNIN) {
                auto x = find_table_slot(tab, old_arr[i]);
                x[0] = old_arr[i];
                x[1] = old_arr[i+1];
            }
        }
    }
    auto x = find_table_slot(tab, k);
    x[0] = k;
    x[1] = v;
}

void pop_to_list(istate* S, u32 n) {
    push(S, V_EMPTY);
    for (u32 i = 0; i < n; ++i) {
        alloc_cons(S, S->sp - 2 - i, S->sp - 2 - i, S->sp - 1 - i);
    }
    S->sp -= n;
}

void push_empty_fun(istate* S) {
    push_nil(S);
    alloc_empty_fun(S, S->sp - 1, S->ns_id);
}

void push_foreign_fun(istate* S,
        void (*foreign)(istate*),
        const string& name,
        const string& params) {
    auto forms = fn_parse::parse_string(params, S);
    if (S->err_happened) {
        for (auto f : forms) {
            free_ast_form(f);
        }
        return;
    }
    auto& p = forms[0];
    if (p->kind != fn_parse::ak_list) {

        ierror(S, "Malformed parameter list for foreign function.");
        return;
    }
    u8 num_args = p->list_length;
    bool vari = false;
    // check for var arg
    if (num_args >= 2) {
        auto x = p->datum.list[num_args - 2];
        if (x->kind == fn_parse::ak_symbol_atom
                && x->datum.sym == intern(S, "&")) {
            vari = true;
            num_args -= 2;
        }
    }
    for (auto f : forms) {
        free_ast_form(f);
    }
    push_nil(S);
    alloc_foreign_fun(S, S->sp - 1, foreign, num_args, vari, 0, name);
}

void print_top(istate* S) {
    std::cout << v_to_string(peek(S), S->symtab, true) << '\n';
}

void print_stack_trace(istate* S) {
    std::ostringstream os;
    os << "Stack trace:\n";
    for (auto& f : S->stack_trace) {
        if (f.callee) {
            if (f.callee->stub->foreign) {
                os << "  File " << convert_fn_string(f.callee->stub->filename)
                   << " in foreign function "
                   << convert_fn_string(f.callee->stub->name) << '\n';
            } else {
                auto c = instr_loc(f.callee->stub, f.pc);
                os << "  File " << string{(char*)f.callee->stub->filename->data}
                   << ", line " << c->loc.line << ", col " << c->loc.col << " in "
                   << string{(char*)f.callee->stub->name->data} << '\n';
            }
        }
    }
    std::cout << os.str();
}

}
