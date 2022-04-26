#include "api.hpp"
#include "alloc.hpp"
#include "compile.hpp"
#include "gc.hpp"
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
        clear_error_info(S->err);
    }
    delete S->G;
    deinit_allocator(*S->alloc, S);
    delete S->alloc;
    delete S->symtab;
    delete S->symcache;
    delete S;
}

void set_filename(istate* S, const string& name) {
    push_str(S, name);
    S->filename = vstr(peek(S));
    pop(S);
}

void set_directory(istate* S, const string& pathname) {
    push_str(S, pathname);
    S->wd = vstr(peek(S));
    pop(S);
}

void ierror(istate* S, const string& message) {
    set_error_info(S->err, message);
}

bool has_error(istate* S) {
    return S->err.happened;
}

void push(istate* S, value v) {
    S->stack[S->sp++] = v;
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

symbol_id cached_sym(istate* S, sc_index i) {
    return S->symcache->syms[i];
}


void push_quoted(istate* S, const scanner_string_table& sst,
        const ast::node* root) {
    switch (root->kind) {
    case ast::ak_number:
        push_num(S, root->datum.num);
        break;
    case ast::ak_string:
        push_str(S, scanner_name(sst, root->datum.str_id));
        break;
    case ast::ak_symbol: {
        auto name = scanner_name(sst, root->datum.str_id);
        // attempt to resolve colon symbols. If resolution fails, we just leave
        // them as is
        if (!name.empty() && name[0] == ':') {
            symbol_id fqn;
            if (resolve_symbol(fqn, S, intern_id(S, name.substr(1)))) {
                push_sym(S, fqn);
            } else {
                push_sym(S, intern_id(S, name));
            }
        } else {
            push_sym(S, intern_id(S, name));
        }
    }
        break;
    case ast::ak_list:
        for (u32 i = 0; i < root->list_length; ++i) {
            push_quoted(S, sst, root->datum.list[i]);
        }
        pop_to_list(S, root->list_length);
        break;
    }
}

bool pop_syntax(ast::node*& result, istate* S, scanner_string_table& sst) {
    auto v = peek(S);
    // FIXME: get the source loc from the person asking to pop syntax
    source_loc loc{0, 0, false, 0};
    if (vis_number(v)) {
        result = ast::mk_number(loc, vnumber(peek(S)));
    } else if (vis_string(v)) {
        result = ast::mk_string(loc,
                scanner_intern(sst, convert_fn_str(vstr(v))));
    } else if (vis_symbol(v)) {
        result = ast::mk_symbol(loc,
                scanner_intern(sst, symname(S, vsymbol(v))));
    } else if (vis_emptyl(v)) {
        result = ast::mk_list(loc, 0, nullptr);
    } else if (vis_cons(v)) {
        dyn_array<ast::node*> buf;
        auto lst_addr = S->sp - 1;
        while(!vis_emptyl(S->stack[lst_addr])) {
            push(S, vhead(S->stack[lst_addr]));
            ast::node* sub;
            if (!pop_syntax(sub, S, sst)) {
                for (auto n : buf) {
                    ast::free_graph(n);
                }
                return false;
            }
            buf.push_back(sub);
            S->stack[lst_addr] = vtail(S->stack[lst_addr]);
        }
        result = ast::mk_list(loc, buf);
    } else {
        return false;
    }
    pop(S);
    return true;
}

void push_foreign_fun(istate* S,
        void (*foreign)(istate*),
        const string& name,
        const string& params) {
    scanner_string_table sst;
    auto forms = parse_string(S, sst, params);
    if (has_error(S)) {
        for (auto f : forms) {
            ast::free_graph(f);
        }
        return;
    }
    auto& p = forms[0];
    if (p->kind != ast::ak_list) {

        ierror(S, "Malformed parameter list for foreign function.");
        return;
    }
    u8 num_args = p->list_length;
    bool vari = false;
    // check for var arg
    if (num_args >= 2) {
        auto x = p->datum.list[num_args - 2];
        if (x->kind == ast::ak_symbol
                && scanner_name(sst, x->datum.str_id) == "&") {
            vari = true;
            num_args -= 2;
        }
    }
    for (auto f : forms) {
        ast::free_graph(f);
    }
    push_nil(S);
    alloc_foreign_fun(S, S->sp - 1, foreign, num_args, vari, name);
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
                os //<< "  File " << convert_fn_str(f.callee->stub->filename)
                   << "  In foreign function "
                   << convert_fn_str(f.callee->stub->name) << '\n';
            } else {
                auto c = instr_loc(f.callee->stub, f.pc);
                os << "  File " << string{(char*)f.callee->stub->filename->data}
                   << ", line " << c->loc.line << ", col " << c->loc.col;
                // FIXME: make it so this is never null
                if (f.callee->stub->name) {
                    os << " in "
                       << string{(char*)f.callee->stub->name->data};
                }
                os << '\n';
            }
        }
    }
    std::cout << os.str();
}

void interpret_stream(istate* S, std::istream* in) {
    // nil for empty files
    scanner_string_table sst;
    scanner sc{sst, *in, S};
    push_nil(S);
    if (!sc.eof_skip_ws()) {
        // the first expression has to be parsed manually because it may be a
        // namespace declaration.
        bool resumable;
        auto form0 = parse_next_node(S, sc, &resumable);
        if (form0->kind == ast::ak_list
                && form0->list_length == 2
                && form0->datum.list[0]->kind == ast::ak_symbol
                && form0->datum.list[1]->kind == ast::ak_symbol
                && intern_id(S, scanner_name(sst, form0->datum.list[0]->datum.str_id))
                == cached_sym(S, SC_NAMESPACE)) {
            switch_ns(S, intern_id(S, scanner_name(sst,
                                    form0->datum.list[1]->datum.str_id)));
            ast::free_graph(form0);
        } else {
            if (has_error(S)) {
                ast::free_graph(form0);
                return;
            }
            pop(S);
            bc_compiler_output bco;
            if (form0 == nullptr) {
                ast::free_graph(form0);
                return;
            }
            compile_to_bytecode(bco, S, sst, form0);
            reify_function(S, sst, bco);
            ast::free_graph(form0);
            if (has_error(S)) {
                return;
            }
            // TODO: add a hook here to disassemble code
            // disassemble_top(S, true);
            // print_top(S);
            // pop(S);
            call(S, 0);
            if (has_error(S)) {
                return;
            }
        }
    }
    while (!sc.eof_skip_ws()) {
        // pop prev return value
        pop(S);
        if (!compile_next_function(S, sc)) {
            return;
        }
        // TODO: add a hook here to disassemble code
        // disassemble_top(S, true);
        // print_top(S);
        // pop(S);
        call(S, 0);
        if (has_error(S)) {
            return;
        }
    }
}

bool load_file(istate* S, const string& pathname) {
    fs::path p = fs::path{convert_fn_str(S->wd)} / pathname;
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
    auto old_filename = convert_fn_str(S->filename);
    set_filename(S, p.string());
    interpret_stream(S, &in);
    set_filename(S, old_filename);
    return !has_error(S);
}

bool load_file_or_package(istate* S, const string& pathname) {
    fs::path p = fs::path{convert_fn_str(S->wd)} / pathname;
    if (!fs::exists(p)) {
        ierror(S, "load_file_or_package() failed. File doesn't exist: " + p.string());
        return false;
    }
    if (fs::is_directory(p)) {
        fs::path init_path = p / "__init.fn";
        auto old_wd = convert_fn_str(S->wd);
        set_directory(S, p.string());
        auto res = load_file(S, init_path);
        set_directory(S, old_wd);
        return res;
    } else {
        return load_file(S, pathname);
    }
}

string find_package(istate* S, const string& spec) {
    // TODO: write
    (void)S;
    (void)spec;
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
