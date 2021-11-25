// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "expand.hpp"
#include "llir.hpp"
#include "parse.hpp"
#include "vm.hpp"


namespace fn {

using namespace fn_parse;

struct interpreter {
private:
    symbol_table symtab;
    global_env globals;
    allocator alloc;

    // Since these don't rightfully belong to any chunks, we save them right
    // here in the interpreter.
    code_chunk* ffi_chunk;

    // logging settings. For now these just go to cout.
    bool log_llir = false;
    bool log_dis = false;

    // ordered list of directories to search for imports
    std::list<string> search_path;

    void interpret_to_end(vm_thread& vm, fault* err);
    value interpret_form(ast_form* ast,
            symbol_id ns,
            working_set* ws,
            fault* err);

public:
    // Initializes the allocator and virtual machine, and starts an empty chunk.
    interpreter();
    ~interpreter();

    void configure_logging(bool log_llir_forms, bool log_disassembly);

    // Accessors
    allocator* get_alloc();
    symbol_table* get_symtab();
    global_env* get_global_env();

    // Init
    // adds a foreign function to fn/builtin
    // void add_builtin_function(const string& name,
    //         value (*foreign_func)(interpreter_handle*,local_address,value*));
    void add_builtin_function(const string& name,
            const string& args,
            value (*foreign_func)(interpreter_handle*,value*));
    
    // Evaluate a source file in an empty chunk. Returns the value from the last
    // expression (or null for an empty file).
    value interpret_file(const string& path,
            working_set* ws,
            fault* err);
    // Evaluate a string in an empty chunk. Returns the value from the last
    // expression (or null).
    value interpret_string(const string& src,
            const string& src_name,
            working_set* ws,
            fault* err);
    value interpret_string(const string& src,
            working_set* ws,
            fault* err);
    // Evaluate all input from an istream. Note that this will not terminate
    // until EOF is encountered in the stream.
    value interpret_istream(std::istream* in,
            const string& src_name,
            working_set* ws,
            fault* err);

    // Evaluate as much of a string as we can. Returns the number of bytes used.
    // Here's how this works: we try to parse ast_forms from src, and execute
    // them one at a time until we get an error. If it's a resumable error (i.e.
    // it might not be an error if there were more text), we roll back the
    // number of bytes consumed to right before that parse attempt. Otherwise,
    // we leave the number of bytes after the parse error.
    dyn_array<value> partial_interpret_string(const string& src,
            const string& src_name,
            working_set* ws,
            u32* bytes_used,
            bool* resumable,
            fault* err);

    // macroexpand a form in the given namespace
    ast_form* expand_macro(symbol_id macro,
            symbol_id ns_id,
            local_address num_args,
            ast_form** args,
            source_loc& loc);

    value ast_to_value(working_set* ws, ast_form* form);
    // returns nullptr on failur
    ast_form* value_to_ast(value v, const source_loc& loc);

    // Emit a runtime error in the form of an exception
    void runtime_error(const string& msg, const source_loc& src);
};

}

#endif
