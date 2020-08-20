#include "vm.hpp"
#include "bytes.hpp"
#include "values.hpp"

#include <iostream>
#include <cstdlib>


namespace fn {

using namespace fn_bytes;

SymbolTable::SymbolTable() : byName(), byId() {}

const Symbol* SymbolTable::intern(string str) {
    u32 id = byId.size();
    Symbol s = { .id=id, .name=str };
    byId.push_back(s);
    byName.insert(str,s);
    return &(byId[byId.size() - 1]);
}

bool SymbolTable::isInternal(string str) {
    return byName.get(str) != nullptr;
}

Bytecode::Bytecode() : locs(nullptr), lastLoc(nullptr) {
    // allocate 256 bytes to start
    cap = 256;
    // use malloc so we can use realloc
    data = (u8*)malloc(256);

    size = 0;
}
Bytecode::~Bytecode() {
    free(data);

    // TODO: free strings in the constant table :(


    auto tmp = locs;
    while (tmp != nullptr) {
        locs = tmp->next;
        delete tmp;
        tmp = locs;
    }
}

void Bytecode::ensureCapacity(u32 newCap) {
    if (newCap <= cap) {
        return;
    }
    while (cap < newCap) {
        cap *= 2;
    }
    data = (u8*)realloc(data, cap);
}

u32 Bytecode::getSize() {
    return size;
}

void Bytecode::setLoc(CodeLoc l) {
    auto prev = lastLoc;
    lastLoc = new BytecodeLoc{ .maxAddr=0, .loc=l, .next=nullptr };
    if (prev == nullptr) {
        locs = lastLoc;
    } else {
        prev->maxAddr = size;
        prev->next = lastLoc;
    }
}

void Bytecode::writeByte(u8 b) {
    ensureCapacity(size + 1);
    data[size] = b;
    ++size;
}

void Bytecode::writeBytes(const u8* bytes, u32 len) {
    ensureCapacity(size + len);
    for (u32 i = 0; i < len; ++i) {
        data[size+i] = bytes[i];
    }
    size += len;
}

void Bytecode::writeShort(u16 s) {
    u8 bot = (u8) (s & 0x00ff);
    u8 top = (u8) (s >> 8);
    // write in little-endian order
    writeByte(bot);
    writeByte(top);
}

u8 Bytecode::readByte(u32 addr) {
    return data[addr];
}

u16 Bytecode::readShort(u32 addr) {
    u16 bot = (u16) readByte(addr);
    u16 top = (u16) readByte(addr + 1);
    return (top << 8) | bot;
}

// TODO: should check if this constant is already present. If it is, should reuse it.
u16 Bytecode::addConstant(Value v) {
    constants.push_back(v);
    return constants.size() - 1;
}

Value Bytecode::getConstant(u16 id) {
    return constants[id];
}

u32 Bytecode::symbolID(const string& name) {
    auto s = symbols.intern(name);
    return s->id;
}
Value Bytecode::symbol(const string& name) {
    auto s = symbols.intern(name);
    return { .raw = (((u64) s->id) << 8) | (u64) TAG_SYM };
}


VM::VM() : ip(0), stack(nullptr), lp(V_NULL) {
    this->stack = new CallFrame(0);
}
VM::~VM() {
    // delete all call frames
    while (stack != nullptr) {
        auto tmp = stack->next;
        delete stack;
        stack = tmp;
    }
}

CallFrame* VM::getStack() {
    return stack;
}

u32 VM::getIp() {
    return ip;
}

Value VM::lastPop() {
    return lp;
}

void VM::addGlobal(string name, Value v) {
    globals.insert(name, v);
}

Value VM::getGlobal(string name) {
    auto res = globals.get(name);
    if (res == nullptr) {
        // TODO: error
        return V_NULL;
    }
    return *res;
}

Bytecode* VM::getBytecode() {
    return &code;
}

void VM::push(Value v) {
    if (stack->sp == STACK_SIZE) {
        // TODO: stack overflow error
        return;
    }
    stack->v[stack->sp++] = v;
}

Value VM::pop() {
    if (stack->sp == 0) {
        // TODO: stack out of bounds exception here
        return V_NULL;
    }
    return stack->v[--stack->sp];
}

Value VM::peek(u32 i) {
    if (stack->sp <= i) {
        // TODO: stack out of bounds exception here
        return V_NULL;
    }
    return stack->v[stack->sp - i - 1];
}

Value VM::peekBot(u32 i) {
    if (stack->sp <= i) {
        // TODO: stack out of bounds exception here
        return V_NULL;
    }
    return stack->v[i];
}

#define pushBool(b) push(b ? V_TRUE : V_FALSE);
void VM::step() {

    u8 instr = code.readByte(ip);

    // variable for use inside the switch
    Value v1, v2;

    bool skip = false;
    bool jump = false;
    i8 offset = 0;

    // note: when an instruction uses an argument that occurs in the bytecode, it is responsible for
    // updating the instruction pointer at the end of its exection (which should be after any
    // exceptions that might be raised).
    switch (instr) {
    case OP_NOP:
        break;
    case OP_POP:
        lp = pop();
        break;
    case OP_COPY:
        v1 = peek(code.readByte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_LOCAL:
        v1 = peekBot(code.readByte(ip+1));
        push(v1);
        ++ip;
        break;

    case OP_GET_GLOBAL:
        v1 = pop();
        // TODO: check that v1 is a string
        push(getGlobal(*valueString(v1)));
        break;
    case OP_SET_GLOBAL:
        v1 = pop(); // name
        v2 = pop(); // value
        addGlobal(*valueString(v1), v2);
        break;

    case OP_CONST:
        push(code.getConstant(code.readShort(ip+1)));
        ip += 2;
        break;

    case OP_NULL:
        push(V_NULL);
        break;
    case OP_FALSE:
        push(V_FALSE);
        break;
    case OP_TRUE:
        push(V_TRUE);
        break;

    case OP_NEGATE:
        pushBool(!isTruthy(pop()));
        break;

    case OP_EQ:
        // TODO: EQ should check tags and descend on pointers
        v1 = pop();
        v2 = pop();
        pushBool(v1.raw == v2.raw);
        break;

    case OP_IS:
        v1 = pop();
        v2 = pop();
        pushBool(v1.raw == v2.raw);
        break;

    case OP_SKIP_TRUE:
        if(isTruthy(pop())) {
            skip = true;
        }
        break;
    case OP_SKIP_FALSE:
        if(!isTruthy(pop())) {
            skip = true;
        }
        break;

    case OP_JUMP:
        jump = true;
        offset = static_cast<i8>(code.readByte(ip+1));
        break;

    case OP_CALL:
    case OP_RETURN:
        // TODO: implement
        break;

    // numbers
    case OP_CK_NUM:
    case OP_CK_INT:
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_POW:
    case OP_GT:
    case OP_LT:
        // TODO: implement
        break;

    // lists
    case OP_CONS:
    case OP_HEAD:
    case OP_TAIL:
    case OP_CK_CONS:
    case OP_CK_EMPTY:
    case OP_CK_LIST:
        // TODO: implement
        break;

    default:
        break;
    }
    ++ip;

    // skip or jump if needed
    if (skip) {
        ip += instrWidth(code.readByte(ip));
    }
    if (jump) {
        ip += offset;
    }
}

void VM::execute() {
    while (ip < code.getSize()) {
        step();
    }
}


}
