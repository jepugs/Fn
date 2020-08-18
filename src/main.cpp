#include "base.hpp"
#include "bytes.hpp"
#include "scan.hpp"
#include "table.hpp"
#include "vm.hpp"
#include "values.hpp"

#include <iostream>

using namespace std;
using namespace fn;
using namespace fn_bytes;

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
//     if (res==nullptr) {
//         cout << "at test idx: " << "nothing" << endl;
//     } else {
//         cout << "at test idx: " << *res << endl;
//     }
    

//     return 0;
// }


int main(int argc, const char** argv) {
    VM vm;
    Bytecode *code = vm.getBytecode();

    u16 constId = code->addConstant({ .num = 42.5 });
    u16 constId2 = code->addConstant({ .num = 3.14 });
    code->writeByte(OP_CONST);
    code->writeShort(constId);
    code->writeByte(OP_CONST);
    code->writeShort(constId2);
    code->writeByte(OP_LOCAL);
    code->writeByte(0x01);
    code->writeByte(OP_EQ);

    //cout << "first byte is OP_CONST " << (OP_CONST == (*code)[0]) << endl;
    
    //cout << "initial stack pointer is " << vm.getStack()->sp << endl;
    vm.step();
    vm.step();
    vm.step();
    cout << "final ip is " << vm.getIp() << endl;
    // check the stack:
    cout << "final stack pointer is " << vm.getStack()->sp << endl;
    cout << "final stack top is " << (vm.getStack()->v[2]).num << endl;
}

