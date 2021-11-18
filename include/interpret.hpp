// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "base.hpp"
#include "bytes.hpp"
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

    // ordered list of directories to search for imports
    std::list<string> search_path;

    void interpret_to_end(vm_thread& vm);

public:
    // Initializes the allocator and virtual machine, and starts an empty chunk.
    interpreter();
    ~interpreter();

    // Accessors
    allocator* get_alloc();
    symbol_table* get_symtab();
    global_env* get_global_env();

    // Init
    // adds a foreign function to fn/builtin
    void add_builtin_function(const string& name,
            value (*foreign_func)(interpreter_handle*,local_address,value*));
    
    // Evaluate a source file in an empty chunk. Returns the value from the last
    // expression (or null for an empty file).
    value interpret_file(const string& path);
    // Evaluate a string in an empty chunk. Returns the value from the last
    // expression (or null).
    value interpret_string(const string& src);

    // macroexpand a form in the given namespace
    ast_form* macroexpand(symbol_id ns_id, const ast_form* form);
    

    // Emit a runtime error in the form of an exception
    void runtime_error(const string& msg, const source_loc& src);
};

}

#endif
