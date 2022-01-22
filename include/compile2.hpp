#include "array.hpp"
#include "base.hpp"
#include "bytes.hpp"
#include "expand.hpp"
#include "llir.hpp"
#include "obj.hpp"
#include "table.hpp"

namespace fn {


// structure representing a local variable
struct lexical_var {
    symbol_id name;
    u8 index;
    // if true, this is used as an upvalue in an enclosed function
    bool is_upvalue;
    lexical_var* next;
};

struct local_upvalue {
    symbol_id name;
    // direct upvalues are plucked right off the stack; indirect upvalues are
    // copied from the enclosing function
    bool direct;
    u32 index;
    local_upvalue* next;
};

class compile_exception final : public std::exception {
public:
    const char* what() const noexcept override {
        return "compile_exception. This should have been handled internally :(";
    }
};

class compiler {
private:
    istate* S;
    function_tree* ft;
    // this is set when there's an enclosing function being compiled. It's
    // mainly used to look up upvalues.
    compiler* parent;
    // base pointer of this compiler's function rel to absolute stack base
    u32 bp;
    // stack pointer relative to bp
    u32 sp;
    // high water mark for the stack pointer. Used to compute required stack
    // space for a function call.
    u32 sp_hwm;
    // local variables ordered by stack address (highest first)
    lexical_var* var_head;
    // variables captured from a higher call frame
    local_upvalue* uv_head;

    compiler(istate* S, function_tree* ft, compiler* parent=nullptr, u32 bp=0);
    ~compiler();
    // compile the whole function tree
    void compile();
    void compile_sym(symbol_id sid);
    void compile_call(llir_call* form, bool tail);
    void compile_def(llir_def* form);
    void compile_fn(llir_fn* form);
    void compile_var(llir_var* form);
    void compile_llir(llir_form* form, bool tail=false);
    // set sp_hwm = max(local_hwm, sp_hwm)
    void update_hwm(u32 local_hwm);
    // functions to write to code
    void write_byte(u8 u);
    void write_short(u16 u);
    void patch_byte(u8 u, u32 where);
    void patch_short(u16 u, u32 where);
    void patch_jump(u32 jmp_addr, u32 dest);
    lexical_var* lookup_var(symbol_id sid);
    local_upvalue* lookup_upval(symbol_id sid);

    void compile_error(const string& msg);

public:
    friend void compile_form(istate*, ast_form*);
};

// compile an ast form and push it as a function (of no arguments) on top of the
// vm stack
void compile_form(istate* S, ast_form* ast);
// pop a function off the top of the stack, disassemble it, and push the result
// as a string. Decompiles subfunctions recursively if recur=true.
void disassemble_top(istate* S, bool recur=false);


}
