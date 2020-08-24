#include "bytes.hpp"
#include "values.hpp"

#include <iomanip>

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
    case OP_LOCAL:
        out << "local " << (i32)code[ip+1];
        break;
    case OP_SET_LOCAL:
        out << "set-local " << (i32)code[ip+1];
        break;
    case OP_COPY:
        out << "copy " << (i32)code[ip+1];
        break;
    case OP_UPVALUE:
        out << "upvalue " << (i32)code[ip+1];
        break;
    case OP_SET_UPVALUE:
        out << "set-upvalue " << (i32)code[ip+1];
        break;
    case OP_CLOSURE:
        out << "closure " << code.readShort(ip+1);
        break;
    case OP_CLOSE:
        out << "close " << (i32)((code.readByte(ip+1)));;
        break;
    case OP_GLOBAL:
        out << "global";
        break;
    case OP_SET_GLOBAL:
        out << "set-global";
        break;
    case OP_CONST:
        out << "const " << code.readShort(ip+1);
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
    case OP_JUMP:
        out << "jump " << (i32)(static_cast<i16>(code.readShort(ip+1)));
        break;
    case OP_CJUMP:
        out << "cjump " << (i32)(static_cast<i16>(code.readShort(ip+1)));
        break;
    case OP_CALL:
        out << "call " << (i32)((code.readByte(ip+1)));;
        break;
    case OP_RETURN:
        out << "return";
        break;

    default:
        out << "<unrecognized opcode: " << (i32)instr << ">";
        break;
    }
}

void disassemble(Bytecode& code, ostream& out) {
    u32 ip = 0;
    // TODO: annotate with line number
    while (ip < code.getSize()) {
        u8 instr = code[ip];
        // write line
        out << setw(6) << ip << "  ";
        disassembleInstr(code, ip, out);

        // additional information
        if (instr == OP_CONST) {
            // write constant value
            out << " ; "
                << showValue(code.getConstant(code.readShort(ip+1)));
        } else if (instr == OP_CLOSURE) {
            out << " ; addr = " << code.getFunction(code.readShort(ip+1))->addr;
        }

        out << "\n";
        ip += instrWidth(instr);
    }
}

}
