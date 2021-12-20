// base.hpp -- common error handling code for fn

#ifndef __FN_BASE_HPP
#define __FN_BASE_HPP

#include <cstdint>
#include <forward_list>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <iostream>

namespace fn {

/// aliases imported from std
template<class T> using forward_list = std::forward_list<T>;
template<class T> using optional = std::optional<T>;
using string = std::string;

template<class T> using shared_ptr = std::shared_ptr<T>;
template<class T> using unique_ptr = std::unique_ptr<T>;

/// integer/float typedefs by bitwidth
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// since we're using u64 for our pointers, we better have 64-bit pointers
static_assert(sizeof(uintptr_t) == 8);

// we also assume we have 64-bit double and 32-bit float
static_assert(sizeof(float) == 4);
typedef float f32; // note: this is not used anywhere at the time of writing
                   // this comment (11/05). Maybe we can drop the assert?
static_assert(sizeof(double) == 8);
typedef double f64;

// this is implemented for std::string and unsigned integers
template<typename T> u64 hash(const T& v);

// naming convention: you can do arithmetic on types whose names end in
// _address, but should not on types that end in _id.

// absolute addresses on the stack
typedef u32 stack_address;
// indexes stack values in the current call frame as well as upvalues
typedef u8 local_address;
// addresses in bytecode
typedef u32 code_address;
// used to identify constant values
typedef u16 constant_id;
// used to identify symbols
typedef u32 symbol_id;
// used to identify namespaces in the global environment
typedef u16 namespace_id;

constexpr u64 max_local_address = 255;

// header for all objects managed by the garbage collector.
struct alignas(16) gc_header {
    u8 bits;         // bitfield holding gc information
    i8 pin_count=0;  // times pinned
    gc_header* next_obj; // next gc_header (intrusive linked list)
};
gc_header* mk_gc_header(u8 bits, gc_header* dest=nullptr);

// Values for the gc_header bits
constexpr u8 GC_MARK_BIT        = 0x01;
constexpr u8 GC_GLOBAL_BIT      = 0x02;
constexpr u8 GC_TYPE_BITMASK    = 0xf0;

// GC Types
constexpr u8 GC_TYPE_CHUNK      = 0x80;
// NOTE: I want these four to line up with the type tags in values.hpp
constexpr u8 GC_TYPE_STRING     = 0x10;
constexpr u8 GC_TYPE_CONS       = 0x20;
constexpr u8 GC_TYPE_TABLE      = 0x30;
constexpr u8 GC_TYPE_FUNCTION   = 0x40;


// Used to track debugging information. An empty string for the filename
// indicates that the bytecode was either internally generated or came from a
// REPL.
struct source_loc {
    string filename;
    int line = 1;
    int col = 0;
    bool operator==(const source_loc& other);
    bool operator!=(const source_loc& other);
};

struct fault {
    bool happened = false;
    source_loc origin;
    string subsystem;
    string message;
};
inline void set_fault(fault* f,
        const source_loc& origin,
        const string& subsystem,
        const string& message) {
    f->happened = true;
    f->origin = origin;
    f->subsystem = subsystem;
    f->message = message;
}

inline void emit_error(std::ostream* out, const fault& err) {
    auto& origin = err.origin;
    (*out) << "[" + err.subsystem + "] Error at line " << origin.line
           << ", col " << origin.col << " in " << origin.filename << ":\n\t"
           << err.message << '\n';
}

// The virtual machine's internal methods and foreign functions throw this
// exception. It gets handled within the VM to prevent it from travelling up the
// call stack.
class runtime_exception : public std::exception {
public:
    const char* what() const noexcept override {
        return "runtime_exception. This should have been handled internally :(";
    }

};

// DEPRECATED in favor of runtime_exception, which never leaves the scope of the
// virtual machine
class fn_exception : public std::exception {
    // pointer to the formatted error message. need this to ensure that the return value of what()
    // is properly cleaned up when the object is destroyed.
    string *formatted;

public:
    const string subsystem;
    const string message;
    const source_loc origin;

    // TODO: move this to a .cpp file so we don't need to include sstream
    fn_exception(const string& subsystem,
            const string& message,
            const source_loc& origin)
        : subsystem{subsystem}
        , message{message}
        , origin{origin} {
        // build formatted error message
        std::ostringstream ss;
        ss << "[" + subsystem + "] error at line " << origin.line << ",col " << origin.col
           << " in " << origin.filename << ":\n\t" << message;
        formatted = new string(ss.str());
        
    }
    ~fn_exception() {
        delete formatted;
    }

    const char* what() const noexcept override {
        return formatted->c_str();
    }

};

    
}

#endif
