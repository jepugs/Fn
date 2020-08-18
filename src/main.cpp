#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "values.hpp"
#include "vm.hpp"

#include <iostream>

using namespace std;
using namespace fn;
using namespace fn_bytes;


int main(int argc, const char** argv) {
    VM vm;
    Bytecode *code = vm.getBytecode();

    if (argc < 2) {
        cout << "Please provide an argument\n";
        return -1;
    }

    istringstream in(argv[1]);
    fn_scan::Scanner sc(&in);
    compile(&sc, code);
    //code->writeByte(OP_POP);
    //Token tok = sc.nextToken();
    //code->setLoc(tok.loc);
    //code->writeByte(OP_TRUE);
    //compileExpr(&sc, code);

    //code->writeByte(OP_NOP);
    //code->writeByte(OP_NOP);
    //compileExpr(&sc, code);
    //sc.nextToken();
    //compileExpr(&sc, code);
    disassemble(*code, cout);

    // u16 constId = code->addConstant({ .num = 42.5 });
    // u16 constId2 = code->addConstant({ .num = 3.14 });
    // code->writeByte(OP_CONST);
    // code->writeShort(constId);
    // code->writeByte(OP_CONST);
    // code->writeShort(constId2);
    // code->writeByte(OP_LOCAL);
    // code->writeByte(0x01);
    // code->writeByte(OP_EQ);

    // cout << "first byte is OP_CONST " << (OP_CONST == (*code)[0]) << endl;
    // cout << "first byte is OP_NULL " << (OP_NULL == (*code)[0]) << endl;
    
    cout << "initial stack pointer is " << vm.getStack()->sp << endl;
    vm.execute();
    //cout << "1st ip is " << vm.getIp() << endl;
    //vm.step();
    //cout << "2nd ip is " << vm.getIp() << endl;
    //vm.step();
    //cout << "3rd ip is " << vm.getIp() << endl;
    //vm.step();
    //cout << "4th ip is " << vm.getIp() << endl;
    //vm.step();
    //cout << "final ip is " << vm.getIp() << endl;

    // check the stack:
    // cout << "final stack pointer is " << vm.getStack()->sp << endl;
    // Value v = vm.getStack()->v[vm.getStack()->sp-1];
    Value v = vm.getGlobal("ey");
    if (isString(v)) {
        cout << "output string is " << *valueString(v) << endl;
    } else if (isNum(v)) {
        cout << "output num is " << v.num << endl;
    } else {
        cout << "output raw is " << v.raw << endl;
    }
}


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

