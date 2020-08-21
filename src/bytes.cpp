#include "bytes.hpp"
#include "values.hpp"

namespace fn_bytes {

// disassemble a single instruction, writing output to out
void disassembleInstr(Bytecode& code, u32 ip, ostream& out) {
    u8 instr = code[ip];
    switch (instr) {
    case OP_NOP:
        out << "nop";
        break;
    case OP_POP:
        out << "pop";
        break;
    case OP_GET_GLOBAL:
        out << "get-global";
        break;
    case OP_SET_GLOBAL:
        out << "set-global";
        break;
    case OP_NULL:
        out << "null";
        break;
    case OP_FALSE:
        out << "false";
        break;
    case OP_TRUE:
        out << "true";
        break;
    case OP_NEGATE:
        out << "negate";
        break;
    case OP_EQ:
        out << "eq";
        break;
    case OP_IS:
        out << "is";
        break;
    case OP_SKIP_TRUE:
        out << "skip-true";
        break;
    case OP_SKIP_FALSE:
        out << "skip-false";
        break;
    case OP_RETURN:
        out << "return";
        break;
    // numbers
    case OP_CK_NUM:
        out << "ck-num";
        break;
    case OP_CK_INT:
        out << "ck-int";
        break;
    case OP_ADD:
        out << "add";
        break;
    case OP_SUB:
        out << "sub";
        break;
    case OP_MUL:
        out << "mul";
        break;
    case OP_DIV:
        out << "div";
        break;
    case OP_POW:
        out << "pow";
        break;
    case OP_GT:
        out << "gt";
        break;
    case OP_LT:
        out << "lt";
        break;

    case OP_CONS:
        out << "cons";
        break;
    case OP_HEAD:
        out << "head";
        break;
    case OP_TAIL:
        out << "tail";
        break;
    case OP_CK_CONS:
        out << "ck-cons";
        break;
    case OP_CK_EMPTY:
        out << "ck-empty";
        break;
    case OP_CK_LIST:
        out << "ck-list";
        break;

    case OP_COPY:
        out << "copy " << (i32)code[ip+1];
        break;
    case OP_LOCAL:
        out << "local " << (i32)code[ip+1];
        break;
    case OP_JUMP:
        out << "jump " << (i32)(static_cast<i16>(code.readShort(ip+1)));
        break;
    case OP_CALL:
        out << "call " << (i32)((code.readByte(ip+1)));;
        break;
    case OP_CONST:
        out << "const " << code.readShort(ip+1);
        break;

    default:
        out << "<unrecognized byte: " << instr << ">";
        break;
    }
}

void disassemble(Bytecode& code, ostream& out) {
    u32 ip = 0;
    // TODO: annotate with line number
    while (ip < code.getSize()) {
        u8 instr = code[ip];
        disassembleInstr(code, ip, out);
        // write constant value
        if (instr == OP_CONST) {
            out << "    ; constant: "
                << showValue(code.getConstant(code.readShort(ip+1)));
        }

        out << "\n";
        ip += instrWidth(instr);
    }
}

}
