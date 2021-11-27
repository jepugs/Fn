#ifndef __FN_FNFFI_H
#define __FN_FNFFI_H

#ifdef __CPLUSPLUS
extern "C" {
#endif

#include <stdint.h>

// all values fit in 64 bits
typedef uint64_t fnvalue;
// symbols have 32-bit numeric identifiers
typedef uint32_t fnsymbol_id;
// opaque handle allowing foreign functions to access interpreter functionality
extern struct fnv_handle;
typedef struct fnv_handle fnv_handle;

extern const uint64_t FNV_TAG_NUM;
extern const uint64_t FNV_TAG_STRING;
extern const uint64_t FNV_TAG_CONS;
extern const uint64_t FNV_TAG_TABLE;
extern const uint64_t FNV_TAG_FUNC;
extern const uint64_t FNV_TAG_SYM;
extern const uint64_t FNV_TAG_NIL;
extern const uint64_t FNV_TAG_BOOL;
extern const uint64_t FNV_TAG_EMPTYL;

extern const fnvalue FNV_NIL;
extern const fnvalue FNV_TRUE;
extern const fnvalue FNV_FALSE;
extern const fnvalue FNV_EMPTYL;

// checking types
// int types treated as _Booleans
extern uint64_t fnvtag(fnvalue v);
extern _Bool fnvis_number(fnvalue v);
extern _Bool fnvis_string(fnvalue v);
extern _Bool fnvis_cons(fnvalue v);
extern _Bool fnvis_table(fnvalue v);
extern _Bool fnvis_function(fnvalue v);
extern _Bool fnvis_symbol(fnvalue v);
extern _Bool fnvis_nil(fnvalue v);
extern _Bool fnvis_bool(fnvalue v);
extern _Bool fnvis_emptyl(fnvalue v);
// checks for cons or empty
extern int fnvis_list(fnvalue v);

// creating number, boolean values
extern fnvalue fnvbox_number(fnvalue v);
extern fnvalue fnvbox_symbol(fnsymbol_id i);
extern fnvalue fnvbox_bool(_Bool b);


// The following value manipulation functions ARE NOT TYPESAFE. You will cause
// terrible errors if you are not 100% sure about the types of the values you're
// manipulating.

// get number (resp symbol id) represented by a value
extern float64_t fnvnumber(fnvalue v);
extern fnsymbol_id fnvsymbol(fnvalue v);

// string values

// you don't need to free this string, as it is attached to the string object v.
extern const char* fnvcstring(fnvalue v);
extern fnvalue fnvstrlen(fnvalue v);

// list/cons values
extern fnvalue fnvhead(fnvalue v); // v must be a cons
extern fnvalue fnvtail(fnvalue v); // v must be cons or emptyl
extern fnvalue fnvnth(uint32_t n, fnvalue v); // must be a list of n elements
extern fnvalue fnvlength(fnvalue v); // list length
// drop returns fewer than n elements if fewer than n are available
extern fnvalue fnvdrop(uint32_t n, fnvalue v);

// table values
extern u32 fnvnum_keys(fnvalue v);
// you must free this array of values when you're done. size is set to the same
// value as returned by fnvnum_keys
extern fnvalue fnvget_keys(fnvalue v, uint32_t* size);
// get and set
extern fnvalue fnvget(fnvalue table, fnvalue key);
extern fnvalue fnvset(fnvalue table, fnvalue key, fnvalue val);

// The following functions require a fnv_handle to do allocations through the
// garbage collector. They are still NOT TYPESAFE. This is all to avoid
// performance penalties from redundant type checks, but be careful, ok?

// the substring bounds here are clamped to the string bounds, so you are not
// guaranteed that the return value has length len, but you are guaranteed not
// to cause a memory fault provided v really is a string. As such, passing -1
// for len is a completely valid and supported way to get a substring going all
// the way to the end.
extern fnvalue fnvsubstr(fnvalue v, uint32_t start, uint32_t len, fnv_handle* vm);
// this is guaranteed to return a string value
extern fnvalue fnvtostring(fnvalue v, fnv_handle* h);
extern fnvalue fnvstrcat(fnvalue l, fnvalue r, fnv_handle* h);

// these two are guaranteed to return symbol values
extern fnvalue fnvintern(const char* name, fnv_handle* h);
extern fnvalue fnvgensym(fnv_handle* h);
// this is guaranteed to return a string value
extern fnvalue fnvsymname(fnsymbol_id v, fnv_handle* h);

// take returns fewer than n elements if fewer than n are available
extern fnvalue fnvtake(uint32_t n, fnvalue v);
extern fnvalue fnvdlistcat(fnvalue v); // concat
extern fnvalue fnvreverse(fnvalue v); // reverse
// destructive concat. Danger! Don't use on arguments.
extern fnvalue fnvdlistcat(fnvalue v);
// destructive reverse. Danger! Don't use on arguments.
extern fnvalue fnvdreverse(fnvalue v);

// join two tables. The keys from the second argument take priority in the case
// of collision.
extern fnvalue fnvtabcat(fnvalue l, fnvalue r);

// function application. Warning: if a call from a foreign function triggers an
// import, it will cause a runtime error. num_args is the length of array args,
// which contains all positional arguments. kwargs must be FNV_NIL or a table
// containing keyword arguments to the function.
extern fnvalue fnvapply(fnvalue fun,
        fnvalue num_args,
        fnvalue* args,
        fnvalue kwargs,
        fnv_handle* h);

extern fnvalue fnvapply(fnvalue fun,
        fnvalue num_args,
        fnvalue* args,
        fnvalue kwargs,
        fnv_handle* h);

// this hooks into the runtime and sets an error. When the function returns with
// an error set, its return value is ignored when the interpreter handles the
// error. A suggested idiom is to return FNV_NIL after calling this (and after
// freeing the message string if necessary).
extern fnvalue fnvfault(const char* message, fnv_handle* h);


// object creation through the GC

// str is copied to make the string object here
extern fnvalue fnvadd_string(const char* str, fnv_handle* h);
extern fnvalue fnvadd_cons(fnvalue hd, fnvalue tl, fnv_handle* h);
extern fnvalue fnvadd_table(fnv_handle* h);
// note: make symbols through intern/gensym
// there is no way to make functions

// get the value global variable
extern fnvalue fnvglobal(fnsymbol_id sym, _Bool* err, fnv_handle* h);

// TODO: as I go I'm sure I'll find other functionality I want to expose via
// fnv_handle. For instance, you might want to query information about the call
// stack.

#ifdef __CPLUSPLUS
}
#endif

#endif
