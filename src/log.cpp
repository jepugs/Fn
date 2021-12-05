#include "log.hpp"

namespace fn {

logger::logger(std::ostream* err_out, std::ostream* info_out)
    : err_out{err_out}
    , info_out{info_out} {
}

symbol_id logger::intern_filename(const string& filename) {
    return filename_table.intern(filename);
}

string logger::get_filename(symbol_id id) {
    return filename_table.symbol_name(id);
}

void logger::log_fault(const fault& err) {
    log_error(err.origin, err.subsystem, err.message);
}

static void print_loc(std::ostream* out, const source_loc& origin) {
    (*out) << "line " << origin.line << ", col " << origin.col
           << " in " << origin.filename;
}

void logger::log_error(const source_loc&  origin,
        const string& subsystem,
        const string& message) {
    (*err_out) << "[ERROR] " << subsystem << ": ";
    print_loc(err_out, origin);
    (*err_out) << ":\n\t" << message << '\n';
}

void logger::log_error(const string& subsystem,
        const string& message) {
    (*err_out) << "[ERROR] " << subsystem << ":\n\t"
               << message << '\n';
}

void logger::log_warning(const source_loc&  origin,
        const string& subsystem,
        const string& message) {
    (*err_out) << "[WARNING] " << subsystem << ": ";
    print_loc(err_out, origin);
    (*err_out) << ":\n\t" << message << '\n';
}

void logger::log_warning(const string& subsystem,
        const string& message) {
    (*err_out) << "[WARNING] " << subsystem << ":\n\t"
               << message << '\n';
}

void logger::log_info(const source_loc&  origin,
        const string& subsystem,
        const string& message) {
    (*info_out) << "[INFO] " << subsystem << ": ";
    print_loc(info_out, origin);
    (*info_out) << ":\n\t" << message << '\n';
}

void logger::log_info(const string& subsystem,
        const string& message) {
    (*info_out) << "[INFO] " << subsystem << ":\n\t"
               << message << '\n';
}


}
