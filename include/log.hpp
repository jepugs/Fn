#ifndef __FN_LOG_HPP
#define __FN_LOG_HPP

#include "array.hpp"
#include "base.hpp"
#include "table.hpp"
#include "values.hpp"

namespace fn {

class logger {
private:
    std::ostream* err_out;
    std::ostream* info_out;
    symbol_table filename_table;

public:
    // info_out and err_out must be externally managed and ensured to outlive
    // the logger. They may be null, in which case messages are simply ignored.
    logger(std::ostream* err_out, std::ostream* info_out);

    // The logger maintains a table of internalized filenames so that source_loc
    // objects don't have to carry around strings. This makes sense because a
    // single logger object should outlive the interpreter instance that's
    // making the tokens.
    symbol_id intern_filename(const string& filename);
    string get_filename(symbol_id id);

    // All log messages accept an optional source_loc

    // this logs a fault (as an error)
    void log_fault(const fault& err);
    // an error goes to err_out and indicates a stoppage of control flow
    void log_error(const source_loc& origin,
            const string& subsystem,
            const string& message);
    void log_error(const string& subsystem, const string& message);

    // a warning goes to err_out but is not considered fatal
    void log_warning(const string& subsystem, const string& message);
    void log_warning(const source_loc& origin,
            const string& subsystem,
            const string& message);

    // info messages are logged to info_out
    void log_info(const string& subsystem, const string& message);
    void log_info(const source_loc& origin,
            const string& subsystem,
            const string& message);
};


}


#endif
