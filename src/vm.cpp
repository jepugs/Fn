#include "vm.hpp"

#include "bytes.hpp"
#include "values.hpp"

#include <cstdlib>
#include <iostream>
#include <numeric>


namespace fn {

using namespace fn_bytes;

symbol_table::symbol_table()
    : by_name()
    , by_id()
{ }

const symbol* symbol_table::intern(const string& str) {
    auto v = find(str);
    if (v.has_value()) {
        return *v;
    } else {
        u32 id = by_id.size();
        symbol s = { .id=id, .name=str };
        by_id.push_back(s);
        by_name.insert(str,s);
        return &(by_id[by_id.size() - 1]);
    }
}

bool symbol_table::is_internal(const string& str) const {
    return by_name.get(str).has_value();
}

inline optional<const symbol*> symbol_table::find(const string& str) const {
    auto v = by_name.get(str);
    if (v.has_value()) {
        return *v;
    }
    return { };
}

u8 func_stub::get_upvalue(local_addr slot, bool direct) {
    for (local_addr i = 0; i < num_upvals; ++i) {
        auto u = upvals[i];
        if (u.slot == slot && u.direct == direct) {
            // found the upvalue
            return i;
        }
    }
    // add a new upvalue
    upvals.push_back({ .slot=slot, .direct=direct });

    return num_upvals++;
}

bytecode::bytecode()
    : locs(nullptr)
    , last_loc(nullptr)
    , symbols()
{
    // allocate 256 bytes to start
    cap = 256;
    // use malloc so we can use realloc
    data = (u8*)malloc(256);

    size = 0;

    //set_loc(source_loc(std::shared_ptr<string>(new string("")), 0, 0));
    set_loc(source_loc(std::shared_ptr<string>(new string("")), 0, 0));
}

bytecode::~bytecode() {
    free(data);

    for (auto v : managed_constants) {
        if (v.is_str()) {
            delete v.ustr();
        } else if (v.is_cons()) {
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

void bytecode::ensure_capacity(u32 new_cap) {
    if (new_cap <= cap) {
        return;
    }
    while (cap < new_cap) {
        cap *= 2;
    }
    data = (u8*)realloc(data, cap);
}

u32 bytecode::get_size() const {
    return size;
}

void bytecode::set_loc(source_loc l) {
    auto prev = last_loc;
    last_loc = new bytecode_loc{ .max_addr=0, .loc=l, .next=nullptr };
    if (prev == nullptr) {
        locs = last_loc;
    } else {
        prev->max_addr = size;
        prev->next = last_loc;
    }
}

source_loc* bytecode::location_of(bc_addr addr) const {
    if(locs == nullptr)
        return nullptr;

    auto l = locs;
    // note: max_addr of 0 indicates that this was the last location set and so it doesn't have an
    // upper limit yet.
    while (l->max_addr <= addr && l->max_addr != 0) {
        l = l->next;
    }
    return &l->loc;
}

void bytecode::write_byte(u8 b) {
    ensure_capacity(size + 1);
    data[size] = b;
    ++size;
}

void bytecode::write_bytes(const u8* bytes, bc_addr len) {
    ensure_capacity(size + len);
    for (u32 i = 0; i < len; ++i) {
        data[size+i] = bytes[i];
    }
    size += len;
}

void bytecode::write_short(u16 s) {
    u8 bot = (u8) (s & 0x00ff);
    u8 top = (u8) (s >> 8);
    // write in little-endian order
    write_byte(bot);
    write_byte(top);
}

u8 bytecode::read_byte(bc_addr addr) const {
    return data[addr];
}

u16 bytecode::read_short(bc_addr addr) const {
    u16 bot = (u16) read_byte(addr);
    u16 top = (u16) read_byte(addr + 1);
    return (top << 8) | bot;
}

void bytecode::patch_short(bc_addr addr, u16 s) {
    u8 bot = (u8) (s & 0x00ff);
    u8 top = (u8) (s >> 8);
    data[addr] = bot;
    data[addr+1] = top;
}

value bytecode::get_constant(u16 id) const {
    return constants[id];
}

u16 bytecode::num_constants() const {
    return constants.size();
}

u16 bytecode::add_function(local_addr arity, bool vararg, value mod_id) {
    arity -= vararg ? 1 : 0;
    functions.push_back(new func_stub {
            .positional=arity, 
            .required=arity,
            .varargs=vararg,
            .num_upvals=0,
            .upvals=vector<upvalue>(),
            .mod_id=mod_id,
            .addr=get_size()});
    return (u16) functions.size() - 1;
}

func_stub* bytecode::get_function(u16 id) const {
    // t_od_o: check bounds?
    return functions[id];
}

u16 bytecode::add_const(value v) {
    constants.push_back(v);
    return constants.size() - 1;
}

u16 bytecode::num_const(f64 num) {
    return add_const(as_value(num));
}

u16 bytecode::str_const(const string& name) {
    auto v = as_value(new fn_string(name));
    managed_constants.push_front(v);
    return add_const(v);
}

u16 bytecode::str_const(const char* name) {
    auto v = as_value(new fn_string(name));
    managed_constants.push_front(v);
    return add_const(v);
}

u16 bytecode::str_const(const fn_string& str) {
    auto v = as_value(new fn_string{str});
    managed_constants.push_front(v);
    return add_const(v);
}


u16 bytecode::cons_const(value hd, value tl) {
    auto v = as_value(new cons(hd, tl));
    managed_constants.push_front(v);
    return add_const(v);
}

const_id bytecode::sym_const(u32 sym) {
    auto s = symbols[sym];
    value v{ .raw = (((u64) s.id) << 8) | (u64) TAG_SYM };
    return add_const(v);
}

const_id bytecode::sym_const(const string& name) {
    return add_const(symbol(name));
}

symbol_table* bytecode::get_symbols() {
    return &this->symbols;
}

const symbol_table* bytecode::get_symbols() const {
    return &this->symbols;
}

u32 bytecode::symbol_id(const string& name) {
    auto s = symbols.intern(name);
    return s->id;
}
value bytecode::symbol(const string& name) {
    auto s = symbols.intern(name);
    return { .raw = (((u64) s->id) << 8) | (u64) TAG_SYM };
}
optional<value> bytecode::find_symbol(const string& name) const {
    auto s = symbols.find(name);
    if (s.has_value()) {
        return value{ .raw = (((u64) (*s)->id) << 8) | (u64) TAG_SYM };
    } else {
        return { };
    }
}


call_frame* call_frame::extend_frame(bc_addr ret_addr,
                                     local_addr num_args,
                                     function* caller) {
    return new call_frame(this, ret_addr, bp+sp-num_args, caller, num_args);
}

upvalue_slot call_frame::create_upvalue(local_addr pos, value* ptr) {
    if (pos >= sp) {
        // t_od_o: probably an error
        return nullptr;
    }

    // check if an upvalue is already open for this stack position
    for (auto u : open_upvals) {
        if (u.pos == pos) {
            return u.slot;
        }
    }
    // t_od_o: ref_count
    auto res = upvalue_slot(ptr);
    open_upvals.push_front(open_upvalue{ .slot=res, .pos=pos });
    return res;
}

void call_frame::close(stack_addr n) {
    sp -= n;
    open_upvals.remove_if([this](auto u) {
        if (u.pos >= sp) {
            u.slot.close();
            return true;
        }
        return false;
    });
}

void call_frame::close_all() {
    sp = 0;
    for (auto u : open_upvals) {
        *u.slot.open = false;
        auto v = **u.slot.val;
        *u.slot.val = new value();
        **u.slot.val = v;
    }

    open_upvals.clear();
}


virtual_machine::virtual_machine()
    : code()
    , core_mod(nullptr)
    , alloc([this] { return generate_roots(); })
    , ip(0)
    , frame(new call_frame(nullptr, 0, 0, nullptr))
    , lp(V_NULL)
{
    ns = v_obj(alloc.add_obj());
    auto mod_id = alloc.add_cons(code.symbol("core"), V_EMPTY);
    mod_id = alloc.add_cons(code.symbol("fn"),mod_id);
    module = init_module(mod_id);
    core_mod = module;
    alloc.enable_gc();
}

virtual_machine::~virtual_machine() {
    // delete all call frames
    while (frame != nullptr) {
        auto tmp = frame->prev;
        // t_od_o: ensure reference count for upvalue_slot is decremented
        delete frame;
        frame = tmp;
    }
}

generator<value> virtual_machine::generate_roots() {
    generator<value> stack_gen([i=0,this]() mutable -> optional<value> {
        auto m = frame->sp + frame->bp;
        if (i >= m) {
            return {};
        }
        return stack[i++];
    });
    generator<value> upval_gen;
    auto f = frame;
    do {
        if (f->caller == nullptr) continue;
        auto n = f->caller->stub->num_upvals;
        auto u = f->caller->upvals;
        upval_gen += generator<value>([n,u,i=0]() mutable -> optional<value> {
                if (i >= n) {
                    return { };
                }
                return **u[i++].val;
        });
    } while ((f = f->prev) != nullptr);
    return stack_gen + upval_gen + generate1(as_value(ns)) + generate1(lp);
}

object* virtual_machine::init_module(value mod_id) {
    if (v_tag(mod_id) != TAG_CONS) {
        runtime_error("module initialization failed: module id not a list of symbols.");
    }

    // disable the gc if necessary
    auto reenable_gc = false;
    if (alloc.gc_is_enabled()) {
        reenable_gc = true;
        alloc.disable_gc();
    }

    // create the module in the ns hierarchy
    auto x = mod_id;
    value key;
    object* res = ns;
    optional<value*> v;
    // f_ix_me: it's still possible to overwrite a variable here
    while (x != V_EMPTY) {
        key = v_head(x);
        if (v_tag(key) != TAG_SYM) {
            runtime_error("module initialization failed: module id not a list of symbols.");
        }
        v = res->contents.get(key);
        if (!v.has_value()) {
            auto tmp = v_obj(alloc.add_obj());
            res->contents.insert(key, as_value(tmp));
            res = tmp;
        } else if (v_short_tag(**v) == TAG_OBJ) {
            res = v_obj(**v);
        } else {
            runtime_error("module initialization would clobber existing variable.");
        }
        x = v_tail(x);
        // t_od_o: check tail is a list (and also do it in find_module)
    }

    // check if this is already a module
    if (res->contents.get(code.symbol("_modinfo"))) {
        runtime_error("module initialization would clobber existing module.");
    }

    // t_od_o: this only happens once in the constructor.
    if (core_mod != nullptr) {
        res->contents = table(core_mod->contents);
    }
    auto cts = &res->contents;
    cts->insert(code.symbol("ns"), as_value(ns));

    // make a global _modinfo object
    object* modinfo = v_obj(alloc.add_obj());
    cts->insert(code.symbol("_modinfo"), as_value(modinfo));
    // name is the last symbol in the module id
    modinfo->contents.insert(code.symbol("name"), key);
    modinfo->contents.insert(code.symbol("id"), mod_id);
    // the file from which this was loaded. the compiler generates code to set this when importing a file.
    modinfo->contents.insert(code.symbol("source"), alloc.add_str("<internal>"));

    if (reenable_gc) {
        alloc.enable_gc();
    }
    return res;
}

// t_od_o: make optional<obj>
object* virtual_machine::find_module(value mod_id) {
    if (v_tag(mod_id) != TAG_CONS) {
        runtime_error("module search failed: module id not a list of symbols.");
    }

    // create the module in the ns hierarchy
    auto x = mod_id;
    value key;
    object* res = ns;
    optional<value*> v;
    while (x != V_EMPTY) {
        // t_od_o: check that everything is a package or a module.
        key = v_head(x);
        if (v_tag(key) != TAG_SYM) {
            runtime_error("module search failed: module id not a list of symbols.");
        }
        v = res->contents.get(key);
        if (!v.has_value()) {
            return nullptr;
        } else if (v_short_tag(**v) == TAG_OBJ) {
            res = v_obj(**v);
        }
        x = v_tail(x);
    }

    // ensure this is already a module
    if (res->contents.get(code.symbol("_modinfo")) == nullptr) {
        runtime_error("module search failed: module id names a variable.");
    }
    return res;
}

u32 virtual_machine::get_ip() const {
    return ip;
}

value virtual_machine::last_pop() const {
    return lp;
}

void virtual_machine::add_global(value name, value v) {
    if (frame != nullptr && frame->caller != nullptr) {
        auto mod_id = frame->caller->stub->mod_id;
        auto mod = find_module(mod_id);
        if (mod == nullptr) {
            runtime_error("function has nonsensical module i_d. (this is probably a bug).");
        } else {
            mod->contents.insert(name, v);
        }
    } else {
        module->contents.insert(name, v);
    }
}

value virtual_machine::get_global(value name) {
    optional<value*> res;
    if (frame != nullptr && frame->caller != nullptr) {
        auto mod_id = frame->caller->stub->mod_id;
        auto mod = find_module(mod_id);
        if (mod == nullptr) {
            runtime_error("function has nonsensical module i_d. (this is probably a bug).");
        } else {
            res = mod->contents.get(name);
        }
    } else {
        res = module->contents.get(name);
    }

    if (!res.has_value()) {
        runtime_error("attempt to access unbound global variable " + v_to_string(name, code.get_symbols()));
    }
    return **res;
}

upvalue_slot virtual_machine::get_upvalue(u8 id) const {
    if (frame->caller == nullptr || frame->caller->stub->num_upvals <= id) {
        throw fn_error("interpreter", "attempt to access nonexistent upvalue",
                      *code.location_of(ip));
        // t_od_o: error: nonexistent upvalue
    }
    return frame->caller->upvals[id];
}


void virtual_machine::add_foreign(string name,
                                  value (*func)(local_addr, value*, virtual_machine*),
                                  local_addr min_args,
                                  bool var_args) {
    auto v = alloc.add_foreign(min_args, var_args, func);
    add_global(code.symbol(name), v);
    foreign_funcs.push_back(v);
}

bytecode* virtual_machine::get_bytecode() {
    return &code;
}
allocator* virtual_machine::get_alloc() {
    return &alloc;
}

void virtual_machine::runtime_error(const string& msg) const {
    throw fn_error("runtime", "(ip = " + std::to_string(ip) + ") " + msg, *code.location_of(ip));
}

void virtual_machine::push(value v) {
    if (frame->sp + frame->bp >= STACK_SIZE - 1) {
        throw fn_error("runtime", "stack exhausted.", *code.location_of(ip));
    }
    stack[frame->bp + frame->sp++] = v;
}

value virtual_machine::pop() {
    if (frame->sp == 0) {
        throw fn_error("runtime",
                      "pop on empty call frame at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
    }
    return stack[frame->bp + --frame->sp];
}

value virtual_machine::pop_times(stack_addr n) {
    if (frame->sp < n) {
        throw fn_error("runtime",
                      "pop on empty call frame at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
    }
    frame->sp -= n;
    return stack[frame->bp + frame->sp];
}

value virtual_machine::peek(stack_addr i) const {
    if (frame->sp <= i) {
        throw fn_error("runtime",
                      "peek out of stack bounds at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
    }
    return stack[frame->bp + frame->sp - i - 1];
}

value virtual_machine::local(local_addr i) const {
    stack_addr pos = i + frame->bp;
    if (frame->sp <= i) {
        throw fn_error("runtime", "out of stack bounds on local.", *code.location_of(ip));
    }
    return stack[pos];
}

void virtual_machine::set_local(local_addr i, value v) {
    stack_addr pos = i + frame->bp;
    if (frame->sp <= i) {
        runtime_error("out of stack bounds on set-local.");
    }
    stack[pos] = v;
}

bc_addr virtual_machine::apply(local_addr num_args) {
    // argument to expand
    auto v = pop();
    auto tag = v_tag(v);
    if (tag != TAG_EMPTY && tag != TAG_CONS) {
        runtime_error("last argument to apply not a list.");
    }

    auto vlen = 0;
    while (v_tag(v) != TAG_EMPTY) {
        push(v_cons(v)->head);
        v = v_cons(v)->tail;
        ++vlen;
    }

    // t_od_o: use a maxargs constant
    if (vlen + num_args - 1 > 255) {
        runtime_error("too many arguments for function call in apply.");
    }
    return call(vlen + num_args - 1);
}

// t_od_o: switch to use runtime_error
bc_addr virtual_machine::call(local_addr num_args) {
    // the function to call should be at the bottom
    auto v1 = peek(num_args);
    value res;
    auto tag = v_tag(v1);
    if (tag == TAG_FUNC) {
        // pause the garbage collector to allow stack manipulation
        alloc.disable_gc();
        auto func = v_func(v1);
        auto stub = func->stub;
        auto mod = find_module(stub->mod_id);
        if (mod == nullptr) {
            runtime_error("function has nonexistent module id.");
        }
        // t_od_o: switch to the function's module. probably put in the callframe
        if (num_args < stub->required) {
            throw fn_error("interpreter",
                          "too few arguments for function call at ip=" + std::to_string(ip),
                          *code.location_of(ip));
        } else if (stub->varargs) {
            // make a list out of the trailing arguments
            auto vararg = V_EMPTY;
            for (auto i = num_args-stub->positional; i > 0; --i) {
                vararg = alloc.add_cons(pop(), vararg);
            }
            push(vararg);
            frame = frame->extend_frame(ip+2, stub->positional+1, func);
        } else if (num_args > stub->positional) {
            throw fn_error("interpreter",
                          "too many arguments for function call at ip=" + std::to_string(ip),
                          *code.location_of(ip));
        } else {
            // normal function call
            frame = frame->extend_frame(ip+2, num_args, func);
        }
        // t_od_o: maybe put this in finally?
        alloc.enable_gc();
        return stub->addr;
    } else if (tag == TAG_FOREIGN) {
        alloc.disable_gc();
        auto f = v_foreign(v1);
        if (num_args < f->min_args) {
            throw fn_error("interpreter",
                          "too few arguments for foreign function call at ip=" + std::to_string(ip),
                          *code.location_of(ip));
        } else if (!f->var_args && num_args > f->min_args) {
            throw fn_error("interpreter",
                          "too many arguments for foreign function call at ip=" + std::to_string(ip),
                          *code.location_of(ip));
        } else {
            // correct arity
            res = f->func((u16)num_args, &stack[frame->bp + frame->sp - num_args], this);
        }
        // pop args+1 times (to get the function)
        pop_times(num_args+1);
        push(res);
        alloc.enable_gc();
        return ip + 2;
    } else {
        // t_od_o: exception: not a function
        throw fn_error("interpreter",
                      "attempt to call nonfunction at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
        return ip + 2;
    }
}


#define push_bool(b) push(b ? v_t_ru_e : v_f_al_se);
void virtual_machine::step() {

    u8 instr = code.read_byte(ip);

    // variable for use inside the switch
    value v1, v2, v3;
    optional<value*> vp;

    bool skip = false;
    bool jump = false;
    bc_addr addr = 0;

    object* mod;

    local_addr num_args;

    func_stub* stub;

    local_addr l;
    u16 id;

    call_frame *tmp;

    upvalue_slot u;

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
        v1 = peek(code.read_byte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_LOCAL:
        v1 = local(code.read_byte(ip+1));
        push(v1);
        ++ip;
        break;
    case OP_SET_LOCAL:
        v1 = pop();
        set_local(code.read_byte(ip+1), v1);
        ++ip;
        break;

    case OP_UPVALUE:
        l = code.read_byte(ip+1);
        // t_od_o: check upvalue exists
        u = frame->caller->upvals[l];
        push(**u.val);
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = code.read_byte(ip+1);
        // t_od_o: check upvalue exists
        u = frame->caller->upvals[l];
        **u.val = pop();
        ++ip;
        break;

    case OP_CLOSURE:
        id = code.read_short(ip+1);
        stub = code.get_function(code.read_short(ip+1));
        push(alloc.add_func(stub,
                            [this, stub](auto upvals) {
                                for (u32 i = 0; i < stub->num_upvals; ++i) {
                                    auto u = stub->upvals[i];
                                    if (u.direct) {
                                        upvals[i] = frame->create_upvalue(u.slot, &stack[frame->bp + u.slot]);
                                    } else {
                                        upvals[i] = get_upvalue(u.slot);
                                    }
                                }
                                
                            }));
        ip += 2;
        break;

    case OP_CLOSE:
        num_args = code.read_byte(ip+1);
        frame->close(num_args);
        // t_od_o: check stack size >= num_args
        ++ip;
        break;

    case OP_GLOBAL:
        v1 = pop();
        if (v_tag(v1) != TAG_SYM) {
            runtime_error("OP_GLOBAL name operand is not a symbol.");
        }
        push(get_global(v1));
        break;
    case OP_SET_GLOBAL:
        v1 = pop(); // value
        v2 = peek(); // name
        if (v_tag(v2) != TAG_SYM) {
            runtime_error("OP_SET_GLOBAL name operand is not a symbol.");
        }
        add_global(v2, v1);
        break;

    case OP_CONST:
        id = code.read_short(ip+1);
        if (id >= code.num_constants()) {
            throw fn_error("runtime", "attempt to access nonexistent constant.",
                          *code.location_of(ip));
        }
        push(code.get_constant(id));
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
        if (v_tag(v2) != TAG_OBJ) {
            runtime_error("obj-get operand not a general object");
        }
        vp = v_obj(v2)->contents.get(v1);
        push(vp.has_value() ? **vp : V_NULL);
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = pop();
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (v_tag(v2) != TAG_OBJ) {
            runtime_error("obj-set operand not a general object");
        }
        v_obj(v2)->contents.insert(v1,v3);
        push(v3);
        break;

    case OP_MODULE:
        v1 = pop();
        // t_od_o: check for _modinfo property
        if (v_tag(v1) != TAG_OBJ) {
            runtime_error("module operand not a general object");
        }
        module = v_obj(v1);
        break;

    case OP_IMPORT:
        v1 = pop();
        mod = find_module(v1);
        if (mod == nullptr) {
            mod = init_module(v1);
        }
        push(as_value(mod));
        break;

    case OP_JUMP:
        jump = true;
        addr = ip + 3 + static_cast<i8>(code.read_byte(ip+1));
        break;

    case OP_CJUMP:
        // jump on false
        if (!v_truthy(pop())) {
            jump = true;
            addr = ip + 3 + static_cast<i8>(code.read_byte(ip+1));
        } else {
            ip += 2;
        }
        break;

    case OP_CALL:
        num_args = code.read_byte(ip+1);
        jump = true;
        addr = call(num_args);
        break;

    case OP_APPLY:
        num_args = code.read_byte(ip+1);
        jump = true;
        addr = apply(num_args);
        break;

    case OP_RETURN:
        // check that we are in a call frame
        if (frame->caller == nullptr) {
            throw fn_error("interpreter",
                          "return instruction at top level. ip = " + std::to_string((i32)ip),
                          *code.location_of(ip));
        }
        // get return value
        v1 = pop();

        // jump to return address
        jump = true;
        addr = frame->ret_addr;

        num_args = frame->num_args;
        frame->close_all();
        tmp = frame;
        // t_od_o: restore stack pointer
        frame = tmp->prev;
        delete tmp;

        // pop the arguments + the caller
        pop_times(num_args+1);
        push(v1);
        break;

    default:
        throw fn_error("interpreter",
                      "unrecognized opcode at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
        break;
    }
    ++ip;

    // skip or jump if needed
    if (skip) {
        ip += instr_width(code.read_byte(ip));
    }
    if (jump) {
        ip = addr;
    }
}

void virtual_machine::execute() {
    while (ip < code.get_size()) {
        step();
    }
}


}

