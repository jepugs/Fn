#include "bytes.hpp"

#include "values.hpp"

#include <iomanip>

namespace fn_bytes {

// disassemble a single instruction, writing output to out
void disassemble_instr(const bytecode& code, bc_addr ip, std::ostream& out) {
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
        out << "closure " << code.read_short(ip+1);
        break;
    case OP_CLOSE:
        out << "close " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_GLOBAL:
        out << "global";
        break;
    case OP_SET_GLOBAL:
        out << "set-global";
        break;
    case OP_CONST:
        out << "const " << code.read_short(ip+1);
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
    case OP_OBJ_GET:
        out << "obj-get";
        break;
    case OP_OBJ_SET:
        out << "obj-set";
        break;
    case OP_NAMESPACE:
        out << "namespace";
        break;
    case OP_IMPORT:
        out << "import";
        break;
    case OP_JUMP:
        out << "jump " << (i32)(static_cast<i16>(code.read_short(ip+1)));
        break;
    case OP_CJUMP:
        out << "cjump " << (i32)(static_cast<i16>(code.read_short(ip+1)));
        break;
    case OP_CALL:
        out << "call " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_APPLY:
        out << "apply " << (i32)((code.read_byte(ip+1)));;
        break;
    case OP_RETURN:
        out << "return";
        break;
    case OP_TABLE:
        out << "table";
        break;

    default:
        out << "<unrecognized opcode: " << (i32)instr << ">";
        break;
    }
}

void disassemble(const bytecode& code, std::ostream& out) {
    u32 ip = 0;
    // t_od_o: annotate with line number
    while (ip < code.get_size()) {
        u8 instr = code[ip];
        // write line
        out << std::setw(6) << ip << "  ";
        disassemble_instr(code, ip, out);

        // additional information
        if (instr == OP_CONST) {
            // write constant value
            out << " ; "
                << v_to_string(code.get_constant(code.read_short(ip+1)), code.get_symbol_table());
        } else if (instr == OP_CLOSURE) {
            out << " ; addr = " << code.get_function(code.read_short(ip+1))->addr;
        }

        out << "\n";
        ip += instr_width(instr);
    }
}

}
