#include "vm.hpp"
#include "bytes.hpp"
#include "values.hpp"

#include <iostream>
#include <cstdlib>
#include <numeric>


namespace fn {

using namespace fn_bytes;

SymbolTable::SymbolTable() : byName(), byId() { }

const Symbol* SymbolTable::intern(string str) {
    auto v = byName.get(str);
    if (v.has_value()) {
        return *v;
    }
    u32 id = byId.size();
    Symbol s = { .id=id, .name=str };
    byId.push_back(s);
    byName.insert(str,s);
    return &(byId[byId.size() - 1]);
}

bool SymbolTable::isInternal(string str) {
    return byName.get(str).has_value();
}

u8 FuncStub::getUpvalue(Local slot, bool direct) {
    for (Local i = 0; i < numUpvals; ++i) {
        auto u = upvals[i];
        if (u.slot == slot && u.direct == direct) {
            // found the upvalue
            return i;
        }
    }
    // add a new upvalue
    upvals.push_back({ .slot=slot, .direct=direct });

    return numUpvals++;
}

Bytecode::Bytecode() : locs(nullptr), lastLoc(nullptr), symbols() {
    // allocate 256 bytes to start
    cap = 256;
    // use malloc so we can use realloc
    data = (u8*)malloc(256);

    size = 0;

    //setLoc(SourceLoc(std::shared_ptr<string>(new string("")), 0, 0));
    setLoc(SourceLoc(std::shared_ptr<string>(new string("")), 0, 0));
}
Bytecode::~Bytecode() {
    free(data);

    for (auto v : managedConstants) {
        if (v.isStr()) {
            delete v.ustr();
        } else if (v.isCons()) {
            delete v.ucons();
        }
    }

    for (auto f : functions) {
        delete f;
    }

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

void Bytecode::setLoc(SourceLoc l) {
    auto prev = lastLoc;
    lastLoc = new BytecodeLoc{ .maxAddr=0, .loc=l, .next=nullptr };
    if (prev == nullptr) {
        locs = lastLoc;
    } else {
        prev->maxAddr = size;
        prev->next = lastLoc;
    }
}

SourceLoc* Bytecode::locationOf(Addr addr) {
    if(locs == nullptr)
        return nullptr;

    auto l = locs;
    // note: maxAddr of 0 indicates that this was the last location set and so it doesn't have an
    // upper limit yet.
    while (l->maxAddr <= addr && l->maxAddr != 0) {
        l = l->next;
    }
    return &l->loc;
}

void Bytecode::writeByte(u8 b) {
    ensureCapacity(size + 1);
    data[size] = b;
    ++size;
}

void Bytecode::writeBytes(const u8* bytes, Addr len) {
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

u8 Bytecode::readByte(Addr addr) {
    return data[addr];
}

u16 Bytecode::readShort(Addr addr) {
    u16 bot = (u16) readByte(addr);
    u16 top = (u16) readByte(addr + 1);
    return (top << 8) | bot;
}

void Bytecode::patchShort(Addr addr, u16 s) {
    u8 bot = (u8) (s & 0x00ff);
    u8 top = (u8) (s >> 8);
    data[addr] = bot;
    data[addr+1] = top;
}

Value Bytecode::getConstant(u16 id) {
    return constants[id];
}

u16 Bytecode::numConstants() {
    return constants.size();
}

u16 Bytecode::addFunction(Local arity,bool vararg, Value modId) {
    arity -= vararg ? 1 : 0;
    functions.push_back(new FuncStub {
            .positional=arity, 
            .required=arity,
            .varargs=vararg,
            .numUpvals=0,
            .upvals=vector<Upvalue>(),
            .modId=modId,
            .addr=getSize()});
    return (u16) functions.size() - 1;
}

FuncStub* Bytecode::getFunction(u16 id) {
    // TODO: check bounds?
    return functions[id];
}

u16 Bytecode::addConst(Value v) {
    constants.push_back(v);
    return constants.size() - 1;
}

u16 Bytecode::numConst(f64 num) {
    return addConst(value(num));
}

u16 Bytecode::strConst(const string& name) {
    auto v = value(new FnString(name));
    managedConstants.push_front(v);
    return addConst(v);
}

u16 Bytecode::strConst(const char* name) {
    auto v = value(new FnString(name));
    managedConstants.push_front(v);
    return addConst(v);
}

u16 Bytecode::consConst(Value hd, Value tl) {
    auto v = value(new Cons(hd, tl));
    managedConstants.push_front(v);
    return addConst(v);
}

u16 Bytecode::symConst(const string& name) {
    return addConst(symbol(name));
}

SymbolTable* Bytecode::getSymbols() {
    return &this->symbols;
}

u32 Bytecode::symbolID(const string& name) {
    auto s = symbols.intern(name);
    return s->id;
}
Value Bytecode::symbol(const string& name) {
    auto s = symbols.intern(name);
    return { .raw = (((u64) s->id) << 8) | (u64) TAG_SYM };
}


CallFrame* CallFrame::extendFrame(Addr retAddr, Local numArgs, Function* caller) {
    return new CallFrame(this, retAddr, bp+sp-numArgs, caller, numArgs);
}

UpvalueSlot CallFrame::openUpvalue(Local pos, Value* ptr) {
    if (pos >= sp) {
        // TODO: probably an error
        return nullptr;
    }

    // check if an upvalue is already open for this stack position
    for (auto u : openUpvals) {
        if (u.pos == pos) {
            return u.slot;
        }
    }
    // TODO: refCount
    auto res = UpvalueSlot(ptr);
    openUpvals.push_front(OpenUpvalue{ .slot=res, .pos=pos });
    return res;
}

void CallFrame::close(StackAddr n) {
    sp -= n;
    openUpvals.remove_if([this](auto u) {
        if (u.pos >= sp) {
            *u.slot.open = false;
            auto v = **u.slot.val;
            *u.slot.val = new Value();
            **u.slot.val = v;
            return true;
        }
        return false;
    });
}

void CallFrame::closeAll() {
    sp = 0;
    for (auto u : openUpvals) {
        *u.slot.open = false;
        auto v = **u.slot.val;
        *u.slot.val = new Value();
        **u.slot.val = v;
    }

    openUpvals.clear();
}


VM::VM()
    : code(), coreMod(nullptr), alloc(), ip(0),
      frame(new CallFrame(nullptr, 0, 0, nullptr)), lp(V_NULL) {
    ns = vObj(alloc.obj());
    auto modId = alloc.cons(code.symbol("core"), V_EMPTY);
    modId = alloc.cons(code.symbol("fn"),modId);
    module = initModule(modId);
    coreMod = module;
}

VM::~VM() {
    // delete all call frames
    while (frame != nullptr) {
        auto tmp = frame->prev;
        // TODO: ensure reference count for UpvalueSlot is decremented
        delete frame;
        frame = tmp;
    }
}

Obj* VM::initModule(Value modId) {
    if (vTag(modId) != TAG_CONS) {
        runtimeError("Module initialization failed: module id not a list of symbols.");
    }

    // create the module in the ns hierarchy
    auto x = modId;
    Value key;
    Obj* res = ns;
    optional<Value*> v;
    // FIXME: it's still possible to overwrite a variable here
    while (x != V_EMPTY) {
        key = vHead(x);
        if (vTag(key) != TAG_SYM) {
            runtimeError("Module initialization failed: module id not a list of symbols.");
        }
        v = res->contents.get(key);
        if (!v.has_value()) {
            auto tmp = vObj(alloc.obj());
            res->contents.insert(key, value(tmp));
            res = tmp;
        } else if (vShortTag(**v) == TAG_OBJ) {
            res = vObj(**v);
        } else {
            runtimeError("Module initialization would clobber existing variable.");
        }
        x = vTail(x);
        // TODO: check tail is a list (and also do it in findModule)
    }

    // check if this is already a module
    if (res->contents.get(code.symbol("_modinfo"))) {
        runtimeError("Module initialization would clobber existing module.");
    }

    // TODO: this only happens once in the constructor.
    if (coreMod != nullptr) {
        res->contents = Table(coreMod->contents);
    }
    auto cts = &res->contents;
    cts->insert(code.symbol("ns"), value(ns));

    // make a global _modinfo object
    Obj* modinfo = vObj(alloc.obj());
    cts->insert(code.symbol("_modinfo"), value(modinfo));
    // name is the last symbol in the module id
    modinfo->contents.insert(code.symbol("name"), key);
    modinfo->contents.insert(code.symbol("id"), modId);
    // the file from which this was loaded. The compiler generates code to set this when importing a file.
    modinfo->contents.insert(code.symbol("source"), alloc.str("<internal>"));

    return res;
}

// TODO: make optional<Obj>
Obj* VM::findModule(Value modId) {
    if (vTag(modId) != TAG_CONS) {
        runtimeError("Module search failed: module id not a list of symbols.");
    }

    // create the module in the ns hierarchy
    auto x = modId;
    Value key;
    Obj* res = ns;
    optional<Value*> v;
    while (x != V_EMPTY) {
        // TODO: check that everything is a package or a module.
        key = vHead(x);
        if (vTag(key) != TAG_SYM) {
            runtimeError("Module search failed: module id not a list of symbols.");
        }
        v = res->contents.get(key);
        if (!v.has_value()) {
            return nullptr;
        } else if (vShortTag(**v) == TAG_OBJ) {
            res = vObj(**v);
        }
        x = vTail(x);
    }

    // ensure this is already a module
    if (res->contents.get(code.symbol("_modinfo")) == nullptr) {
        runtimeError("Module search failed: module id names a variable.");
    }
    return res;
}

u32 VM::getIp() {
    return ip;
}

Value VM::lastPop() {
    return lp;
}

void VM::addGlobal(Value name, Value v) {
    if (frame != nullptr && frame->caller != nullptr) {
        auto modId = frame->caller->stub->modId;
        auto mod = findModule(modId);
        if (mod == nullptr) {
            runtimeError("Function has nonsensical module ID. (This is probably a bug).");
        } else {
            mod->contents.insert(name, v);
        }
    } else {
        module->contents.insert(name, v);
    }
}

Value VM::getGlobal(Value name) {
    optional<Value*> res;
    if (frame != nullptr && frame->caller != nullptr) {
        auto modId = frame->caller->stub->modId;
        auto mod = findModule(modId);
        if (mod == nullptr) {
            runtimeError("Function has nonsensical module ID. (This is probably a bug).");
        } else {
            res = mod->contents.get(name);
        }
    } else {
        res = module->contents.get(name);
    }

    if (!res.has_value()) {
        runtimeError("Attempt to access unbound global variable " + vToString(name, code.getSymbols()));
    }
    return **res;
}

UpvalueSlot VM::getUpvalue(u8 id) {
    if (frame->caller == nullptr || frame->caller->stub->numUpvals <= id) {
        throw FNError("interpreter", "Attempt to access nonexistent upvalue",
                      *code.locationOf(ip));
        // TODO: error: nonexistent upvalue
    }
    return frame->caller->upvals[id];
}


void VM::addForeign(string name, Value (*func)(Local, Value*, VM*), Local minArgs, bool varArgs) {
    auto v = alloc.foreign(minArgs, varArgs, func);
    addGlobal(code.symbol(name), v);
    foreignFuncs.push_back(v);
}

Bytecode* VM::getBytecode() {
    return &code;
}
Allocator* VM::getAlloc() {
    return &alloc;
}

void VM::runtimeError(const string& msg) {
    throw FNError("runtime", "(ip = " + std::to_string(ip) + ") " + msg, *code.locationOf(ip));
}

void VM::push(Value v) {
    if (frame->sp + frame->bp >= STACK_SIZE - 1) {
        throw FNError("runtime", "Stack exhausted.", *code.locationOf(ip));
    }
    stack[frame->bp + frame->sp++] = v;
}

Value VM::pop() {
    if (frame->sp == 0) {
        throw FNError("runtime",
                      "Pop on empty call frame at address " + std::to_string((i32)ip),
                      *code.locationOf(ip));
    }
    return stack[frame->bp + --frame->sp];
}

Value VM::popTimes(StackAddr n) {
    if (frame->sp < n) {
        throw FNError("runtime",
                      "Pop on empty call frame at address " + std::to_string((i32)ip),
                      *code.locationOf(ip));
    }
    frame->sp -= n;
    return stack[frame->bp + frame->sp];
}

Value VM::peek(StackAddr i) {
    if (frame->sp <= i) {
        throw FNError("runtime",
                      "Peek out of stack bounds at address " + std::to_string((i32)ip),
                      *code.locationOf(ip));
    }
    return stack[frame->bp + frame->sp - i - 1];
}

Value VM::local(Local i) {
    StackAddr pos = i + frame->bp;
    if (frame->sp <= i) {
        throw FNError("runtime", "Out of stack bounds on local.", *code.locationOf(ip));
    }
    return stack[pos];
}

void VM::setLocal(Local i, Value v) {
    StackAddr pos = i + frame->bp;
    if (frame->sp <= i) {
        runtimeError("Out of stack bounds on set-local.");
    }
    stack[pos] = v;
}

Addr VM::apply(Local numArgs) {
    // argument to expand
    auto v = pop();
    auto tag = vTag(v);
    if (tag != TAG_EMPTY && tag != TAG_CONS) {
        runtimeError("Last argument to apply not a list.");
    }

    auto vlen = 0;
    while (vTag(v) != TAG_EMPTY) {
        push(vCons(v)->head);
        v = vCons(v)->tail;
        ++vlen;
    }

    // TODO: use a maxargs constant
    if (vlen + numArgs - 1 > 255) {
        runtimeError("Too many arguments for function call in apply.");
    }
    return call(vlen + numArgs - 1);
}

// TODO: switch to use runtimeError
Addr VM::call(Local numArgs) {
    // the function to call should be at the bottom
    auto v1 = peek(numArgs);
    Value res;
    auto tag = vTag(v1);
    if (tag == TAG_FUNC) {
        auto func = vFunc(v1);
        auto stub = func->stub;
        auto mod = findModule(stub->modId);
        if (mod == nullptr) {
            runtimeError("Function has nonexistent module id.");
        }
        // TODO: switch to the function's module. Probably put in the callframe
        if (numArgs < stub->required) {
            throw FNError("interpreter",
                          "Too few arguments for function call at ip=" + std::to_string(ip),
                          *code.locationOf(ip));
        } else if (stub->varargs) {
            // make a list out of the trailing arguments
            auto vararg = V_EMPTY;
            for (auto i = numArgs-stub->positional; i > 0; --i) {
                vararg = alloc.cons(pop(), vararg);
            }
            push(vararg);
            frame = frame->extendFrame(ip+2, stub->positional+1, func);
        } else if (numArgs > stub->positional) {
            throw FNError("interpreter",
                          "Too many arguments for function call at ip=" + std::to_string(ip),
                          *code.locationOf(ip));
        } else {
            // normal function call
            frame = frame->extendFrame(ip+2, numArgs, func);
        }

        return stub->addr;
    } else if (tag == TAG_FOREIGN) {
        auto f = vForeign(v1);
        if (numArgs < f->minArgs) {
            throw FNError("interpreter",
                          "Too few arguments for foreign function call at ip=" + std::to_string(ip),
                          *code.locationOf(ip));
        } else if (!f->varArgs && numArgs > f->minArgs) {
            throw FNError("interpreter",
                          "Too many arguments for foreign function call at ip=" + std::to_string(ip),
                          *code.locationOf(ip));
        } else {
            // correct arity
            res = f->func((u16)numArgs, &stack[frame->bp + frame->sp - numArgs], this);
        }
        // pop args+1 times (to get the function)
        popTimes(numArgs+1);
        push(res);
        return ip + 2;
    } else {
        // TODO: exception: not a function
        throw FNError("interpreter",
                      "Attempt to call nonfunction at address " + std::to_string((i32)ip),
                      *code.locationOf(ip));
        return ip + 2;
    }
}


#define pushBool(b) push(b ? V_TRUE : V_FALSE);
void VM::step() {

    u8 instr = code.readByte(ip);

    // variable for use inside the switch
    Value v1, v2, v3;
    optional<Value*> vp;

    bool skip = false;
    bool jump = false;
    Addr addr = 0;

    Obj* mod;

    Local numArgs;

    FuncStub* stub;

    Local l;
    u16 id;

    CallFrame *tmp;

    UpvalueSlot u;

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
        v1 = local(code.readByte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_SET_LOCAL:
        v1 = pop();
        setLocal(code.readByte(ip+1), v1);
        ++ip;
        break;

    case OP_UPVALUE:
        l = code.readByte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        push(**u.val);
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = code.readByte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        **u.val = pop();
        ++ip;
        break;

    case OP_CLOSURE:
        id = code.readShort(ip+1);
        stub = code.getFunction(code.readShort(ip+1));
        v1 = alloc.func(stub,
                        [this, stub](auto upvals) {
                            for (u32 i = 0; i < stub->numUpvals; ++i) {
                                auto u = stub->upvals[i];
                                if (u.direct) {
                                    upvals[i] = frame->openUpvalue(u.slot, &stack[frame->bp + u.slot]);
                                } else {
                                    upvals[i] = getUpvalue(u.slot);
                                }
                            }
                                
                        });
        push(v1);
        ip += 2;
        break;

    case OP_CLOSE:
        numArgs = code.readByte(ip+1);
        frame->close(numArgs);
        // TODO: check stack size >= numArgs
        ++ip;
        break;

    case OP_GLOBAL:
        v1 = pop();
        if (vTag(v1) != TAG_SYM) {
            runtimeError("OP_GLOBAL name operand is not a symbol.");
        }
        push(getGlobal(v1));
        break;
    case OP_SET_GLOBAL:
        v1 = pop(); // value
        v2 = peek(); // name
        if (vTag(v2) != TAG_SYM) {
            runtimeError("OP_SET_GLOBAL name operand is not a symbol.");
        }
        addGlobal(v2, v1);
        break;

    case OP_CONST:
        id = code.readShort(ip+1);
        if (id >= code.numConstants()) {
            throw FNError("runtime", "Attempt to access nonexistent constant.",
                          *code.locationOf(ip));
        }
        push(code.getConstant(id));
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

    case OP_OBJ_GET:
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (vTag(v2) != TAG_OBJ) {
            runtimeError("obj-get operand not a general object");
        }
        vp = vObj(v2)->contents.get(v1);
        push(vp.has_value() ? **vp : V_NULL);
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = pop();
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (vTag(v2) != TAG_OBJ) {
            runtimeError("obj-set operand not a general object");
        }
        vObj(v2)->contents.insert(v1,v3);
        push(v3);
        break;

    case OP_MODULE:
        v1 = pop();
        // TODO: check for _modinfo property
        if (vTag(v1) != TAG_OBJ) {
            runtimeError("module operand not a general object");
        }
        module = vObj(v1);
        break;

    case OP_IMPORT:
        v1 = pop();
        mod = findModule(v1);
        if (mod == nullptr) {
            mod = initModule(v1);
        }
        push(value(mod));
        break;

    case OP_JUMP:
        jump = true;
        addr = ip + 3 + static_cast<i8>(code.readByte(ip+1));
        break;

    case OP_CJUMP:
        // jump on false
        if (!vTruthy(pop())) {
            jump = true;
            addr = ip + 3 + static_cast<i8>(code.readByte(ip+1));
        } else {
            ip += 2;
        }
        break;

    case OP_CALL:
        numArgs = code.readByte(ip+1);
        jump = true;
        addr = call(numArgs);
        break;

    case OP_APPLY:
        numArgs = code.readByte(ip+1);
        jump = true;
        addr = apply(numArgs);
        break;

    case OP_RETURN:
        // check that we are in a call frame
        if (frame->caller == nullptr) {
            throw FNError("interpreter",
                          "Return instruction at top level. ip = " + std::to_string((i32)ip),
                          *code.locationOf(ip));
        }
        // get return value
        v1 = pop();

        // jump to return address
        jump = true;
        addr = frame->retAddr;

        numArgs = frame->numArgs;
        frame->closeAll();
        tmp = frame;
        frame = tmp->prev;
        delete tmp;

        // pop the arguments + the caller
        popTimes(numArgs+1);
        push(v1);
        break;

    default:
        throw FNError("interpreter",
                      "Unrecognized opcode at address " + std::to_string((i32)ip),
                      *code.locationOf(ip));
        break;
    }
    ++ip;

    // skip or jump if needed
    if (skip) {
        ip += instrWidth(code.readByte(ip));
    }
    if (jump) {
        ip = addr;
    }
}

void VM::execute() {
    while (ip < code.getSize()) {
        step();
    }
}


}

