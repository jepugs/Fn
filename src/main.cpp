#include "base.hpp"
#include "bytes.hpp"
#include "ffi/builtin.hpp"
#include "interpret.hpp"
#include "table.hpp"
#include "values.hpp"

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
    static_assert(std::is_standard_layout<code_chunk>::value);
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

    logger log{&std::cerr, &std::cout};
    interpreter inter{&log};
    install_builtin(inter);
    // Important: configure logging after install_builtin. Usually don't want to
    // disassemble the standard library.
    inter.set_log_dis(opt.dis);
    inter.set_log_llir(opt.llir);
    // FIXME: this might cause a chunk to only be added to the allocator after
    // the allocator has been deleted, which is no bueno
    auto ws = inter.get_alloc()->add_working_set();

    // namespace to use for evaluation
    auto ns_id = inter.intern("fn/user");

    // evaluate
    // FIXME: use --ns arg when applicable
    fault i_err;
    value res;
    switch (opt.mode) {
    case em_file:
        res = inter.interpret_main_file(opt.src, &ws, &i_err);
        if (i_err.happened) {
            return -1;
        }
        break;
    case em_string:
        res = inter.interpret_string(opt.src, ns_id, &ws, &i_err);
        if (i_err.happened) {
            return -1;
        }
        break;
    case em_stdin:
        {
            scanner sc{&std::cin, "STDIN"};
            bool resumable;
            res = inter.interpret_from_scanner(&sc, ns_id, &ws, &resumable,
                    &i_err);
            if (i_err.happened) {
                return -1;
            }
        }
        break;
    case em_none:
        break;
    }
    
    // run the repl if necessary
    dyn_array<value> vals;
    if (opt.repl) {
        string buf;
        u32 bytes_used = 0;
        bool still_reading = false;
        while (!std::cin.eof()) {
            // declared inside the loop so return values get garbage collected
            auto ws = inter.get_alloc()->add_working_set();
            string line;
            if (still_reading) {
                std::cout << " >> ";
                std::cout.flush();
                std::getline(std::cin, line);
                buf += '\n' + line;
            } else {
                std::cout << "fn> ";
                std::cout.flush();
                std::getline(std::cin, line);
                buf = line;
                // TODO: check for keywords for repl commands
                //if(line[0] == ':')
            }

            bool resumable;
            fault err;
            vals = inter.partial_interpret_string(buf, ns_id, &ws,
                    &bytes_used, &resumable, &err);
            if (err.happened) {
                if (resumable) {
                    std::istringstream s{buf.substr(bytes_used)};
                    if (s.eof()) {
                        still_reading = false;
                    } else {
                        std::getline(s, buf);
                        still_reading = true;
                    }
                } else {
                    still_reading = false;
                }
            } else if (vals.size > 0) { // emit nothing on no input
                buf = "";
                // print value
                for (auto x : vals) {
                    std::cout << v_to_string(x, inter.get_symtab(), true) << '\n';
                    still_reading = false;
                }
            }
        }
    } else {
        std::cout << v_to_string(res, inter.get_symtab());
    }
    std::cout << endl;

    return 0;
}

