#include "base.hpp"
#include "builtin.hpp"
#include "bytes.hpp"
#include "gc.hpp"
#include "compile2.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

using namespace fn;

namespace fs = std::filesystem;
using std::endl;

void show_usage() {
    std::cout <<
        "Usage: fn [options] [PATH | -]\n"
        "Description:\n"
        "  Fn programming language interpreter and REPL.\n"
        "Options/Arguments:\n"
        "  -h            Show this help message and exit.\n"
        "  -i            Start the REPL after finishing evaluation.\n"
        "  -D dir        Set working directory.\n"
        "  -I dir        Add a package search directory. Can occur multiple times.\n"
        "  -             Take file input directly from STDIN.\n"
        "  FILE          File or package to interpret. Omitting this starts a REPL.\n"
        "Running with no options starts REPL in namespace fn/user/repl.\n"
        "When evaluating a file, the package and namespace are determined\n"
        "by the filename and package declaration, if present. Refer to\n"
        "the Fn manual for more information.\n"
        ;
}

struct interpreter_options {
    // holds filename to evaluate depending on mode. An empty filename is
    // treated as stdin.
    string src = "";
    // interpreter working directory
    string dir = "";
    // if true, show help and exit
    bool help = false;
    // whether to start a REPL. If src is nonempty, the file will be evaluated
    // before starting the REPL
    bool repl = false;
    // package include directories
    dyn_array<string> include;

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
    bool stdin_flag = false;
    for (i = 1; i < argc; ++i) {
        string s{argv[i]};
        if (s[0] == '-') {
            switch(s[1]) {
            case 'i':
                opt->repl = true;
                if (s[2] != '\0') {
                    opt->err = true;
                    opt->message = "Unrecognized option: " + s;
                    return;
                }
                break;
            case 'h':
                opt->help = true;
                if (s[2] != '\0') {
                    opt->err = true;
                    opt->message = "Unrecognized option: " + s;
                }
                // no sense doing further processing at this point
                return;
            case 'D':
                if (opt->dir != "") {
                    opt->err = true;
                    opt->message = "Multiple -D options.";
                    return;
                }
                // can have -D my/dir or -Dmy/dir syntax
                if (s[2] == '\0') {
                    if (i == argc - 1) {
                        opt->err = true;
                        opt->message = "Option -D requires an argument.";
                        return;
                    }
                    opt->dir = argv[++i];
                } else {
                    opt->dir = s.substr(1);
                }
                break;
            case 'I':
                if (i == argc - 1) {
                    opt->err = true;
                    opt->message = "Option -I requires an argument.";
                    return;
                }
                opt->include.push_back(argv[++i]);
                break;
            case '\0':
                stdin_flag = true;
                break;
            default:
                opt->err = true;
                opt->message = "Unrecognized option: " + s;
                break;
            }
        } else {
            // filename
            if (stdin_flag || opt->src != "") {
                opt->err = true;
                opt->message = "Multiple input sources provided.";
                return;
            }
            opt->src = s;
        }
    }
    // enable repl if no file was provided
    if (opt->src == "" && !stdin_flag) {
        opt->repl = true;
    }
}

int main(int argc, char** argv) {
    interpreter_options opt;
    process_args(argc, argv, &opt);
    if (opt.help) {
        show_usage();
        return 0;
    } else if (opt.err) {
        std::cout << "Error processing command line arguments:\n  "
                  << opt.message << '\n';
        return -1;
    }

    setup_gc_methods();
    auto S = init_istate();
    install_builtin(S);
    if (has_error(S)) {
        std::cout << "Error: " << *S->err.message << '\n';
        print_stack_trace(S);
        free_istate(S);
        return -1;
    }

    set_directory(S, opt.dir);
    set_namespace_name(S, "fn/user");
    if (opt.src != "") {
        if (load_file_or_package(S, opt.src)) {
            print_top(S);
            pop(S);
        }
    } else if (opt.repl) {
        // TODO: implement REPL :)
        std::cout << "Sorry, REPL isn't implemented :'(\n";
    } else {
        set_filename(S, "<stdin>");
        interpret_stream(S, &std::cin);
        if (!has_error(S)) {
            print_top(S);
            pop(S);
        }
    }
    if (has_error(S)) {
        std::cout << "Error: " << *S->err.message << '\n';
        print_stack_trace(S);
    }
    free_istate(S);

    return 0;
}

