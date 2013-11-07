#include "stdlib_c.h"
#include "stdlib/kumirstdlib.hpp"
#include <memory.h>
#include <string.h>
#include <wchar.h>
#include <stack>
#include <stdarg.h>

#if defined(WIN32) || defined(_WIN32)
#else
#   include <sys/resource.h>
#endif

static Kumir::FileType __kumir_scalar_to_file_type(const __kumir_scalar & scalar);
static void __kumir_create_string(__kumir_scalar * result, const std::wstring & wstr);
static __kumir_real __kumir_scalar_as_real(const __kumir_scalar * scalar);
static std::wstring __kumir_scalar_as_wstring(const __kumir_scalar * scalar);

EXTERN void __kumir_internal_debug(int32_t code)
{
    std::cerr << "Debug " << code << std::endl;
}

inline __kumir_int IMAX(const __kumir_int a, const __kumir_int b)
{
    return a < b ? b : a;
}

inline __kumir_int IMIN(const __kumir_int a, const __kumir_int b)
{
    return a > b ? b : a;
}


EXTERN void __use_all_types()
{
    __kumir_bool        b;
    __kumir_int         i;
    __kumir_real        r;
    __kumir_char        c;
    __kumir_string      s;
    __kumir_scalar      scalar;
    __kumir_array       array;
}

EXTERN void __kumir_create_undefined_scalar(__kumir_scalar * result)
{
    result->defined = false;
    result->data.s = nullptr;
}

EXTERN void __kumir_create_undefined_array(__kumir_array * result)
{
    result->data = nullptr;
    result->dim = 0;
}

EXTERN void __kumir_create_defined_scalar(__kumir_scalar * lvalue, const __kumir_scalar * rvalue)
{
    __kumir_copy_scalar(lvalue, rvalue);
}

EXTERN void __kumir_create_int(__kumir_scalar * result, const __kumir_int value)
{
    result->defined = true;
    result->type = __KUMIR_INT;
    result->data.i = value;
}

EXTERN void __kumir_create_real(__kumir_scalar * result, const __kumir_real value)
{
    result->defined = true;
    result->type = __KUMIR_REAL;
    result->data.r = value;
}

EXTERN void __kumir_create_bool(__kumir_scalar * result, const __kumir_bool value)
{
    result->defined = true;
    result->type = __KUMIR_BOOL;
    result->data.b = value;
}

EXTERN void __kumir_create_char(__kumir_scalar * result, const char * utf8)
{
    std::string utf8string(utf8);
    std::wstring wstr;
    try {
        wstr = Kumir::Core::fromUtf8(utf8string);
    }
    catch (Kumir::EncodingError) {
        Kumir::Core::abort(Kumir::Core::fromAscii("Encoding error"));        
    }
    result->defined = true;
    result->type = __KUMIR_CHAR;
    result->data.c = wstr[0];
}

EXTERN void __kumir_create_string(__kumir_scalar  * result, const char * utf8)
{
    std::string utf8string(utf8);
    std::wstring wstr;
    try {
        wstr = Kumir::Core::fromUtf8(utf8string);
    }
    catch (Kumir::EncodingError) {
        Kumir::Core::abort(Kumir::Core::fromAscii("Encoding error"));
    }
    result->defined = true;
    result->type = __KUMIR_STRING;
    result->data.s = reinterpret_cast<wchar_t*>(calloc(wstr.length() + 1, sizeof(wchar_t)));
    memcpy(result->data.s, wstr.c_str(), wstr.length() * sizeof(wchar_t));
    result->data.s[wstr.length()] = L'\0';
}

EXTERN __kumir_variant __kumir_copy_variant(const __kumir_variant rvalue, __kumir_scalar_type type)
{
    __kumir_variant lvalue;
    switch (type) {
    case __KUMIR_INT:
        lvalue.i = rvalue.i;
        break;
    case __KUMIR_REAL:
        lvalue.r = rvalue.r;
        break;
    case __KUMIR_BOOL:
        lvalue.b = rvalue.b;
        break;
    case __KUMIR_CHAR:
        lvalue.c = rvalue.c;
        break;
    case __KUMIR_STRING: {
        size_t sz = wcslen(rvalue.s);
        lvalue.s = reinterpret_cast<wchar_t*>(calloc(sz+1u, sizeof(wchar_t)));
        wcsncpy(lvalue.s, rvalue.s, sz);
        lvalue.s[sz] = L'\0';
        break;
    }
    case __KUMIR_RECORD: {
        lvalue.u.nfields = rvalue.u.nfields;
        lvalue.u.types = reinterpret_cast<__kumir_scalar_type*>(calloc(lvalue.u.nfields, sizeof(__kumir_scalar_type)));
        lvalue.u.fields = reinterpret_cast<__kumir_variant*>(calloc(lvalue.u.nfields, sizeof(__kumir_variant)));
        __kumir_variant * lfields = reinterpret_cast<__kumir_variant*>(lvalue.u.fields);
        __kumir_variant * rfields = reinterpret_cast<__kumir_variant*>(rvalue.u.fields);
        for (size_t i=0u; i<rvalue.u.nfields; i++) {
            __kumir_scalar_type type = lvalue.u.types[i] = rvalue.u.types[i];
            lfields[i] = __kumir_copy_variant(rfields[i], type);
        }
        break;
    }
    }
    return lvalue;
}

EXTERN void __kumir_copy_scalar(__kumir_scalar * lvalue, const __kumir_scalar * rvalue)
{    
    lvalue->defined = rvalue->defined;
    lvalue->type = rvalue->type;
    __kumir_check_value_defined(rvalue);
    if (lvalue->defined) {
        lvalue->data = __kumir_copy_variant(rvalue->data, rvalue->type);
    }
}

EXTERN void __kumir_move_scalar(__kumir_scalar  * lvalue, __kumir_scalar * rvalue)
{
    lvalue->defined = rvalue->defined;
    lvalue->type = rvalue->type;
    memcpy(&(lvalue->data), &(rvalue->data), sizeof(rvalue->data));
}

EXTERN void __kumir_store_scalar(__kumir_scalar ** lvalue_ptr, const __kumir_scalar * rvalue)
{
    __kumir_scalar * lvalue = *lvalue_ptr;
    __kumir_copy_scalar(lvalue, rvalue);
}

EXTERN void __kumir_modify_string(__kumir_stringref * lvalue, const __kumir_scalar * rvalue)
{
    __kumir_check_value_defined(rvalue);
    __kumir_check_value_defined(lvalue->ref);
    const std::wstring r = __kumir_scalar_as_wstring(rvalue);
    std::wstring l = __kumir_scalar_as_wstring(lvalue->ref);
    switch (lvalue->op) {
    case __KUMIR_STRINGREF_APPEND:
        l.append(r);
        break;
    case __KUMIR_STRINGREF_PREPEND:
        l = r + l;
        break;
    case __KUMIR_STRINGREF_INSERT:
        l.insert(lvalue->from, r);
        break;
    case __KUMIR_STRINGREF_REPLACE:
        l.replace(lvalue->from, lvalue->length, r);
        break;
    }
    free(lvalue->ref->data.s);
    __kumir_create_string(lvalue->ref, l);
}

static int32_t __kumir_current_line_number = -1;

EXTERN void __kumir_halt()
{
    const std::wstring message =
            Kumir::Core::fromUtf8("\nСТОП.");
    Kumir::Encoding enc = Kumir::UTF8;
#if defined(WIN32) || defined(_WIN32)
    enc = Kumir::CP866;
#endif
    const std::string loc_message = Kumir::Coder::encode(enc, message);
    std::cerr << loc_message << std::endl;
    exit(0);
}

static void __kumir_handle_abort()
{
    const std::wstring message = __kumir_current_line_number == -1
            ? Kumir::Core::fromUtf8("ОШИБКА ВЫПОЛНЕНИЯ: ") + Kumir::Core::getError()
            : Kumir::Core::fromUtf8("ОШИБКА ВЫПОЛНЕНИЯ В СТРОКЕ ") +
              Kumir::Converter::sprintfInt(__kumir_current_line_number, 10, 0, 0) +
              Kumir::Core::fromAscii(": ") +
              Kumir::Core::getError();
    Kumir::Encoding enc = Kumir::UTF8;
#if defined(WIN32) || defined(_WIN32)
    enc = Kumir::CP866;
#endif
    const std::string loc_message = Kumir::Coder::encode(enc, message);
    std::cerr << loc_message << std::endl;
    exit(1);
}

EXTERN void __kumir_set_current_line_number(const int32_t line_no)
{
    __kumir_current_line_number = line_no;
}

EXTERN void __kumir_check_value_defined(const __kumir_scalar * value)
{
    if (value == nullptr || !value->defined) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Нет значения у величины"));
    }
}

EXTERN __kumir_bool __kumir_scalar_as_bool(const __kumir_scalar * scalar)
{
    __kumir_check_value_defined(scalar);
    return scalar->data.b;
}

EXTERN void __kumir_free_scalar(__kumir_scalar * scalar)
{
    if (scalar->defined && __KUMIR_STRING == scalar->type && scalar->data.s) {
        free(scalar->data.s);
    }
    else if (scalar->defined && __KUMIR_RECORD == scalar->type) {
        __kumir_scalar_type * types = scalar->data.u.types;
        __kumir_variant * fields = reinterpret_cast<__kumir_variant*>(scalar->data.u.fields);
        for (size_t i=0; i<scalar->data.u.nfields; i++) {
            if (__KUMIR_STRING == types[i]) {
                free(fields[i].s);
            }
        }
        free(scalar->data.u.types);
        free(scalar->data.u.fields);
    }
}

EXTERN void __kumir_input_stdin(const __kumir_int format, __kumir_scalar ** pptr)
{    
    __kumir_scalar * ptr = *pptr;
    unsigned int f = static_cast<unsigned int>(format);
    if (__KUMIR_CHAR == f) {
        __kumir_char c = Kumir::IO::readChar();
        ptr->defined = true;
        ptr->type = __KUMIR_CHAR;
        ptr->data.c = c;
    }
    else if (__KUMIR_REAL == f) {
        __kumir_real r = Kumir::IO::readReal();
        __kumir_create_real(ptr, r);
    }
    else if (__KUMIR_INT == f) {
        __kumir_int v = Kumir::IO::readInteger();
        __kumir_create_int(ptr, v);
    }
    else if (__KUMIR_BOOL == f) {
        __kumir_bool b = Kumir::IO::readBool();
        __kumir_create_bool(ptr, b);
    }
    else if (__KUMIR_STRING == f) {
        std::wstring ws = Kumir::IO::readString();
        __kumir_create_string(ptr, ws);
    }
}

EXTERN void __kumir_input_file(const __kumir_scalar * handle, const __kumir_int format, __kumir_scalar ** pptr)
{
    __kumir_check_value_defined(handle);
    Kumir::FileType ft = __kumir_scalar_to_file_type(*handle);

    unsigned int f = static_cast<unsigned int>(format);
    __kumir_scalar * ptr = *pptr;
    if (__KUMIR_CHAR == f) {
        __kumir_char c = Kumir::IO::readChar(ft, false);
        ptr->defined = true;
        ptr->type = __KUMIR_CHAR;
        ptr->data.c = c;
    }
    else if (__KUMIR_REAL == f) {
        __kumir_real r = Kumir::IO::readReal(ft, false);
        __kumir_create_real(ptr, r);
    }
    else if (__KUMIR_INT == f) {
        __kumir_int v = Kumir::IO::readInteger(ft, false);
        __kumir_create_int(ptr, v);
    }
    else if (__KUMIR_BOOL == f) {
        __kumir_bool b = Kumir::IO::readBool(ft, false);
        __kumir_create_bool(ptr, b);
    }
    else if (__KUMIR_STRING == f) {
        std::wstring ws = Kumir::IO::readString(ft, false);
        __kumir_create_string(ptr, ws);
    }

}

EXTERN void __kumir_output_stdout_ii(const __kumir_scalar * value, const int format1, const int format2)
{    
    __kumir_check_value_defined(value);
    switch (value->type) {
    case __KUMIR_INT:
        Kumir::IO::writeInteger(format2, value->data.i);
        break;
    case __KUMIR_REAL:
        Kumir::IO::writeReal(format2, format1, value->data.r);
        break;
    case __KUMIR_BOOL:
        Kumir::IO::writeBool(format2, value->data.b);
        break;
    case __KUMIR_CHAR:
        Kumir::IO::writeChar(format2, value->data.c);
        break;
    case __KUMIR_STRING:
        Kumir::IO::writeString(format2, std::wstring(value->data.s));
        break;
    default:
        break;
    }
}

EXTERN void __kumir_output_stdout_is(const __kumir_scalar * value, const int format1, const __kumir_scalar * format2)
{
    __kumir_check_value_defined(format2);
    __kumir_output_stdout_ii(value, format1, format2->data.i);
}

EXTERN void __kumir_output_stdout_si(const __kumir_scalar * value, const __kumir_scalar * format1, const int format2)
{
    __kumir_check_value_defined(format1);
    __kumir_output_stdout_ii(value, format1->data.i, format2);
}

EXTERN void __kumir_output_stdout_ss(const __kumir_scalar * value, const __kumir_scalar * format1, const __kumir_scalar * format2)
{
    __kumir_check_value_defined(format1);
    __kumir_check_value_defined(format2);
    __kumir_output_stdout_ii(value, format1->data.i, format2->data.i);
}

EXTERN void __kumir_output_file_ii(const __kumir_scalar * handle, const __kumir_scalar * value, const int format1, const int format2)
{
    __kumir_check_value_defined(handle);
    __kumir_check_value_defined(value);
    Kumir::FileType f = __kumir_scalar_to_file_type(*handle);
    switch (value->type) {
    case __KUMIR_INT:
        Kumir::IO::writeInteger(format2, value->data.i, f, false);
        break;
    case __KUMIR_REAL:
        Kumir::IO::writeReal(format2, format1, value->data.r, f, false);
        break;
    case __KUMIR_BOOL:
        Kumir::IO::writeBool(format2, value->data.b, f, false);
        break;
    case __KUMIR_CHAR:
        Kumir::IO::writeChar(format2, value->data.c, f, false);
        break;
    case __KUMIR_STRING:
        Kumir::IO::writeString(format2, std::wstring(value->data.s), f, false);
        break;
    default:
        break;
    }
}

EXTERN void __kumir_output_file_is(const __kumir_scalar * handle, const __kumir_scalar * value, const int format1, const __kumir_scalar * format2)
{
    __kumir_check_value_defined(format2);
    __kumir_output_file_ii(handle, value, format1, format2->data.i);
}

EXTERN void __kumir_output_file_si(const __kumir_scalar * handle, const __kumir_scalar * value, const __kumir_scalar * format1, const int format2)
{
    __kumir_check_value_defined(format1);
    __kumir_output_file_ii(handle, value, format1->data.i, format2);
}

EXTERN void __kumir_output_file_ss(const __kumir_scalar * handle, const __kumir_scalar * value, const __kumir_scalar * format1, const __kumir_scalar * format2)
{
    __kumir_check_value_defined(format1);
    __kumir_check_value_defined(format2);
    __kumir_output_file_ii(handle, value, format1->data.i, format2->data.i);
}

static __kumir_scalar __kumir_file_type_to_scalar(const Kumir::FileType &f)
{
    __kumir_scalar result;
    result.defined = true;
    result.type = __KUMIR_RECORD;
    result.data.u.nfields = 4u;
    __kumir_scalar_type * types = reinterpret_cast<__kumir_scalar_type*>(
                calloc(result.data.u.nfields, sizeof(__kumir_scalar_type))
                );
    __kumir_variant * fields = reinterpret_cast<__kumir_variant*>(
                calloc(result.data.u.nfields, sizeof(__kumir_variant))
                );

    result.data.u.fields = fields;
    result.data.u.types = types;

    types[0] = __KUMIR_STRING;
    types[1] = __KUMIR_INT;
    types[2] = __KUMIR_INT;
    types[3] = __KUMIR_BOOL;

    fields[0].s = reinterpret_cast<wchar_t*>(calloc(f.fullPath.length() + 1u, sizeof(wchar_t)));
    wcsncpy(fields[0].s, f.fullPath.c_str(), f.fullPath.length());
    fields[0].s[f.fullPath.length()] = L'\0';

    fields[1].i = f.mode;
    fields[2].i = f.type;
    fields[3].b = f.valid;


    return result;
}

static Kumir::FileType __kumir_scalar_to_file_type(const __kumir_scalar & scalar)
{
    Kumir::FileType f;
    __kumir_variant * fields = reinterpret_cast<__kumir_variant*>(scalar.data.u.fields);
    f.fullPath = std::wstring(fields[0].s);
    f.mode = fields[1].i;
    f.type = fields[2].i;
    f.valid = fields[3].b;
    return f;
}

static void __kumir_create_string(__kumir_scalar * result, const std::wstring & wstr)
{
    result->type = __KUMIR_STRING;
    result->defined = true;
    result->data.s = reinterpret_cast<wchar_t*>(
                calloc(wstr.length() + 1, sizeof(wchar_t))
                );
    result->data.s[wstr.length()] = L'\0';
    wcsncpy(result->data.s, wstr.c_str(), wstr.length());
}

// Math

EXTERN void __kumir__stdlib__div(__kumir_scalar  * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);
    __kumir_create_int(result, Kumir::Math::div(left->data.i, right->data.i));
}

EXTERN void __kumir__stdlib__mod(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);
    __kumir_create_int(result, Kumir::Math::mod(left->data.i, right->data.i));
}

EXTERN void __kumir__stdlib__ln(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result, Kumir::Math::ln(__kumir_scalar_as_real(value)));
}

EXTERN void __kumir__stdlib__lg(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result, Kumir::Math::lg(__kumir_scalar_as_real(value)));
}

EXTERN void __kumir__stdlib__exp(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result, Kumir::Math::exp(__kumir_scalar_as_real(value)));
}

EXTERN void __kumir__stdlib__rnd(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Random::rrnd(
                            __kumir_scalar_as_real(value)
                            )
                        );
}

EXTERN void __kumir__stdlib__iabs(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::Math::iabs(
                           value->data.i
                           )
                       );
}

EXTERN void __kumir__stdlib__abs(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                       Kumir::Math::abs(
                           __kumir_scalar_as_real(value)
                           )
                       );
}

EXTERN void __kumir__stdlib__sign(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::Math::sign(
                           __kumir_scalar_as_real(value)
                           )
                       );
}

EXTERN void __kumir__stdlib__int(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::Math::intt(
                           __kumir_scalar_as_real(value)
                           )
                       );
}

EXTERN void __kumir__stdlib__arcsin(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::arcsin(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__arccos(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::arccos(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__arctg(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::arctg(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__arcctg(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::arcctg(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__tg(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::tg(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__ctg(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::ctg(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__sin(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::sin(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__cos(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_real(result,
                        Kumir::Math::cos(__kumir_scalar_as_real(value))
                        );
}

EXTERN void __kumir__stdlib__tsel_v_lit(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_string(result,
                       Kumir::Converter::intToString(value->data.i)
                       );
}

EXTERN void __kumir__stdlib__vesch_v_lit(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_string(result,
                       Kumir::Converter::realToString(__kumir_scalar_as_real(value))
                       );
}

EXTERN void __kumir__stdlib__dlin(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::StringUtils::length(__kumir_scalar_as_wstring(value))
                );
}

EXTERN void __kumir__stdlib__yunikod(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::StringUtils::unicode(value->data.c)
                );
}

EXTERN void __kumir__stdlib__kod(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    __kumir_create_int(result,
                       Kumir::StringUtils::code(value->data.c)
                );
}

EXTERN void __kumir__stdlib__simvol(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    result->defined = true;
    result->type = __KUMIR_CHAR;
    result->data.c = Kumir::StringUtils::symbol(value->data.i);
}

EXTERN void __kumir__stdlib__simvol2(__kumir_scalar * result, const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    result->defined = true;
    result->type = __KUMIR_CHAR;
    result->data.c = Kumir::StringUtils::unisymbol(value->data.i);
}

EXTERN void __kumir__stdlib__lit_v_vesch(__kumir_scalar * result, const __kumir_scalar * value, __kumir_scalar * success)
{
    __kumir_check_value_defined(value);
    bool ok;
    double res = Kumir::Converter::stringToReal(__kumir_scalar_as_wstring(value), ok);
    __kumir_create_real(result, res);
    __kumir_create_bool(success, ok);
}

EXTERN void __kumir__stdlib__lit_v_tsel(__kumir_scalar * result, const __kumir_scalar * value, __kumir_scalar * success)
{
    __kumir_check_value_defined(value);
    bool ok;
    int res = Kumir::Converter::stringToInt(__kumir_scalar_as_wstring(value), ok);
    __kumir_create_int(result, res);
    __kumir_create_bool(success, ok);
}

// Files
EXTERN void __kumir__stdlib__est_dannyie(__kumir_scalar * result, const __kumir_scalar * handle)
{
    __kumir_check_value_defined(handle);
    Kumir::FileType f = __kumir_scalar_to_file_type(*handle);
    bool res = Kumir::Files::hasData(f);
    __kumir_create_bool(result, res);
}

EXTERN void __kumir__stdlib__KATALOG_PROGRAMMYi(__kumir_scalar * result)
{
    const std::wstring res = Kumir::Files::CurrentWorkingDirectory();
    __kumir_create_string(result, res);
}

EXTERN void __kumir__stdlib__otkryit_na_chtenie(__kumir_scalar * result, const __kumir_scalar * name)
{
    __kumir_check_value_defined(name);
    std::wstring wsname(name->data.s);
    Kumir::FileType f = Kumir::Files::open(wsname, Kumir::FileType::Read);
    *result = __kumir_file_type_to_scalar(f);
}

EXTERN void __kumir__stdlib__otkryit_na_zapis(__kumir_scalar * result, const __kumir_scalar * name)
{
    __kumir_check_value_defined(name);
    std::wstring wsname(name->data.s);
    Kumir::FileType f = Kumir::Files::open(wsname, Kumir::FileType::Write);
    *result = __kumir_file_type_to_scalar(f);
}

EXTERN void __kumir__stdlib__zakryit(const __kumir_scalar * handle)
{
    __kumir_check_value_defined(handle);
    Kumir::FileType f = __kumir_scalar_to_file_type(*handle);
    Kumir::Files::close(f);
}

EXTERN void __kumir__stdlib__vremya(__kumir_scalar * result)
{
    __kumir_create_int(result, Kumir::System::time());
}

EXTERN void __kumir__stdlib__zhdat(const __kumir_scalar * value)
{
    __kumir_check_value_defined(value);
    if (value->data.i < 0) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Отрицательное время"));
    }
    uint32_t msec = static_cast<uint32_t>(value->data.i);
#if defined(WIN32) || defined(_WIN32)
    Sleep(msec);
#else
    uint32_t sec = msec / 1000;
    uint32_t usec = (msec - sec * 1000) * 1000;
    // usleep works in range [0, 1000000), so
    // call sleep(sec) first for long periods
    sleep(sec);
    usleep(usec);
#endif
}

static __kumir_real __kumir_scalar_as_real(const __kumir_scalar * scalar)
{
    __kumir_real result = 0.;
    if (__KUMIR_REAL == scalar->type) {
        result = scalar->data.r;
    }
    else if (__KUMIR_INT == scalar->type) {
        result = static_cast<__kumir_real>(scalar->data.i);
    }
    return result;
}

static std::wstring __kumir_scalar_as_wstring(const __kumir_scalar * scalar)
{
    std::wstring result;
    if (__KUMIR_CHAR == scalar->type) {
        result.push_back(scalar->data.c);
    }
    else if (__KUMIR_STRING == scalar->type) {
        result = std::wstring(scalar->data.s);
    }
    return result;
}

static signed char __kumir_compare_scalars(const __kumir_scalar * left, const __kumir_scalar * right)
{
    signed char result = 0;
    if (__KUMIR_INT == left->type && __KUMIR_INT == right->type) {
        const __kumir_int l = left->data.i;
        const __kumir_int r = right->data.i;
        if (l < r) result = 1;
        else if (l > r) result = -1;
        else if (l == r) result = 0;
    }
    else if (__KUMIR_REAL == left->type || __KUMIR_REAL == right->type) {
        const __kumir_real l = __kumir_scalar_as_real(left);
        const __kumir_real r = __kumir_scalar_as_real(right);
        if (l < r) result = 1;
        else if (l > r) result = -1;
        else if (l == r) result = 0;
    }
    else if (__KUMIR_BOOL == left->type && __KUMIR_BOOL == right->type) {
        const __kumir_bool l = left->data.b;
        const __kumir_bool r = right->data.b;
        if (l == r) result = 0;
        else result = 1;
    }
    else if (__KUMIR_STRING == left->type || __KUMIR_STRING == right->type) {
        const std::wstring l = __kumir_scalar_as_wstring(left);
        const std::wstring r = __kumir_scalar_as_wstring(right);
        if (l < r) result = 1;
        else if (l > r) result = -1;
        else if (l == r) result = 0;
    }
    else if (__KUMIR_CHAR == left->type && __KUMIR_CHAR == right->type) {
        const __kumir_char l = left->data.c;
        const __kumir_char r = right->data.c;
        if (l < r) result = 1;
        else if (l > r) result = -1;
        else if (l == r) result = 0;
    }
    return result;
}

EXTERN void __kumir_operator_eq(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);
    result->defined = true;
    result->type = __KUMIR_BOOL;
    signed char v = __kumir_compare_scalars(left, right);
    result->data.b = v == 0;
}

EXTERN void __kumir_operator_ls(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    result->defined = true;
    result->type = __KUMIR_BOOL;
    result->data.b = __kumir_compare_scalars(left, right) == 1;
}

EXTERN void __kumir_operator_gt(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    result->defined = true;
    result->type = __KUMIR_BOOL;
    result->data.b = __kumir_compare_scalars(left, right) == -1;
}

EXTERN void __kumir_operator_lq(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    result->defined = true;
    result->type = __KUMIR_BOOL;
    result->data.b = __kumir_compare_scalars(left, right) >= 0;
}

EXTERN void __kumir_operator_gq(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    result->defined = true;
    result->type = __KUMIR_BOOL;
    result->data.b = __kumir_compare_scalars(left, right) <= 0;
}

EXTERN void __kumir_operator_neq(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_operator_eq(result, left, right);
    result->data.b = !result->data.b;
}

EXTERN void __kumir_operator_sum(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    if (__KUMIR_INT == left->type && __KUMIR_INT == right->type) {
        if (!Kumir::Math::checkSumm(left->data.i, right->data.i)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Целочисленное переполнение"));
        }
        __kumir_create_int(result, left->data.i + right->data.i);
    }
    else if (__KUMIR_REAL == left->type || __KUMIR_REAL == right->type) {
        const __kumir_real l = __kumir_scalar_as_real(left);
        const __kumir_real r = __kumir_scalar_as_real(right);
        if (!Kumir::Math::checkSumm(l, r)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Вещественное переполнение"));
        }
        __kumir_create_real(result, l + r);
    }
    else if (__KUMIR_STRING == left->type || __KUMIR_CHAR == left->type) {
        const std::wstring l = __kumir_scalar_as_wstring(left);
        const std::wstring r = __kumir_scalar_as_wstring(right);
        const std::wstring res = l + r;
        __kumir_create_string(result, res);
    }
}

EXTERN void __kumir_operator_sub(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    if (__KUMIR_INT == left->type && __KUMIR_INT == right->type) {
        if (!Kumir::Math::checkDiff(left->data.i, right->data.i)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Целочисленное переполнение"));
        }
        __kumir_create_int(result, left->data.i - right->data.i);
    }
    else if (__KUMIR_REAL == left->type || __KUMIR_REAL == right->type) {
        const __kumir_real l = __kumir_scalar_as_real(left);
        const __kumir_real r = __kumir_scalar_as_real(right);
        if (!Kumir::Math::checkDiff(l, r)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Вещественное переполнение"));
        }
        __kumir_create_real(result, l - r);
    }
}

EXTERN void __kumir_operator_mul(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    if (__KUMIR_INT == left->type && __KUMIR_INT == right->type) {
        if (!Kumir::Math::checkProd(left->data.i, right->data.i)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Целочисленное переполнение"));
        }
        __kumir_create_int(result, left->data.i * right->data.i);
    }
    else if (__KUMIR_REAL == left->type || __KUMIR_REAL == right->type) {
        const __kumir_real l = __kumir_scalar_as_real(left);
        const __kumir_real r = __kumir_scalar_as_real(right);
        __kumir_create_real(result, l * r);
        if (!Kumir::Math::isCorrectReal(result->data.r)) {
            Kumir::Core::abort(Kumir::Core::fromUtf8("Вещественное переполнение"));
        }
    }
}

EXTERN void __kumir_operator_div(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    if ( (__KUMIR_INT == right->type && 0 == right->data.i) || (__KUMIR_REAL == right->type && 0. == right->data.r) ) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Деление на ноль"));
    }

    const __kumir_real l = __kumir_scalar_as_real(left);
    const __kumir_real r = __kumir_scalar_as_real(right);
    __kumir_create_real(result, l/r);
    if (!Kumir::Math::isCorrectReal(result->data.r)) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Вещественное переполнение"));
    }
}

EXTERN void __kumir_operator_pow(__kumir_scalar * result, const __kumir_scalar * left, const __kumir_scalar * right)
{
    __kumir_check_value_defined(left);
    __kumir_check_value_defined(right);

    if (__KUMIR_INT == left->type && __KUMIR_INT == right->type) {
        __kumir_create_int(result, Kumir::Math::ipow(left->data.i, right->data.i));
    }
    else {
        const __kumir_real l = __kumir_scalar_as_real(left);
        const __kumir_real r = __kumir_scalar_as_real(right);
        __kumir_create_real(result, Kumir::Math::pow(l, r));
    }
}

EXTERN void __kumir_operator_neg(__kumir_scalar * result, const __kumir_scalar * left)
{
    __kumir_check_value_defined(left);
    result->defined = true;
    result->type = left->type;
    if (__KUMIR_BOOL == left->type) {
        result->data.b = ! left->data.b;
    }
    else if (__KUMIR_INT == left->type) {
        result->data.i = - left->data.i;
    }
    else if (__KUMIR_REAL == left->type) {
        result->data.r = 0.0 - left->data.r;
    }
}

EXTERN void __kumir_assert(const __kumir_scalar * assumption)
{
    __kumir_check_value_defined(assumption);
    bool value = assumption->data.b;
    if (!value) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Утверждение ложно"));
    }
}

EXTERN void __kumir_abort_on_error(const char * message)
{
    Kumir::Core::abort(Kumir::Core::fromUtf8(std::string(message)));
}

EXTERN void __kumir_init_stdlib()
{
    // Set stack size some greater...
#if defined(WIN32) || defined(_WIN32)
#else
    static const rlim_t kStackSize = 32 * 1024 * 1024;
    struct rlimit rl;
    int result;
    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0) {
        if (rl.rlim_cur < kStackSize) {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            if (result != 0) {
                std::cerr << "Warning: Can't set rlimit stack size\n";
            }
        }
    }
    else {
        std::cerr << "Warning: Can't get rlimit stack size\n";
    }
#endif
    // Init Standard library
    Kumir::initStandardLibrary();

    // Set abort and print message on error handler
    Kumir::Core::AbortHandler = &__kumir_handle_abort;
}

EXTERN void __kumir_create_array_1(__kumir_array * result,
                                   const __kumir_scalar * left_1,
                                   const __kumir_scalar * right_1
                                   )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    result->dim = 1u;
    result->shape_left[0] = result->size_left[0] = left_1->data.i;
    result->shape_right[0] = result->size_right[0] = right_1->data.i;
    result->shape_left[1] = result->size_left[1] = 0;
    result->shape_right[1] = result->size_right[1] = 0;
    result->shape_left[2] = result->size_left[2] = 0;
    result->shape_right[2] = result->size_right[2] = 0;
    int isz = 1 + right_1->data.i - left_1->data.i;
    if (isz < 0) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    else {
        size_t sz = static_cast<size_t>(isz);
        if (sz) {
            __kumir_scalar * scalars = reinterpret_cast<__kumir_scalar*>(
                        calloc(sz, sizeof(__kumir_scalar))
                        );
            result->data = scalars;
            for (size_t i=0; i<sz; i++) {
                __kumir_create_undefined_scalar(&scalars[i]);
            }
        }
    }
}

EXTERN void __kumir_create_array_2(__kumir_array * result,
                                   const __kumir_scalar * left_1,
                                   const __kumir_scalar * right_1,
                                   const __kumir_scalar * left_2,
                                   const __kumir_scalar * right_2
                                   )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    __kumir_check_value_defined(left_2);
    __kumir_check_value_defined(right_2);
    result->dim = 2u;
    result->shape_left[0] = result->size_left[0] = left_1->data.i;
    result->shape_right[0] = result->size_right[0] = right_1->data.i;
    result->shape_left[1] = result->size_left[1] = left_2->data.i;
    result->shape_right[1] = result->size_right[1] = right_2->data.i;
    result->shape_left[2] = result->size_left[2] = 0;
    result->shape_right[2] = result->size_right[2] = 0;
    int isz1 = 1 + right_1->data.i - left_1->data.i;
    int isz2 = 1 + right_2->data.i - left_2->data.i;
    if (isz1 < 0 || isz2 < 0) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    else {
        size_t sz = static_cast<size_t>(isz1 * isz2);
        if (sz) {
            __kumir_scalar * scalars = reinterpret_cast<__kumir_scalar*>(
                        calloc(sz, sizeof(__kumir_scalar))
                        );
            result->data = scalars;
            for (size_t i=0; i<sz; i++) {
                __kumir_create_undefined_scalar(&scalars[i]);
            }
        }
    }
}

EXTERN void __kumir_create_array_3(__kumir_array * result,
                                   const __kumir_scalar * left_1,
                                   const __kumir_scalar * right_1,
                                   const __kumir_scalar * left_2,
                                   const __kumir_scalar * right_2,
                                   const __kumir_scalar * left_3,
                                   const __kumir_scalar * right_3
                                   )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    __kumir_check_value_defined(left_2);
    __kumir_check_value_defined(right_2);
    __kumir_check_value_defined(left_3);
    __kumir_check_value_defined(right_3);
    result->dim = 3u;
    result->shape_left[0] = result->size_left[0] = left_1->data.i;
    result->shape_right[0] = result->size_right[0] = right_1->data.i;
    result->shape_left[1] = result->size_left[1] = left_2->data.i;
    result->shape_right[1] = result->size_right[1] = right_2->data.i;
    result->shape_left[2] = result->size_left[2] = left_3->data.i;
    result->shape_right[2] = result->size_right[2] = right_3->data.i;
    int isz1 = 1 + right_1->data.i - left_1->data.i;
    int isz2 = 1 + right_2->data.i - left_2->data.i;
    int isz3 = 1 + right_3->data.i - left_3->data.i;
    if (isz1 < 0 || isz2 < 0 || isz3 < 0) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    else {
        size_t sz = static_cast<size_t>(isz1 * isz2 * isz3);
        if (sz) {
            __kumir_scalar * scalars = reinterpret_cast<__kumir_scalar*>(
                        calloc(sz, sizeof(__kumir_scalar))
                        );
            result->data = scalars;
            for (size_t i=0; i<sz; i++) {
                __kumir_create_undefined_scalar(&scalars[i]);
            }
        }
    }
}

EXTERN void __kumir_create_array_ref_1(__kumir_array * result,
                                       const __kumir_scalar * left_1,
                                       const __kumir_scalar * right_1
                                       )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    if (left_1->data.i > right_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    result->shape_left[0] = IMAX(result->shape_left[0], left_1->data.i);
    result->shape_right[0] = IMIN(result->shape_right[0], right_1->data.i);
}

EXTERN void __kumir_create_array_ref_2(__kumir_array * result,
                                       const __kumir_scalar * left_1,
                                       const __kumir_scalar * right_1,
                                       const __kumir_scalar * left_2,
                                       const __kumir_scalar * right_2
                                       )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    __kumir_check_value_defined(left_2);
    __kumir_check_value_defined(right_2);
    if (right_1->data.i < left_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_2->data.i < left_2->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    result->shape_left[0] = IMAX(result->shape_left[0], left_1->data.i);
    result->shape_right[0] = IMIN(result->shape_right[0], right_1->data.i);
    result->shape_left[1] = IMAX(result->shape_left[1], left_2->data.i);
    result->shape_right[1] = IMIN(result->shape_right[1], right_2->data.i);
}

EXTERN void __kumir_create_array_ref_3(__kumir_array * result,
                                       const __kumir_scalar * left_1,
                                       const __kumir_scalar * right_1,
                                       const __kumir_scalar * left_2,
                                       const __kumir_scalar * right_2,
                                       const __kumir_scalar * left_3,
                                       const __kumir_scalar * right_3
                                       )
{
    __kumir_check_value_defined(left_1);
    __kumir_check_value_defined(right_1);
    __kumir_check_value_defined(left_2);
    __kumir_check_value_defined(right_2);
    __kumir_check_value_defined(left_3);
    __kumir_check_value_defined(right_3);
    if (right_1->data.i < left_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_2->data.i < left_2->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_3->data.i < left_3->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    result->shape_left[0] = IMAX(result->shape_left[0], left_1->data.i);
    result->shape_right[0] = IMIN(result->shape_right[0], right_1->data.i);
    result->shape_left[1] = IMAX(result->shape_left[1], left_2->data.i);
    result->shape_right[1] = IMIN(result->shape_right[1], right_2->data.i);
    result->shape_left[2] = IMAX(result->shape_left[2], left_3->data.i);
    result->shape_right[2] = IMIN(result->shape_right[2], right_3->data.i);
}

EXTERN void __kumir_create_array_copy_1(__kumir_array * result,
                                        const __kumir_scalar * left_1,
                                        const __kumir_scalar * right_1
                                        )
{    
    __kumir_create_array_ref_1(result, left_1, right_1);
    if (left_1->data.i < result->shape_left[0] || left_1->data.i > right_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_1->data.i < left_1->data.i || right_1->data.i > result->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    size_t start_pos = static_cast<size_t>(result->shape_left[0] - result->size_left[0]);
    size_t items_count = start_pos +
            static_cast<size_t>(result->shape_right[0] - result->shape_left[0] + 1);
    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(
                calloc(items_count, sizeof(__kumir_scalar))
                );
    const __kumir_scalar * const source = result->data + start_pos;
    memcpy(data, source, items_count * sizeof(__kumir_scalar));
    const __kumir_scalar & first = source[0];
    if (__KUMIR_STRING == first.type) {
        for (size_t i=0; i<items_count; i++) {
            const __kumir_scalar & from = source[i];
            __kumir_scalar & to = data[i];
            if (from.defined) {
                const size_t strl = wcslen(from.data.s);
                to.data.s = reinterpret_cast<wchar_t*>(calloc(strl+1, sizeof(wchar_t)));
                wcsncpy(to.data.s, from.data.s, strl);
                to.data.s[strl] = L'\0';
            }
        }
    }
    result->data = data;
    result->size_left[0] = result->shape_left[0];
    result->size_right[0] = result->shape_right[0];
}

EXTERN void __kumir_create_array_copy_2(__kumir_array * result,
                                        const __kumir_scalar * left_1,
                                        const __kumir_scalar * right_1,
                                        const __kumir_scalar * left_2,
                                        const __kumir_scalar * right_2
                                        )
{
    __kumir_create_array_ref_2(result, left_1, right_1, left_2, right_2);
    if (left_1->data.i < result->shape_left[0] || left_1->data.i > right_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_1->data.i < left_1->data.i || right_1->data.i > result->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (left_2->data.i < result->shape_left[1] || left_2->data.i > right_2->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_2->data.i < left_2->data.i || right_2->data.i > result->shape_right[1]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }

    const size_t size1 = static_cast<size_t>(
                1 + result->shape_right[0] - result->shape_left[0]
                );

    const size_t size2 = static_cast<size_t>(
                1 + result->shape_right[1] - result->shape_left[1]
                );

    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(
                calloc(size1 * size2, sizeof(__kumir_scalar))
                );

    const size_t start_pos1 = static_cast<size_t>(result->shape_left[0] - result->size_left[0]);
    const size_t start_pos2 = static_cast<size_t>(result->shape_left[1] - result->size_left[1]);

    for (size_t i=0u; i<size1; i++) {
        const size_t source_start_pos = start_pos1 * i + start_pos2;
        const size_t target_start_pos = size1 * i;
        const __kumir_scalar * const source = result->data + source_start_pos;
        __kumir_scalar * target = data + target_start_pos;
        memcpy(target, source, size2);
    }

    result->data = data;
    result->size_left[0] = result->shape_left[0];
    result->size_left[1] = result->shape_left[1];
    result->size_right[0] = result->shape_right[0];
    result->size_right[1] = result->shape_right[1];
}

EXTERN void __kumir_create_array_copy_3(__kumir_array * result,
                                        const __kumir_scalar * left_1,
                                        const __kumir_scalar * right_1,
                                        const __kumir_scalar * left_2,
                                        const __kumir_scalar * right_2,
                                        const __kumir_scalar * left_3,
                                        const __kumir_scalar * right_3
                                        )
{
    __kumir_create_array_ref_3(result, left_1, right_1, left_2, right_2, left_3, right_3);
    if (left_1->data.i < result->shape_left[0] || left_1->data.i > right_1->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_1->data.i < left_1->data.i || right_1->data.i > result->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (left_2->data.i < result->shape_left[1] || left_2->data.i > right_2->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_2->data.i < left_2->data.i || right_2->data.i > result->shape_right[1]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (left_3->data.i < result->shape_left[2] || left_3->data.i > right_3->data.i) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    if (right_3->data.i < left_3->data.i || right_3->data.i > result->shape_right[2]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Неверный размер таблицы"));
    }
    const size_t size1 = static_cast<size_t>(
                1 + result->shape_right[0] - result->shape_left[0]
                );

    const size_t size2 = static_cast<size_t>(
                1 + result->shape_right[1] - result->shape_left[1]
                );

    const size_t size3 = static_cast<size_t>(
                1 + result->shape_right[2] - result->shape_left[2]
                );

    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(
                calloc(size1 * size2 * size3, sizeof(__kumir_scalar))
                );

    const size_t start_pos1 = static_cast<size_t>(result->shape_left[0] - result->size_left[0]);
    const size_t start_pos2 = static_cast<size_t>(result->shape_left[1] - result->size_left[1]);
    const size_t start_pos3 = static_cast<size_t>(result->shape_left[2] - result->size_left[2]);

    for (size_t i=0u; i<size1; i++) {
        for (size_t j=0u; j<size2; j++) {
            const size_t source_start_pos = start_pos1 * i + start_pos2 * j + start_pos3;
            const size_t target_start_pos = (size2 * size1 * i) + size2 * j;
            const __kumir_scalar * const source = result->data + source_start_pos;
            __kumir_scalar * target = data + target_start_pos;
            memcpy(target, source, size2);
        }
    }

    result->data = data;
    result->size_left[0] = result->shape_left[0];
    result->size_left[1] = result->shape_left[1];
    result->size_left[2] = result->shape_left[2];
    result->size_right[0] = result->shape_right[0];
    result->size_right[1] = result->shape_right[1];
    result->size_right[2] = result->shape_right[2];
}


template<typename T>
static T __eat(const char ** pdata)
{
    const char * data = *pdata;
    const T * idata = reinterpret_cast<const T*> (data);
    T val = idata[0];
    idata++;
    data = reinterpret_cast<const char*>(idata);
    *pdata = data;
    return val;
}

static void __kumir_fill_array_1(__kumir_array array, const char *data, const __kumir_scalar_type type)
{
    const char * p = data;
    __kumir_int isz = __eat<__kumir_int>(&p);
    size_t index = 0u;
    __kumir_scalar * adata = reinterpret_cast<__kumir_scalar*>(array.data);
    while (isz) {
        adata[index].defined = __eat<__kumir_bool>(&p);
        adata[index].type = type;
        if (__KUMIR_INT == type) {
            adata[index].data.i = __eat<__kumir_int>(&p);
        }
        else if (__KUMIR_REAL == type) {
            adata[index].data.r = __eat<__kumir_real>(&p);
        }
        else if (__KUMIR_BOOL == type) {
            adata[index].data.b = __eat<__kumir_bool>(&p);
        }
        isz--;
        index++;
    }
}


EXTERN void __kumir_fill_array_i(__kumir_array array, const char * data)
{
    if      (1u == array.dim) __kumir_fill_array_1(array, data, __KUMIR_INT);
}

EXTERN void __kumir_fill_array_r(__kumir_array array, const char * data)
{
    if      (1u == array.dim) __kumir_fill_array_1(array, data, __KUMIR_REAL);
}

EXTERN void __kumir_fill_array_b(__kumir_array array, const char * data)
{
    if      (1u == array.dim) __kumir_fill_array_1(array, data, __KUMIR_BOOL);
}

EXTERN void __kumir_fill_array_c(__kumir_array array, const char * data)
{
    if      (1u == array.dim) __kumir_fill_array_1(array, data, __KUMIR_CHAR);
}

EXTERN void __kumir_fill_array_s(__kumir_array array, const char * data)
{
    if      (1u == array.dim) __kumir_fill_array_1(array, data, __KUMIR_STRING);
}

EXTERN void __kumir_get_array_1_element(__kumir_scalar ** result,
                                        bool value_expected,
                                        __kumir_array * array,
                                        const __kumir_scalar * x)
{
    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(array->data);
    __kumir_check_value_defined(x);
    __kumir_int xx = x->data.i;
    if (xx < array->shape_left[0] || xx > array->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else {
        const size_t index = static_cast<size_t>(xx - array->size_left[0]);
        if (value_expected) {
            __kumir_check_value_defined(&data[index]);
        }
        *result = &data[index];
    }
}

EXTERN void __kumir_get_array_2_element(__kumir_scalar ** result,
                                        bool value_expected,
                                        __kumir_array * array,
                                        const __kumir_scalar * y,
                                        const __kumir_scalar * x)
{
    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(array->data);
    __kumir_check_value_defined(x);
    __kumir_check_value_defined(y);
    __kumir_int xx = x->data.i;
    __kumir_int yy = y->data.i;
    if (yy < array->shape_left[0] || yy > array->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else if (xx < array->shape_left[1] || xx > array->shape_right[1]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else {
        const size_t size_y = static_cast<size_t>(
                    1 + array->size_right[0] - array->size_left[0]
                    );
        const size_t index = static_cast<size_t>(
                    ( size_y * (yy - array->size_left[0]) ) +
                    ( xx - array->size_left[1] )
                );
        if (value_expected) {
            __kumir_check_value_defined(&data[index]);
        }
        *result = &data[index];
    }
}

EXTERN void __kumir_get_array_3_element(__kumir_scalar ** result,
                                        bool value_expected,
                                        __kumir_array * array,
                                        const __kumir_scalar * z,
                                        const __kumir_scalar * y,
                                        const __kumir_scalar * x)
{
    __kumir_scalar * data = reinterpret_cast<__kumir_scalar*>(array->data);
    __kumir_check_value_defined(x);
    __kumir_check_value_defined(y);
    __kumir_check_value_defined(z);
    __kumir_int xx = x->data.i;
    __kumir_int yy = y->data.i;
    __kumir_int zz = z->data.i;
    if (zz < array->shape_left[0] || zz > array->shape_right[0]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else if (yy < array->shape_left[1] || yy > array->shape_right[1]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else if (xx < array->shape_left[2] || xx > array->shape_right[2]) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Выход за границу таблицы"));
    }
    else {
        const size_t size_z = static_cast<size_t>(
                    1 + array->size_right[0] - array->size_left[0]
                    );
        const size_t size_y = static_cast<size_t>(
                    1 + array->size_right[1] - array->size_left[1]
                    );
        const size_t index = static_cast<size_t>(
                    ( size_z * size_y * (zz - array->size_left[0]) ) +
                    ( size_y * (yy - array->size_left[1]) ) +
                    ( xx - array->size_left[2] )
                );
        if (value_expected) {
            __kumir_check_value_defined(&data[index]);
        }
        *result = &data[index];
    }
}


EXTERN void __kumir_link_array(__kumir_array * result, const __kumir_array * from)
{
    memcpy(result, from, sizeof(__kumir_array));
}

EXTERN void __kumir_free_array(__kumir_array * array)
{
    if (array->dim > 0u && array->data) {
        const __kumir_scalar & first = array->data[0];
        if (__KUMIR_STRING == first.type) {
            size_t elems = 0u;
            elems = static_cast<size_t>(1 + array->size_right[0] - array->size_left[0]);
            if (array->dim > 1u) {
                elems *= static_cast<size_t>(1 + array->size_right[1] - array->size_left[1]);
            }
            if (array->dim > 2u) {
                elems *= static_cast<size_t>(1 + array->size_right[2] - array->size_left[2]);
            }
            for (size_t i=0; i<elems; i++) {
                __kumir_free_scalar(&(array->data[i]));
            }
        }
        free(array->data);
    }
}

EXTERN void __kumir_get_string_slice_ref(__kumir_stringref * result,
                                     __kumir_scalar ** sptr,
                                     const __kumir_scalar * from,
                                     const __kumir_scalar * to)
{
    __kumir_scalar * sval = *sptr;
    __kumir_check_value_defined(sval);
    __kumir_check_value_defined(from);
    __kumir_check_value_defined(to);
    const std::wstring s = __kumir_scalar_as_wstring(sval);
    const int kfrom = from->data.i;
    const int kto = to->data.i;
    const int klength = s.length();
    result->ref = sval;
    if (kto < kfrom && kfrom == 0) {
        result->from = result->length = 0u;
        result->op = __KUMIR_STRINGREF_PREPEND;
    }
    else if (kfrom > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Левая граница вырезки за пределами строки"));
    }
    else if (kto < kfrom && kfrom > 0) {
        result->from = static_cast<size_t>(kfrom);
        result->length = 0u;
        result->op = __KUMIR_STRINGREF_INSERT;
    }
    else if (kfrom == klength + 1 && kto <= kfrom) {
        result->from = result->length = 0u;
        result->op = __KUMIR_STRINGREF_APPEND;
    }
    else if (kto < 1 || kto > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Правая граница вырезки за пределами строки"));
    }
    else if (kfrom < 1 || kfrom > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Левая граница вырезки за пределами строки"));
    }
    else if (kto < kfrom) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Ошибка в границах вырезки"));
    }
    else {
        result->from = static_cast<size_t>(kfrom - 1);
        result->length = static_cast<size_t>(kto - kfrom);
        result->op = __KUMIR_STRINGREF_REPLACE;
    }
}

EXTERN void __kumir_get_string_element_ref(__kumir_stringref * result,
                                     __kumir_scalar ** sptr,
                                     const __kumir_scalar * at)
{
    __kumir_scalar * sval = *sptr;
    __kumir_check_value_defined(sval);
    __kumir_check_value_defined(at);
    const std::wstring s = __kumir_scalar_as_wstring(sval);
    const int kindex = at->data.i;
    const int klength = s.length();
    if (kindex < 1) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Индекс символа меньше 1"));
    }
    else if (kindex > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Индекс символа больше длины строки"));
    }
    else {
        const size_t index = static_cast<size_t>(kindex - 1);
        result->ref = sval;
        result->op = __KUMIR_STRINGREF_REPLACE;
        result->from = index;
        result->length = 1u;
    }
}

EXTERN void __kumir_get_string_slice(__kumir_scalar * result,
                                     const __kumir_scalar ** sptr,
                                     const __kumir_scalar * from,
                                     const __kumir_scalar * to)
{
    const __kumir_scalar * sval = *sptr;
    __kumir_check_value_defined(sval);
    __kumir_check_value_defined(from);
    __kumir_check_value_defined(to);
    const std::wstring s = __kumir_scalar_as_wstring(sval);
    const int kfrom = from->data.i;
    const int kto = to->data.i;
    const int klength = s.length();
    std::wstring res;
    if (kfrom < 1 || kfrom > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Левая граница вырезки за пределами строки"));
    }
    else if (kto < kfrom) {
        // keep empty string
    }
    else if (kto < 1 || kto > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Правая граница вырезки за пределами строки"));
    }
    else {
        const size_t index_from = static_cast<size_t>(kfrom - 1);
        const size_t res_length = static_cast<size_t>(kto - kfrom + 1);
        res = s.substr(index_from, res_length);
    }
    __kumir_create_string(result, res);
}

EXTERN void __kumir_get_string_element(__kumir_scalar * result,
                                       const __kumir_scalar ** sptr,
                                       const __kumir_scalar * at)
{
    const __kumir_scalar * sval = *sptr;
    __kumir_check_value_defined(sval);
    __kumir_check_value_defined(at);
    const std::wstring s = __kumir_scalar_as_wstring(sval);
    const int kindex = at->data.i;
    const int klength = s.length();
    if (kindex < 1) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Индекс символа меньше 1"));
    }
    else if (kindex > klength) {
        Kumir::Core::abort(Kumir::Core::fromUtf8("Индекс символа больше длины строки"));
    }
    else {
        const size_t index = static_cast<size_t>(kindex - 1);
        const wchar_t ch = s.at(index);
        result->defined = true;
        result->type = __KUMIR_CHAR;
        result->data.c = ch;
    }
}

static std::stack<int32_t> for_counters;

EXTERN void __kumir_loop_for_from_to_init_counter(const __kumir_scalar * from)
{
    int32_t val = from->data.i - 1;
    for_counters.push(val);
}

EXTERN void __kumir_loop_for_from_to_step_init_counter(const __kumir_scalar * from, const __kumir_scalar * step)
{
    int32_t val = from->data.i - step->data.i;
    for_counters.push(val);
}

EXTERN __kumir_bool __kumir_loop_for_from_to_check_counter(__kumir_scalar * variable, const __kumir_scalar * from, const __kumir_scalar * to)
{
    int32_t & i = for_counters.top();
    i += 1;
    int32_t f = from->data.i;
    int32_t t = to->data.i;
    bool result = f <= i && i <= t;
    if (result) {
        variable->data.i = i;
        variable->defined = true;
        variable->type = __KUMIR_INT;
    }
    return result;
}

EXTERN __kumir_bool __kumir_loop_for_from_to_step_check_counter(__kumir_scalar * variable, const __kumir_scalar * from, const __kumir_scalar * to, const __kumir_scalar * step)
{
    int32_t & i = for_counters.top();
    int32_t f = from->data.i;
    int32_t t = to->data.i;
    int32_t s = step->data.i;
    i += s;
    bool result = s >= 0
            ? f <= i && i <= t
            : t <= i && i <= f;
    if (result) {
        variable->data.i = i;
        variable->defined = true;
        variable->type = __KUMIR_INT;
    }
    return result;
}

EXTERN void __kumir_loop_times_init_counter(const __kumir_scalar * from)
{
    int32_t val = from->data.i;
    for_counters.push(val);
}

EXTERN __kumir_bool __kumir_loop_times_check_counter()
{
    int32_t & times = for_counters.top();
    if (times > 0) {
        times --;
        return true;
    }
    else {
        return false;
    }
}

EXTERN void __kumir_loop_end_counter()
{
    for_counters.pop();
}


EXTERN void __kumir_stdlib_init()
{
    Kumir::initStandardLibrary();
}
