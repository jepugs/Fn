#include "vm.hpp"

#include "config.h"

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
                           bool var_table) {
    functions.push_back(new func_stub {
            .positional=positional,
            .optional_index=optional_index,
            .var_list=var_list,
            .var_table=var_table,
            .num_upvals=0,
            .upvals=vector<upvalue>(),
            .addr=get_size()});
    return (u16) (functions.size() - 1);
}

void bytecode::define_bytecode_function(const string& name,
                                        const vector<symbol_id>& positional,
                                        local_addr optional_index,
                                        bool var_list,
                                        bool var_table,
                                        fn_namespace* ns,
                                        vector<u8>& bytes) {
    write_byte(OP_CONST);
    write_short(add_const(as_value(symtab.intern(name))));

    // TODO: wrap in appropriate code to bind to a variable
    auto func_id = add_function(positional,
                                optional_index,
                                var_list,
                                var_table,
                                ns);
    for (auto x : bytes) {
        write_byte(x);
    }
    write_byte(OP_CLOSURE);
    write_short(func_id);

    write_byte(OP_SET_GLOBAL);
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

const_id bytecode::add_sym_const(symbol_id s) {
    return add_const(as_sym_value(s));
}


symbol_table* bytecode::get_symbol_table() {
    return &this->symtab;
}

const symbol_table* bytecode::get_symbol_table() const {
    return &this->symtab;
}

value bytecode::get_symbol(const string& name) {
    return as_sym_value(get_symbol_id(name));
}
symbol_id bytecode::get_symbol_id(const string& name) {
    return symtab.intern(name)->id;
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


virtual_machine::virtual_machine(allocator* use_alloc, code_chunk* init_chunk)
    : code{init_chunk}
    , core_ns{nullptr}
    , alloc{use_alloc}
    , wd{fs::current_path().string()}
    , ip{0}
    , frame{new call_frame(nullptr, 0, 0, nullptr)}
    , lp{V_NULL} {

    alloc->disable_gc();
    alloc->add_root_generator([this] { return get_roots(); });

    ns_root = alloc.add_namespace().unamespace();

    // TODO: create chunk namespace in namespace hierarchy and use as
    // current/core namespace

    auto ns_id = alloc.add_cons(get_symbol("core"), V_EMPTY);
    ns_id = alloc.add_cons(get_symbol("fn"), ns_id);
    core_ns = create_ns(ns_id);

    cur_ns = core_ns;

    alloc.enable_gc();
}

// virtual_machine::virtual_machine(const string& wd)
//     : code{}
//     , core_ns{nullptr}
//     , alloc{[this] { return get_roots(); }}
//     , wd{wd}
//     , ip{0}
//     , frame{new call_frame(nullptr, 0, 0, nullptr)}
//     , lp{V_NULL} {

//     alloc.disable_gc();

//     ns_root = alloc.add_namespace().unamespace();

//     auto ns_id = alloc.add_cons(get_symbol("core"), V_EMPTY);
//     ns_id = alloc.add_cons(get_symbol("fn"), ns_id);
//     core_ns = create_ns(ns_id);

//     cur_ns = core_ns;

//     alloc.enable_gc();
// }

virtual_machine::~virtual_machine() {
    // delete all call frames
    while (frame != nullptr) {
        auto tmp = frame->prev;
        // TODO: ensure reference count for upvalue_slot is decremented
        delete frame;
        frame = tmp;
    }
}

void virtual_machine::do_apply(local_addr num_args) {
    ip = apply(num_args);
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

    auto i = 0;
    while (!sc.eof_skip_ws()) {
        ++i;
        c.compile_expr();
        execute();
    }
}

fn_namespace* virtual_machine::find_ns(value id) {
    // FIXME: should check for cons
    auto hd = v_head(this, id);
    auto tl = v_tail(this, id);
    auto ns = ns_root;
    while (true) {
        auto x = ns->contents.get(v_sym_id(this,hd));
        if (x.has_value()) {
            if (!(**x).is_namespace()) {
                runtime_error("Error: namespace id overlaps with non-namespace definition.");
            }
            ns = (**x).unamespace();
        } else {
            return nullptr;
        }

        if(!tl.is_cons()) {
            break;
        }

        hd = v_uhead(tl);
        tl = v_utail(tl);
    }

    return ns;
}

optional<string> virtual_machine::find_ns_file(value id) {
    fs::path rel_path{};
    // FIXME: should check that these are all symbols
    auto hd = v_head(this, id);
    auto tl = v_tail(this, id);
    while (tl.is_cons()) {
        rel_path /= get_symtab()[v_usym_id(hd)].name;
        hd = v_uhead(tl);
        tl = v_utail(tl);
    }
    rel_path /= get_symtab()[v_usym_id(hd)].name + ".fn";
    
    fs::path p{wd / rel_path};
    if (fs::exists(p)) {
        return p.string();
    }

    // TODO: set these properly

    // s = "${HOME}" + "/.local/lib/fn/ns/" + rel_path;
    // if (fs::exists(s)) {
    //     return s;
    // }

    // add the prefix
    p = fs::path{PREFIX} / "lib" / "fn" / "ns" / rel_path;
    if (fs::exists(p)) {
        return p.string();
    }

    return std::nullopt;
}

fn_namespace* virtual_machine::create_empty_ns(value id) {
    if (!id.is_cons()) {
        runtime_error("(Internal). Namespace id not a list of symbols.");
    }
    auto hd = v_uhead(id);
    auto tl = v_utail(id);
    auto ns = ns_root;
    while (true) {
        if (!tl.is_cons() && !tl.is_empty()) {
            runtime_error("(Internal). Namespace id not a list of symbols.");
            break;
        }
        if (!hd.is_symbol()) {
            runtime_error("Namespace id must be a list of symbols.");
            break;
        }

        auto x = ns->contents.get(v_usym_id(hd));
        if (x.has_value()) {
            if (!(**x).is_namespace()) {
                runtime_error("Error: namespace id overlaps with non-namespace definition.");
            }
            ns = (**x).unamespace();
        } else {
            auto next = alloc.add_namespace().unamespace();
            v_set(this, as_value(ns), hd, as_value(next));
            ns = next;
        }

        if(tl.is_empty()) {
            break;
        }

        hd = v_head(this, tl);
        tl = v_tail(this, tl);
    }
    return ns;
}

fn_namespace* virtual_machine::create_ns(value id) {
    auto res = create_empty_ns(id);

    auto sym = as_sym_value(get_symtab().intern("ns")->id);
    v_set(this, as_value(res), sym, as_value(ns_root));

    return res;
}

fn_namespace* virtual_machine::create_ns(value id,
                                                fn_namespace* templ) {
    auto res = create_empty_ns(id);

    res->contents = table{templ->contents};

    auto sym = as_sym_value(get_symtab().intern("ns")->id);
    v_uns_set(as_value(res), sym, as_value(ns_root));

    return res;
}

fn_namespace* virtual_machine::load_ns(value id, const string& filename) {
    // save vm parameters
    auto tmp_ns = cur_ns;
    auto tmp_ip = ip;
    value tmp_stack[STACK_SIZE];
    memcpy(tmp_stack, stack, STACK_SIZE*sizeof(value));
    auto tmp_frame = frame;

    // set up a fresh environment
    cur_ns = create_ns(id, core_ns);
    ip = code.get_size();
    frame = new call_frame(nullptr, 0, 0, nullptr);

    // FIXME: we should probably add a long jump operator for this sort of
    // thing.

    // write some code to jump over the intepreted file
    code.write_byte(OP_JUMP);
    code.write_short(0);
    auto patch_loc = code.get_size();

    // interpret the dang thing
    interpret_file(filename);

    // now that we know how long that file was, we know how far to jump
    code.patch_short(patch_loc-2, ip - patch_loc);

    auto res = cur_ns;

    memcpy(stack, tmp_stack, STACK_SIZE*sizeof(value));
    cur_ns = tmp_ns;
    frame = tmp_frame;
    ip = tmp_ip;

    return res;
}

fn_namespace* virtual_machine::import_ns(value id) {
    auto ns = find_ns(id);
    if (ns == nullptr) {
        auto path = find_ns_file(id);
        if (!path.has_value()) {
            runtime_error("Import failed. Could not find file.");
        }
        auto res = load_ns(id, *path);
        return res;
    } else {
        return ns;
    }
}

vector<value> virtual_machine::get_roots() {
    vector<value> res;

    // stack
    u32 m = frame->sp + frame->bp;
    for (u32 i = 0; i < m; ++i) {
        res.push_back(stack[i]);
    }
    
    // upvalues for the current call frame
    for (auto f = frame; f != nullptr; f = f->prev) {
        auto n = f->caller->stub->num_upvals;
        for (u32 i = 0; i < n; ++i) {
            auto u = f->caller->upvals;
            res.push_back(**u[i++].val);
        }
    }

    // this will handle all the global variables
    res.push_back(as_value(ns_root));

    // While unlikely, it is possible that garbage collection happens after a
    // pop but before the last pop value is used. This ensures it won't be freed
    // prematurely.
    res.push_back(lp);

    return res;
}

u32 virtual_machine::get_ip() const {
    return ip;
}

value virtual_machine::last_pop() const {
    return lp;
}

void virtual_machine::add_global(value name, value v) {
    if (!name.is_symbol()) {
        runtime_error("Global name is not a symbol.");
    }
    auto sym = v_usym_id(name);
    if (frame != nullptr && frame->caller != nullptr) {
        // TODO: get namespace from chunk
        auto ns = cur_ns;
        ns->set(sym, v);
    } else {
        cur_ns->set(sym, v);
    }
}

value virtual_machine::get_global(value name) {
    if (!name.is_symbol()) {
        runtime_error("Global name is not a symbol.");
    }
    auto sym = v_usym_id(name);

    optional<value> res;
    if (frame != nullptr && frame->caller != nullptr) {
        // TODO: get namespace from chunk
        auto ns = cur_ns;
        //auto ns = frame->caller->stub->ns;
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
                                  optional<value> (*func)(local_addr, value*, virtual_machine*),
                                  local_addr min_args,
                                  bool var_args) {
    auto v = alloc.add_foreign(min_args, var_args, func);
    auto tmp = cur_ns;
    cur_ns = core_ns;
    add_global(as_sym_value(code.get_symbol_id(name)), v);
    foreign_funcs.push_back(v);
    cur_ns = tmp;
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

fn_namespace* virtual_machine::current_ns() {
    return cur_ns;
}

void virtual_machine::runtime_error(const string& msg) const {
    auto p = code.location_of(ip);
    if (p == nullptr) {
        throw fn_error("runtime",
                       "(ip = " + std::to_string(ip) + ") " + msg,
                       source_loc{"<bytecode top>"});
    }
    throw fn_error("runtime", "(ip = " + std::to_string(ip) + ") " + msg, *p);
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
    // last argument is table, second to last is list
    auto arg_tab = pop();
    auto arg_list = pop();

    auto tag = v_tag(arg_list);
    if (tag != TAG_EMPTY && tag != TAG_CONS) {
        runtime_error("2nd-to-last argument to apply must be a list.");
    }
    tag = v_tag(arg_tab);
    if (tag != TAG_TABLE) {
        runtime_error("Last argument to apply must be a table.");
    }

    forward_list<value> stack_vals;
    for (u32 i = 0; i < num_args; ++i) {
        stack_vals.push_front(pop());
    }

    // now the function is at the top of the stack

    // push arguments
    push(arg_tab);
    for (auto v : stack_vals) {
        push(v);
    }
    // count variadic arguments
    u32 vlen = 0;
    auto tl = arg_list;
    while (tl.is_cons()) {
        push(v_uhead(tl));
        tl = v_utail(tl);
        ++vlen;
    }
 
    // TODO: use a maxargs constant
    if (vlen + num_args > 255) {
        runtime_error("Too many arguments for function call in apply.");
    }

    return call(static_cast<local_addr>(vlen + num_args));
}

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
        auto func = callee.ufunction();
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
            auto id = v_usym_id(*k);
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
                v_utab_set(vtable, *k, **cts.get(*k));
            }
        }

        // finish placing positional parameters on the stack
        for (u32 i = num_args; i < stub->positional.size(); ++i) {
            auto v = pos.get(stub->positional[i]);
            if (v.has_value()) {
                push(**v);
            } else if (i >= stub->optional_index) {
                push(callee.ufunction()->init_vals[i-stub->optional_index]);
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
        auto f = callee.uforeign();
        if (num_args < f->min_args) {
            runtime_error("Too few arguments for foreign function call at ip="
                          + std::to_string(ip));
        } else if (!f->var_args && num_args > f->min_args) {
            runtime_error("Too many arguments for foreign function call at ip="
                          + std::to_string(ip));
        } else {
            // correct arity
            auto x = f->func((u16)num_args,
                             &stack[frame->bp+frame->sp-num_args],
                             this);
            // if there's no return value, it means the foreign function handed
            // control off to another function, so we don't manipulate the stack.
            if (x.has_value()) {
                res = *x;
            } else {
                return ip;
            }
        }
        // pop args+2 times (to get the function and keyword dictionary)
        pop_times(num_args+2);
        push(res);
        alloc.enable_gc();
        return ip + 2;
    } else if (tag == TAG_TABLE) {
        auto s = get_symtab().intern("__on-call__");
        auto v = v_utab_get(callee, as_value(*s));

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
        runtime_error("attempt to call nonfunction at address " + std::to_string((i32)ip));
        return ip + 2;
    }
}


#define push_bool(b) push(b ? V_TRUE : V_FALSE);
void virtual_machine::step() {

    u8 instr = code.read_byte(ip);

    // variable for use inside the switch
    value v1, v2, v3;
    optional<value*> vp;

    bool skip = false;
    bool jump = false;
    bc_addr addr = 0;

    fn_namespace* ns;

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
        push(alloc.add_function
             (stub,
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
            push(v_utab_get(v2, v1));
            break;
        } else if (v_tag(v2) == TAG_NAMESPACE) {
            if (v_tag(v1) == TAG_SYM) {
                vp = v2.unamespace()->contents.get(v_usym_id(v1));
                if (!vp.has_value()) {
                    runtime_error("obj-get undefined key for namespace");
                }
                push(**vp);
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
        v2.utable()->contents.insert(v1,v3);
        break;

    case OP_NAMESPACE:
        v1 = pop();

        if (v_tag(v1) != TAG_NAMESPACE) {
            runtime_error("namespace operand not a namespace");
        }
        cur_ns = v1.unamespace();
        break;

    case OP_IMPORT:
        v1 = pop();
        ns = import_ns(v1);
        push(as_value(ns));
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

