#include "allocator.hpp"
#include "compile2.hpp"
#include "istate.hpp"
#include "namespace.hpp"
#include "parse.hpp"
#include "vm.hpp"

#include <filesystem>

namespace fn {

namespace fs = std::filesystem;

istate* init_istate() {
    return alloc_istate("<internal>", fs::current_path().string());
}

void free_istate(istate* S) {
    if (has_error(S)) {
        clear_error(S->err);
    }
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

void set_directory(istate* S, const string& pathname) {
    push_string(S, pathname);
    S->wd = vstring(peek(S));
    pop(S);
}

void ierror(istate* S, const string& message) {
    set_error(S->err, message);
}

bool has_error(istate* S) {
    return S->err.happened;
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

void pop_to_list(istate* S, u32 n) {
    push(S, V_EMPTY);
    for (u32 i = 0; i < n; ++i) {
        alloc_cons(S, S->sp - 2 - i, S->sp - 2 - i, S->sp - 1 - i);
    }
    S->sp -= n;
}

// void push_foreign_fun(istate* S,
//         void (*foreign)(istate*),
//         const string& name,
//         const string& params) {
//     scanner_string_table sst;
//     auto forms = parse_string(S, sst, params);
//     if (has_error(S)) {
//         for (auto f : forms) {
//             ast::free_graph(f);
//         }
//         return;
//     }
//     auto& p = forms[0];
//     if (p->kind != ast::ak_list) {

//         ierror(S, "Malformed parameter list for foreign function.");
//         return;
//     }
//     u8 num_args = p->list_length;
//     bool vari = false;
//     // check for var arg
//     if (num_args >= 2) {
//         auto x = p->datum.list[num_args - 2];
//         if (x->kind == ast::ak_symbol
//                 && scanner_name(sst, x->datum.str_id) == "&") {
//             vari = true;
//             num_args -= 2;
//         }
//     }
//     for (auto f : forms) {
//         ast::free_graph(f);
//     }
//     push_nil(S);
//     alloc_foreign_fun(S, S->sp - 1, foreign, num_args, vari, 0, name);
// }

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

void interpret_stream(istate* S, std::istream* in) {
    // nil for empty files
    scanner_string_table sst;
    scanner sc{sst, *in};
    bool resumable;
    push_nil(S);
    if (!sc.eof_skip_ws()) {
        auto form0 = parse_next_node(S, sc, &resumable);
        if (form0->kind == ast::ak_list
                && form0->list_length == 2
                && form0->datum.list[0]->kind == ast::ak_symbol
                && form0->datum.list[1]->kind == ast::ak_symbol
                && intern(S, scanner_name(sst, form0->datum.list[0]->datum.str_id))
                == cached_sym(S, SC_NAMESPACE)) {
            switch_ns(S, intern(S, scanner_name(sst,
                                    form0->datum.list[1]->datum.str_id)));
        } else {
            if (has_error(S)) {
                return;
            }
            bc_compiler_output bco;
            bool resumable; // we won't actually use this
            auto root = form0;
            if (root == nullptr) {
                return;
            }
            compile_to_bytecode(bco, S, sst, root);
            reify_function(S, sst, bco);
            ast::free_graph(root);
            if (has_error(S)) {
                return;
            }
            // TODO: add a hook here to disassemble code
            disassemble_top(S);
            print_top(S);
            pop(S);
            call(S, 0);
            if (has_error(S)) {
                return;
            }
            // S->stack[S->sp - 2] = peek(S);
            // pop(S);
        }
    }
    while (!sc.eof_skip_ws()) {
        // pop prev return value
        pop(S);
        if (!compile_next_function(S, sc)) {
            return;
        }
        // TODO: add a hook here to disassemble code
        disassemble_top(S);
        print_top(S);
        pop(S);
        call(S, 0);
        if (has_error(S)) {
            return;
        }
        S->stack[S->sp - 2] = peek(S);
    }
}

bool load_file(istate* S, const string& pathname) {
    fs::path p = fs::path{convert_fn_string(S->wd)} / pathname;
    if (!fs::exists(p)) {
        ierror(S, "load_file() failed. File doesn't exist: " + p.string());
        return false;
    } else if (fs::is_directory(p)) {
        ierror(S, "load_file() failed. Provided file is a directory: "
                + p.string());
        return false;
    }

    std::ifstream in{p};
    if (in.bad()) {
        ierror(S, "load_file() failed. Could not open file: " + p.string());
        return false;
    }
    auto old_filename = convert_fn_string(S->filename);
    set_filename(S, p.string());
    interpret_stream(S, &in);
    set_filename(S, old_filename);
    return !has_error(S);
}

bool load_file_or_package(istate* S, const string& pathname) {
    fs::path p = fs::path{convert_fn_string(S->wd)} / pathname;
    if (!fs::exists(p)) {
        ierror(S, "load_package_or_file() failed. File doesn't exist: " + p.string());
        return false;
    }
    if (fs::is_directory(p)) {
        fs::path init_path = p / "__init.fn";
        auto old_wd = convert_fn_string(S->wd);
        set_directory(S, p.string());
        auto res = load_file(S, init_path);
        set_directory(S, old_wd);
        return res;
    } else {
        return load_file(S, pathname);
    }
}

string find_package(istate* S, const string& spec) {
    return "";
}

// compile a function and push the result to the stack
bool compile_next_function(istate* S, scanner& sc) {
    bool resumable;
    auto root = parse_next_node(S, sc, &resumable);
    bc_compiler_output bco;
    if (!compile_to_bytecode(bco, S, sc.get_sst(), root)) {
        ast::free_graph(root);
        return false;
    }
    reify_function(S, sc.get_sst(), bco);
    ast::free_graph(root);
    return !has_error(S);
}


}
