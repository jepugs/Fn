// Main interface to the interpreter
#ifndef __FN_INTERPRET_HPP
#define __FN_INTERPRET_HPP

#include "base.hpp"
#include "bytes.hpp"
#include "compile.hpp"
#include "expand.hpp"
#include "llir.hpp"
#include "log.hpp"
#include "parse.hpp"
#include "vm.hpp"


namespace fn {

using namespace fn_parse;

struct interpreter {
private:
    symbol_table symtab;
    global_env globals;
    allocator alloc;
    logger* log;

    // Since these don't rightfully belong to any chunks, we save them right
    // here in the interpreter.
    code_chunk* ffi_chunk;

    string base_dir;
    string main_prefix;

    // logging settings. For now these just go to cout.
    bool log_llir = false;
    bool log_dis = false;

    // interpret until the vm halts either on an error or on the end of the
    // bytecode.
    void interpret_to_end(vm_thread& vm, fault* err);
    value interpret_form(ast_form* ast,
            symbol_id ns,
            working_set* ws,
            fault* err);

public:
    // Initializes the allocator and virtual machine, and starts an empty chunk.
    // The logger here must not be null and must outlive this interpreter
    // instance.
    // FIXME: this seems inelegant. The problem is that the logger manages the
    // file table. Maybe the interpreter should do that instead? (Alternatively,
    // the interpreter make its own logger).
    interpreter(logger* log);
    ~interpreter();

    // Set the base package. Default is the package of the main file or fn/user
    // if no package declaration is present.
    void set_base_pkg(symbol_id sym);
    // set the base directory. Default is to directory of the main file or the
    // current directory if there is no main file.
    void set_base_dir(const string& dir);
    // determine whether to log llir or disassembly
    void set_log_dis(bool b);
    void set_log_llir(bool b);
    void log_error(fault* err);

    // Accessors
    allocator* get_alloc();
    symbol_table* get_symtab();
    global_env* get_global_env();

    // adds a foreign function to fn/builtin
    void add_builtin_function(const string& name,
            const string& args,
            void (*foreign_func)(fn_handle*,value*));

    // Evaluate a source file in an empty chunk. Returns the value from the last
    // expression (or null for an empty file). A warning will be generated if
    // the specified namespace does not match the package declaration in the
    // file.
    value interpret_file(const string& path,
            symbol_id ns_id,
            working_set* ws,
            fault* err);
    // like interpret_file(), but also sets the base directory and base package
    // to the directory and package of the given file
    value interpret_main_file(const string& path,
            working_set* ws,
            fault* err);
    // namespace override version of interpret_main_file(). The base package
    // will be the package indicated by the ns_id.
    value interpret_main_file(const string& path,
            symbol_id ns_id,
            working_set* ws,
            fault* err);
    // Evaluate a string in an empty chunk. Returns the value from the last
    // expression (or null).
    value interpret_string(const string& src,
            symbol_id ns_id,
            working_set* ws,
            fault* err);
    // Interpret input from a scanner object. When scanning a file, this scanner
    // should start after the namespace declaration (i.e. at the position it's
    // left at by read_ns_decl()). If an error occurs, *resumable is set to true
    // for errors that could be prevented by extending the input stream, and
    // false otherwise.
    value interpret_from_scanner(scanner* sc,
            symbol_id ns_id,
            working_set* ws,
            bool* resumable,
            fault* err);
    // Evaluate as much of a string as we can. Returns the number of bytes used.
    // Here's how this works: we try to parse ast_forms from src, and execute
    // them one at a time until we get an error. If it's a resumable error (i.e.
    // it might not be an error if there were more text), we roll back the
    // number of bytes consumed to right before that parse attempt. Otherwise,
    // we leave the number of bytes after the parse error. Only non-resumable
    // errors are logged. It's up to the caller to log errors if resumable =
    // true.
    dyn_array<value> partial_interpret_string(const string& src,
            symbol_id ns_id,
            working_set* ws,
            u32* bytes_used,
            bool* resumable,
            fault* err);


    // Import a namespace, performing full search. Returns false if no file is
    // found.
    bool import_ns(symbol_id ns_id, working_set* ws, fault* err);
    // read the namespace declaration from a file, if present. This will leave
    // the scanner where it left off, so it should be reinitialized if this
    // returns nullopt.
    optional<symbol_id> read_ns_decl(scanner* sc,
            working_set* ws,
            fault* err);
    // search for the file, first relative to the base package, then in the
    // search path (unimplemented), and then in the system package directory.
    optional<string> find_import_file(symbol_id ns_id);

    // macroexpand a form in the given namespace
    ast_form* expand_macro(symbol_id macro,
            symbol_id ns_id,
            local_address num_args,
            ast_form** args,
            source_loc& loc,
            fault* err);

    value ast_to_value(working_set* ws, ast_form* form);
    // returns nullptr on failur
    ast_form* value_to_ast(value v, const source_loc& loc);

    symbol_id intern(const string& str);
    symbol_id gensym();

    // Emit a runtime error in the form of an exception
    void runtime_error(const string& msg, const source_loc& src);
};

// initialize a vm_thread with a new global state
vm_thread* init_vm();

// interpret expressions from a file
void interpret_main_file(vm_thread* vm, const string& filename);
// Here's how this works: we try to parse ast_forms from src, and execute
// them one at a time until we get an error. If it's a resumable error (i.e.
// it might not be an error if there were more text), we roll back the
// number of bytes consumed to right before that parse attempt. Otherwise,
// we leave the number of bytes after the parse error. Only non-resumable
// errors are logged. It's up to the caller to log errors if resumable =
// true.
void partial_interpret_string(vm_thread* vm,
        const string& src,
        symbol_id ns_id,
        u32* bytes_used,
        bool* resumable);
// interpret expressions from a scanner until EOF
void interpret_from_scanner(vm_thread* vm, scanner* sc);
// import a file with the specified namespace id. Importing involves creating a
// new global namespace, evaluating the file, and then returning.
void load_file_in_ns(vm_thread* vm, symbol_id ns_id, const string& filename);
// import the specified namespace, performing a search when necessary
void load_ns(vm_thread* vm, symbol_id ns_id);


}

#endif
