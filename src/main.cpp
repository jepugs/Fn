#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include <iostream>
#include <vector>
#include <unistd.h>

using namespace std;
using namespace fn;
using namespace fn_bytes;


void showUsage() {
    cout <<
        "Usage: fn [options] [-e string | file] ...\n"
        "Description:\n"
        "  fn programming language interpreter and repl.\n"
        "Options:\n"
        //"  -c            (UNIMPLEMENTED) compile files\n"
        "  -d            output disassembled bytecode instead of executing\n"
        "  -e string     evaluate the fn expressions in the string, printing the result.\n"
        "                  Multiple -e (options and filenames) can be mixed, in which\n"
        "                  case they will be evaluated in the order supplied.\n"
        "  -h            show this help message and exit\n"
        //"  -i            (UNIMPLEMENTED) instead of exiting, start a repl after completing other actions\n"
        //"  -o file       (UNIMPLEMENTED) write the output of -d or -c to the file\n"
        ;
}

void compileString(VM* vm, const string& str) {
    auto code = vm->getBytecode();
    istringstream in(str);
    fn_scan::Scanner sc(&in, "<cmdline>");
    compile(&sc, code);
}

// returns -1 on failure
int compileFile(VM* vm, const string& filename) {
    auto code = vm->getBytecode();
    ifstream in(filename);
    if (!in.is_open()) {
        // TODO: might be better to make this an FNError so we can have a single toplevel handler
        perror(("error opening file " + filename).c_str());
        return -1;
    } else {
        fn_scan::Scanner sc(&in, filename);
        compile(&sc, code);
    }

    return 0;
}

int main(int argc, char** argv) {
    int opt;
    // this contains filenames and strings to evaluate. The first character of each entry indicates
    // whether it is a filename ('f') or a string ('s').
    vector<string> evals;
    // if true, then output disassembled bytecode rather instead of running
    bool dis = false;
    
    // this depends on GNU getopt
    while ((opt = getopt(argc, argv, "-cde:hio:")) != -1) {
        switch (opt) {
        case 'c':
            cerr << "error: -c not yet implemented\n";
            return -1;
        case 'd':
            dis = true;
            break;
        case 'e':
            evals.push_back("s" + string(optarg));
            break;
        case 'h':
            showUsage();
            return 0;
        case 'i':
            cerr << "error: -i not yet implemented\n";
            return -1;
        case 'o':
            cerr << "error: -o not yet implemented\n";
            return -1;
        case 1:
            // non-arguments are filenames
            evals.push_back("f" + string(optarg));
            break;
        default:
            cerr << "error: unrecognized option\n";
            return -1;
        }
    }

    // check for no arguments
    if (optind == 1) {
        cerr << "error: repl not yet implemented :(\n\n";
        showUsage();
        return -1;
    }

    VM vm;
    for (auto s : evals) {
        if (s[0] == 's') {
            compileString(&vm, s.substr(1));
        } else {
            if (compileFile(&vm, s.substr(1)) == -1) {
                // exit on error; error message should already be printed
                return -1;
            }
        }
    }

    if (dis) {
        // disassembly mode
        // TODO: open a file if -o was supplied
        auto code = vm.getBytecode();
        disassemble(*code, cout);
        return 0;
    }

    // time to actually run the vm
    vm.execute();
    // FIXME: for now we print out the last value, but we probably really shouldn't
    cout << showValue(vm.lastPop()) << endl;
    return 0;
}

// int main(int argc, const char** argv) {
//     VM vm;
//     Bytecode *code = vm.getBytecode();

//     if (argc < 2) {
//         cout << "Please provide an argument\n";
//         return -1;
//     }

//     istringstream in(argv[1]);
//     fn_scan::Scanner sc(&in);
//     compile(&sc, code);
//     //code->writeByte(OP_POP);
//     //Token tok = sc.nextToken();
//     //code->setLoc(tok.loc);
//     //code->writeByte(OP_TRUE);
//     //compileExpr(&sc, code);

//     //code->writeByte(OP_NOP);
//     //code->writeByte(OP_NOP);
//     //compileExpr(&sc, code);
//     //sc.nextToken();
//     //compileExpr(&sc, code);
//     disassemble(*code, cout);

//     // u16 constId = code->addConstant({ .num = 42.5 });
//     // u16 constId2 = code->addConstant({ .num = 3.14 });
//     // code->writeByte(OP_CONST);
//     // code->writeShort(constId);
//     // code->writeByte(OP_CONST);
//     // code->writeShort(constId2);
//     // code->writeByte(OP_LOCAL);
//     // code->writeByte(0x01);
//     // code->writeByte(OP_EQ);

//     // cout << "first byte is OP_CONST " << (OP_CONST == (*code)[0]) << endl;
//     // cout << "first byte is OP_NULL " << (OP_NULL == (*code)[0]) << endl;
    
//     cout << "initial stack pointer is " << vm.getStack()->sp << endl;
//     vm.execute();
//     //cout << "1st ip is " << vm.getIp() << endl;
//     //vm.step();
//     //cout << "2nd ip is " << vm.getIp() << endl;
//     //vm.step();
//     //cout << "3rd ip is " << vm.getIp() << endl;
//     //vm.step();
//     //cout << "4th ip is " << vm.getIp() << endl;
//     //vm.step();
//     //cout << "final ip is " << vm.getIp() << endl;

//     // check the stack:
//     // cout << "final stack pointer is " << vm.getStack()->sp << endl;
//     // Value v = vm.getStack()->v[vm.getStack()->sp-1];
//     Value v = vm.getGlobal("ey");
//     if (isString(v)) {
//         cout << "output string is " << *valueString(v) << endl;
//     } else if (isNum(v)) {
//         cout << "output num is " << v.num << endl;
//     } else {
//         cout << "output raw is " << v.raw << endl;
//     }
// }


// scanner/table test

// int main(int argc, const char **argv) {
//     if (argc < 2) {
//         cout << "argument required\n";
//         return -1;
//     }

//     istringstream in(argv[1]);
//     fn_scan::Scanner sc(&in);

//     auto tok = sc.nextToken();
//     while (tok.tk != fn_scan::TKEOF) {
//         cout << tok.to_string() << endl;
//         tok = sc.nextToken();
//     }

//     // test of hash table
//     Table<int> testTab(1);
//     testTab.insert("test", 4);
//     testTab.insert("test2", 5);
//     testTab.insert("test3", 6);
//     auto res = testTab.get("test2");
//     res = testTab.get("test2");
//     res = testTab.get("test2");
//     res = testTab.get("test2");
//     res = testTab.get("test3");
//     if (res==nullptr) {
//         cout << "at test idx: " << "nothing" << endl;
//     } else {
//         cout << "at test idx: " << *res << endl;
//     }
    

//     return 0;
// }


// bytecode test

// int main(int argc, const char** argv) {
//     VM vm;
//     Bytecode *code = vm.getBytecode();

//     u16 constId = code->addConstant({ .num = 42.5 });
//     u16 constId2 = code->addConstant({ .num = 3.14 });
//     code->writeByte(OP_CONST);
//     code->writeShort(constId);
//     code->writeByte(OP_CONST);
//     code->writeShort(constId2);
//     code->writeByte(OP_LOCAL);
//     code->writeByte(0x01);
//     code->writeByte(OP_EQ);

//     //cout << "first byte is OP_CONST " << (OP_CONST == (*code)[0]) << endl;
    
//     //cout << "initial stack pointer is " << vm.getStack()->sp << endl;
//     vm.step();
//     vm.step();
//     vm.step();
//     vm.step();
//     cout << "final ip is " << vm.getIp() << endl;
//     // check the stack:
//     cout << "final stack pointer is " << vm.getStack()->sp << endl;
//     cout << "final stack top is " << (vm.getStack()->v[vm.getStack()->sp-1]).raw << endl;
// }

