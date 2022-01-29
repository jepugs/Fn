#include "base.hpp"
#include "builtin.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include "expand.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

using namespace fn;

namespace fs = std::filesystem;
using std::endl;

void show_usage() {
    std::cout <<
        "Usage: fn [options] [--eval string | file | -] ARGS ...\n"
        "Description:\n"
        "  Fn programming language interpreter and REPL.\n"
        "Options/Arguments:\n"
        "  -h           Show this help message and exit.\n"
        "  -r           Start the REPL (after finishing evaluation).\n"
        "  -l           Print LLIR (low level intermediate rep) before\n"
        "                compiling each expression.\n"
        "  -d           Print disassembled bytecode after compiling.\n"
        "  -            Take input directly from STDIN.\n"
        "  --ns namespace   Specify the namespace for evaluation. In\n"
        "                    the case of file evaluation, this will\n"
        "                    override the file's package & namespace.\n"
        "                    The default namespace is fn/user/repl.\n"
        "  --eval string    Evaluate a string (instead of a file).\n"
        "  ARGS ...         These are passed to the interpreter as\n"
        "                    command line arguments.\n"
        "Running with no options starts REPL in namespace fn/user/repl.\n"
        "When evaluating a file, the package and namespace are determined\n"
        "by the filename and package declaration, if present. Refer to\n"
        "the Fn manual for more information.\n"
        ;
}

// specifies where the Fn source code comes from
enum eval_mode {
    em_file,
    em_string,
    em_stdin,
    em_none
};

struct interpreter_options {
    // where to get source code
    eval_mode mode = em_none;
    // holds filename or string to evaluate depending on mode
    string src = "";
    // namespace if one is set
    string ns = "fn/user/repl";
    // the start of program arguments in argv
    int args_start = 1;
    // if true, should just show help and exit. In this case, other fields of
    // the struct are not guaranteed to be initialized
    bool help = false;
    // whether to start a repl
    bool repl = false;
    // whether to print bytecode
    bool dis = false;
    // whether to print llir
    bool llir = false;

    // if true, the argument list was malformed and the other fields are not
    // guaranteed to be properly initialized
    bool err = false;
    string message = "";
};

// create an interpreter_options object based on CLI options. Returns false on
// malformed command line arguments.
void process_args(int argc, char** argv, interpreter_options* opt) {
    // process options first
    int i;
    for (i = 1; i < argc; ++i) {
        string s{argv[i]};
        if (s == string{"-"} || s == string{"--eval"} || s[0] != '-') {
            break;
        } else if (s == "--ns") {
            if (i + 1 == argc) {
                opt->err = true;
                opt->message = "--ns must be followed by a namespace name.";
                return;
            } else if (opt->message.size() != 0) {
                opt->err = true;
                opt->message = "Multiple --ns options.";
                return;
            } else {
                opt->ns = argv[++i];
            }
        } else if (s[0] == '-') {
            for (u32 j = 1; j < s.size(); ++j) {
                switch(s[j]) {
                case 'r':
                    opt->repl = true;
                    break;
                case 'd':
                    opt->dis = true;
                    break;
                case 'l':
                    opt->llir = true;
                    break;
                case 'h':
                    opt->help = true;
                    // no sense doing further processing at this point
                    return;
                default:
                    opt->err = true;
                    opt->message = "Unrecognized option in"
                        + string{s[j]} + ".";
                    return;
                }
            }
        } else {
            break;
        }
    }

    if (i == argc) {
        opt->args_start = i;
        opt->mode = em_none;
        opt->repl = true;
        return;
    }

    // process source
    string src{argv[i]};
    if (src == string{"--eval"}) {
        if(i == argc) {
            opt->err = true;
            opt->message = "--eval must be followed by a string to evaluate.";
            return;
        }
        opt->src = argv[++i];
        opt->mode = em_string;
    } else if (src == string{"-"}) {
        opt->mode = em_stdin;
    } else {
        opt->src = src;
        opt->mode = em_file;
    }
    opt->args_start = i+1;
}

int main(int argc, char** argv) {
    interpreter_options opt;
    process_args(argc, argv, &opt);
    if (opt.help) {
        show_usage();
        return 0;
    } else if (opt.err) {
        std::cout << "Error processing command line arguments:\n"
                  << opt.message << '\n';
        return -1;
    }

    auto S = init_istate();
    install_builtin(S);

    scanner sc{&std::cin};
    fault err;
    bool resumable;

    auto form = parse_next_form(&sc, S->symtab, &resumable, &err);
    while (form != nullptr) {
        compile_form(S, form);
        free_ast_form(form);
        if (S->err_happened) {
            std::cout << "Error: " << S->err_msg << '\n';
            free_istate(S);
            return -1;
        }
        disassemble_top(S, true);
        print_top(S);
        pop(S);
        call(S, 0);
        if (S->err_happened) {
            std::cout << "Error: " << S->err_msg << '\n';
            free_istate(S);
            return -1;
        }
        print_top(S);
        pop(S);
        form = parse_next_form(&sc, S->symtab, &resumable, &err);
    }
    free_istate(S);

    return 0;
}

