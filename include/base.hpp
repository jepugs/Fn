// base.hpp -- common error handling code for fn

#ifndef __FN_BASE_HPP
#define __FN_BASE_HPP

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <string>

namespace fn {

using namespace std;

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
typedef float f32;
static_assert(sizeof(double) == 8);
typedef double f64;

/// semantic typedefs
// addresses on the stack
typedef u16 StackAddr;
// addresses in the current call frame (i.e. arguments and local variables)
typedef u8 Local;
// 32-bit integers represent addresses in the bytecode
typedef u32 Addr;

struct SourceLoc {
    const shared_ptr<string> filename;
    const int line;
    const int col;

    SourceLoc(string *filename, int line, int col) : filename(new string(*filename)), line(line), col(col) { }
    SourceLoc(const shared_ptr<string>& filename, int line, int col)
        : filename(filename), line(line), col(col) { }
    SourceLoc(const SourceLoc& loc) : filename(loc.filename), line(loc.line), col(loc.col) { }
};

class FNError : public exception {
    // Pointer to the formatted error message. Need this to ensure that the return value of what()
    // is properly cleaned up when the object is destroyed.
    string *formatted;

    public:
    const string subsystem;
    const string message;
    const SourceLoc origin;

    // TODO: move this to a .cpp file so we don't need to include sstream
    FNError(const string& subsystem, const string& message, const SourceLoc& origin)
        : subsystem(subsystem), message(message), origin(origin) {
        // build formatted error message
        ostringstream ss;
        ss << "[" + subsystem + "] error at line " << origin.line << ",col " << origin.col
           << " in " << *origin.filename << ":\n\t" << message;
        formatted = new string(ss.str());
        
    }
    ~FNError() {
        delete formatted;
    }

    const char* what() const noexcept override {
        return formatted->c_str();
    }

};

    
}

#endif
