#include "vm.hpp"

#include "bytes.hpp"
#include "values.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>


namespace fn {

using namespace fn_bytes;

bytecode::bytecode()
    : locs(nullptr)
    , last_loc(nullptr)
    , symtab() {
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

u16 bytecode::add_function(const vector<symbol_id>& positional,
                           local_addr optional_index,
                           bool var_list,
                           bool var_table,
                           fn_namespace* ns) {
    functions.push_back(new func_stub {
            .positional=positional,
            .optional_index=optional_index,
            .var_list=var_list,
            .var_table=var_table,
            .num_upvals=0,
            .upvals=vector<upvalue>(),
            .ns=ns,
            .addr=get_size()});
    return (u16) (functions.size() - 1);
}


func_stub* bytecode::get_function(u16 id) const {
    // TODO: check bounds?
    return functions[id];
}

u16 bytecode::add_const(value v) {
    auto x = const_lookup.get(v);
    if (x.has_value()) {
        return **x;
    }
    constants.push_back(v);
    const_lookup.insert(v, constants.size()-1);
    return constants.size() - 1;
}

symbol_table* bytecode::get_symbol_table() {
    return &this->symtab;
}

const symbol_table* bytecode::get_symbol_table() const {
    return &this->symtab;
}

value bytecode::symbol(const string& name) {
    auto s = symtab.intern(name);
    return as_sym_value(s->id);
}
optional<value> bytecode::find_symbol(const string& name) const {
    auto s = symtab.find(name);
    if (s.has_value()) {
        return value{ .raw = (((u64) (*s)->id) << 4) | (u64) TAG_SYM };
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
        // TODO: probably an error
        return nullptr;
    }

    // check if an upvalue is already open for this stack position
    for (auto u : open_upvals) {
        if (u.pos == pos) {
            return u.slot;
        }
    }
    // TODO: ref_count
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
    : code{}
    , core_ns{nullptr}
    , alloc{[this] { return generate_roots(); }}
    , wd{fs::current_path().string()}
    , ip{0}
    , frame{new call_frame(nullptr, 0, 0, nullptr)}
    , lp(V_NULL) {
    alloc.disable_gc();
    ns_root = v_namespace(alloc.add_namespace());
    auto ns_id = alloc.add_cons(code.symbol("core"), V_EMPTY);
    ns_id = alloc.add_cons(code.symbol("fn"),ns_id);
    cur_ns = init_namespace(ns_id);
    core_ns = cur_ns;
    alloc.enable_gc();
}

virtual_machine::virtual_machine(const string& wd)
    : code{}
    , core_ns{nullptr}
    , alloc{[this] { return generate_roots(); }}
    , wd{wd}
    , ip{0}
    , frame{new call_frame(nullptr, 0, 0, nullptr)}
    , lp(V_NULL) {
    alloc.disable_gc();
    ns_root = v_namespace(alloc.add_namespace());
    auto ns_id = alloc.add_cons(code.symbol("core"), V_EMPTY);
    ns_id = alloc.add_cons(code.symbol("fn"),ns_id);
    cur_ns = init_namespace(ns_id);
    core_ns = cur_ns;
    alloc.enable_gc();
}

virtual_machine::~virtual_machine() {
    // delete all call frames
    while (frame != nullptr) {
        auto tmp = frame->prev;
        // TODO: ensure reference count for upvalue_slot is decremented
        delete frame;
        frame = tmp;
    }
}

void virtual_machine::set_wd(const string& new_wd) {
    wd = new_wd;
}

string virtual_machine::get_wd() {
    return wd;
}

void virtual_machine::compile_string(const string& src,
                                       const string& origin) {
    std::istringstream in{src};
    fn_scan::scanner sc{&in, origin};
    compiler c{this, &sc};
    c.compile_to_eof();
}

void virtual_machine::compile_file(const string& filename) {
    std::ifstream in{filename};
    if (!in.is_open()) {
        runtime_error("Could not open file: '" + filename + "'");
    }
    fn_scan::scanner sc{&in};
    compiler c{this, &sc};
    c.compile_to_eof();
}

void virtual_machine::interpret_string(const string& src,
                                       const string& origin) {
    std::istringstream in{src};
    fn_scan::scanner sc{&in, origin};
    compiler c{this, &sc};
    c.compile_to_eof();
    execute();
}

void virtual_machine::interpret_file(const string& filename) {
    std::ifstream in{filename};
    if (!in.is_open()) {
        runtime_error("Could not open file: '" + filename + "'");
    }
    fn_scan::scanner sc{&in};
    compiler c{this, &sc};

    while (!sc.eof_skip_ws()) {
        c.compile_expr();
        execute();
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
    // return stack_gen + upval_gen
    //     + generator<value>([i=0,this]() mutable -> optional<value> {
    //             if (i == 0) {
    //                 ++i;
    //                 return as_value(ns_root);
    //             }
    //             return { };
    //         });
    return stack_gen + upval_gen + generate1(as_value(ns_root))
        + generate1(lp) + generate1(as_value(cur_ns))
        + generate1(as_value(core_ns));
}

fn_namespace* virtual_machine::init_namespace(value ns_id) {
    if (v_tag(ns_id) != TAG_CONS) {
        runtime_error("namespace initialization failed: namespace id not a list of symbols.");
    }

    // disable the gc if necessary
    auto reenable_gc = false;
    if (alloc.gc_is_enabled()) {
        reenable_gc = true;
        alloc.disable_gc();
    }

    // create the namespace in the ns hierarchy
    auto x = ns_id;
    value key;
    fn_namespace* res = ns_root;
    optional<value> v;
    // FIXME: it's still possible to overwrite a variable here
    while (x != V_EMPTY) {
        key = v_head(x);
        if (v_tag(key) != TAG_SYM) {
            runtime_error("Namespace init failed on invalid namespace id.");
        }
        auto sym = v_sym_id(key);
        v = res->get(sym);
        if (!v.has_value()) {
            auto tmp = v_namespace(alloc.add_namespace());
            res->set(sym, as_value(tmp));
            res = tmp;
        } else if (v_tag(*v) == TAG_NAMESPACE) {
            res = v_namespace(*v);
        } else {
            runtime_error("Namespace init failed on collision with non-namespace definition.");
        }
        x = v_tail(x);
        // TODO: check tail is a list (and also do it in find_namespace)
    }

    // TODO: this only happens once in the constructor.
    if (core_ns != nullptr) {
        res->contents = table{core_ns->contents};
    }
    res->set(v_sym_id(code.symbol("ns")), as_value(ns_root));

    if (reenable_gc) {
        alloc.enable_gc();
    }
    return res;
}

// TODO: make optional<obj>
fn_namespace* virtual_machine::find_namespace(value ns_id) {
    if (v_tag(ns_id) != TAG_CONS) {
        runtime_error("namespace search failed: namespace id not a list of symbols.");
    }

    // create the namespace in the ns hierarchy
    auto x = ns_id;
    value key;
    fn_namespace* res = ns_root;
    optional<value> v;
    while (x != V_EMPTY) {
        // TODO: check that everything is a package or a namespace.
        key = v_head(x);
        if (v_tag(key) != TAG_SYM) {
            runtime_error("namespace search failed: namespace id not a list of symbols.");
        }
        v = res->get(v_sym_id(key));
        if (!v.has_value()) {
            return nullptr;
        } else if (v_tag(*v) == TAG_NAMESPACE) {
            res = v_namespace(*v);
        } else {
            runtime_error("namespace search failed: namespace id collides with a variable.");
        }
        x = v_tail(x);
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
    if (!name.is_sym()) {
        runtime_error("Global name is not a symbol.");
    }
    auto sym = v_sym_id(name);
    if (frame != nullptr && frame->caller != nullptr) {
        auto ns = frame->caller->stub->ns;
        ns->set(sym, v);
    } else {
        cur_ns->set(sym, v);
    }
}

value virtual_machine::get_global(value name) {
    if (!name.is_sym()) {
        runtime_error("Global name is not a symbol.");
    }
    auto sym = v_sym_id(name);

    optional<value> res;
    if (frame != nullptr && frame->caller != nullptr) {
        auto ns = frame->caller->stub->ns;
        res = ns->get(sym);
    } else {
        res = cur_ns->get(sym);
    }

    if (!res.has_value()) {
        runtime_error("Attempt to access unbound global variable " + v_to_string(name, code.get_symbol_table()));
    }
    return *res;
}

upvalue_slot virtual_machine::get_upvalue(u8 id) const {
    if (frame->caller == nullptr || frame->caller->stub->num_upvals <= id) {
        throw fn_error("interpreter", "Attempt to access nonexistent upvalue",
                      *code.location_of(ip));
        // TODO: error: nonexistent upvalue
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

bytecode& virtual_machine::get_bytecode() {
    return code;
}
allocator& virtual_machine::get_alloc() {
    return alloc;
}
symbol_table& virtual_machine::get_symtab() {
    return *code.get_symbol_table();
}

fn_namespace* virtual_machine::current_namespace() {
    return cur_ns;
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

    // TODO: use a maxargs constant
    if (vlen + num_args - 1 > 255) {
        runtime_error("too many arguments for function call in apply.");
    }
    return call(vlen + num_args - 1);
}

// TODO: switch to use runtime_error
bc_addr virtual_machine::call(local_addr num_args) {
    // the function to call should be at the bottom
    auto callee = peek(num_args + 1);
    auto kw = peek(num_args); // keyword arguments come first
    if (!kw.is_table()) {
        runtime_error("VM call operation has malformed keyword table.");
    }
    value res;
    auto tag = v_tag(callee);
    if (tag == TAG_FUNC) {
        // pause the garbage collector to allow stack manipulation
        alloc.disable_gc();
        auto func = v_func(callee);
        auto stub = func->stub;

        // usually, positional arguments can get left where they are

        // extra positional arguments go to the variadic list parameter
        value vlist = V_EMPTY;
        if (stub->positional.size() < num_args) {
            if (!stub->var_list) {
                runtime_error("Too many positional arguments to function.");
            }
            for (auto i = stub->positional.size(); i < num_args; ++i) {
                vlist = alloc.add_cons(peek(num_args - i), vlist);
            }
            // clear variadic arguments from the stack
            pop_times(num_args - stub->positional.size());
        }

        // positional arguments after num_args
        table<symbol_id,value> pos;
        // things we put in vtable
        table<symbol_id,bool> extra;
        value vtable = stub->var_table ? alloc.add_table() : V_NULL;
        auto& cts = kw.utable()->contents;
        for (auto k : cts.keys()) {
            auto id = v_sym_id(*k);
            bool found = false;
            for (u32 i = 0; i < stub->positional.size(); ++i) {
                if (stub->positional[i] == id) {
                    if (pos.get(id).has_value() || i < num_args) {
                        if (!stub->var_table) {
                            runtime_error("Extra keyword argument.");
                        } else {
                            extra.insert(id, true);
                        }
                    } else {
                        found = true;
                        pos.insert(id,**cts.get(*k));
                    }
                    break;
                }
            }
            if (!found) {
                if (!stub->var_table) {
                    runtime_error("Extraneous keyword arguments.");
                }
                vtable.table_set(*k, **cts.get(*k));
            }
        }

        // finish placing positional parameters on the stack
        for (u32 i = num_args; i < stub->positional.size(); ++i) {
            auto v = pos.get(stub->positional[i]);
            if (v.has_value()) {
                push(**v);
            } else if (i >= stub->optional_index) {
                push(callee.ufunc()->init_vals[i-stub->optional_index]);
            } else {
                runtime_error("Missing parameter with no default.");
            }
        }

        // push variadic list and table parameters 
        if (stub->var_list) {
            push(vlist);
        }
        if (stub->var_table) {
            push(vtable);
        }

        // extend the call frame and move on
        frame = frame->
            extend_frame(ip+2,
                         stub->positional.size() + stub->var_list + stub->var_table,
                         func);
        alloc.enable_gc();
        return stub->addr;
    } else if (tag == TAG_FOREIGN) {
        if (kw.utable()->contents.get_size() != 0) {
            runtime_error("Foreign function was passed keyword arguments.");
        }
        alloc.disable_gc();
        auto f = v_foreign(callee);
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
        // pop args+2 times (to get the function and keyword dictionary)
        pop_times(num_args+2);
        push(res);
        alloc.enable_gc();
        return ip + 2;
    } else if (tag == TAG_TABLE) {
        auto s = get_symtab().intern("__on-call__");
        auto v = callee.table_get(as_value(*s));

        // make space to insert the table on the stack
        push(V_NULL);
        auto sp = frame->bp + frame->sp;
        for (u32 i = 0; i < num_args; ++i) {
            stack[sp - i - 1] = stack[sp - i];
        }
        stack[sp - num_args - 1] = callee;
        stack[sp - num_args - 3] = v;
        return call(num_args + 1);
    } else {
        // TODO: exception: not a function
        throw fn_error("interpreter",
                      "attempt to call nonfunction at address " + std::to_string((i32)ip),
                      *code.location_of(ip));
        return ip + 2;
    }
}


#define push_bool(b) push(b ? V_TRUE : V_FALSE);
void virtual_machine::step() {

    u8 instr = code.read_byte(ip);

    // variable for use inside the switch
    value v1, v2, v3;
    optional<value> vp;

    bool skip = false;
    bool jump = false;
    bc_addr addr = 0;

    fn_namespace* mod;

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
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        push(**u.val);
        ++ip;
        break;
    case OP_SET_UPVALUE:
        l = code.read_byte(ip+1);
        // TODO: check upvalue exists
        u = frame->caller->upvals[l];
        **u.val = pop();
        ++ip;
        break;

    case OP_CLOSURE:
        id = code.read_short(ip+1);
        stub = code.get_function(code.read_short(ip+1));
        push(alloc
             .add_func(stub,
                       [this, stub](auto upvals, auto init_vals) {
                           for (u32 i = 0; i < stub->num_upvals; ++i) {
                               auto u = stub->upvals[i];
                               if (u.direct) {
                                   upvals[i] = frame
                                       ->create_upvalue(u.slot,
                                                        &stack[frame->bp+u.slot]);
                               } else {
                                   upvals[i] = get_upvalue(u.slot);
                               }
                           }
                           auto num_opt = stub->positional.size()
                               - stub->optional_index;
                           for (i32 i = num_opt-1; i >= 0; --i) {
                               init_vals[i] = pop();
                           }
                       }));
        ip += 2;
        break;

    case OP_CLOSE:
        num_args = code.read_byte(ip+1);
        frame->close(num_args);
        // TODO: check stack size >= num_args
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
        if (v_tag(v2) == TAG_TABLE) {
            vp = v2.table_get(v1);
            push(vp.has_value() ? *vp : V_NULL);
            break;
        } else if (v_tag(v2) == TAG_NAMESPACE) {
            if (v_tag(v1) == TAG_SYM) {
                vp = v2.namespace_get(v_sym_id(v1));
                if (!vp.has_value()) {
                    runtime_error("obj-get undefined key for namespace");
                }
                push(*vp);
                break;
            }
            runtime_error("obj-get namespace key must be a symbol");
        }
        runtime_error("obj-get operand not a table or namespace");
        break;

    case OP_OBJ_SET:
        // new-value
        v3 = pop();
        // key
        v1 = pop();
        // object
        v2 = pop();
        if (v_tag(v2) != TAG_TABLE) {
            runtime_error("obj-set operand not a table");
        }
        v_table(v2)->contents.insert(v1,v3);
        break;

    case OP_NAMESPACE:
        v1 = pop();

        if (v_tag(v1) != TAG_NAMESPACE) {
            runtime_error("namespace operand not a namespace");
        }
        cur_ns = v_namespace(v1);
        break;

    case OP_IMPORT:
        v1 = pop();
        mod = find_namespace(v1);
        if (mod == nullptr) {
            mod = init_namespace(v1);
        }
        push(as_value(mod));
        break;

    case OP_JUMP:
        jump = true;
        addr = ip + 3 + static_cast<i16>(code.read_short(ip+1));
        break;

    case OP_CJUMP:
        // jump on false
        if (!v_truthy(pop())) {
            jump = true;
            addr = ip + 3 + static_cast<i16>(code.read_short(ip+1));
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
        // TODO: restore stack pointer
        frame = tmp->prev;
        delete tmp;

        // pop the arguments + the caller + the keyword table
        pop_times(num_args+2);
        push(v1);
        break;

    case OP_TABLE:
        push(alloc.add_table());
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
    // TODO: annotate with line number
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

