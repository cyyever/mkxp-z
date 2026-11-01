// Most of the MiniFFI class was taken from Ruby 1.8's Win32API.c,
// it's just as basic but should work fine for the moment

#include "util/paths.h"

#include <SDL3/SDL.h>
#include <cstdint>

#include "filesystem/filesystem.h"
#include "miniffi.h"
#include "binding-util.h"

#include <ruby/thread.h>

#if defined(__linux__) || defined(SDL_PLATFORM_APPLE)
#define MVAL2RB(v) ULONG2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULONG(v)
#else
#if defined(_WIN64)
#define MVAL2RB(v) ULL2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2ULL(v)
#else
#define MVAL2RB(v) UINT2NUM(v)
#define RB2MVAL(v) (mffi_value)NUM2UINT(v)
#endif
#endif

#define _T_VOID 0
#define _T_NUMBER 1
#define _T_POINTER 2
#define _T_INTEGER 3
#define _T_BOOL 4

static void MiniFFI_Unload(void *handle) {
    SDL_UnloadObject(static_cast<SDL_SharedObject *>(handle));
}

DEF_TYPE_CUSTOMFREE(MiniFFI, MiniFFI_Unload);

static SDL_FunctionPointer MiniFFI_GetFunctionHandle(SDL_SharedObject *libhandle, const char *func) {
    if (!libhandle)
        return 0;
    return SDL_LoadFunction(libhandle, func);
}

static int miniFFIType(char code) {
    switch (code) {
        case 'V': case 'v': return _T_VOID;
        case 'N': case 'n': case 'L': case 'l': return _T_NUMBER;
        case 'P': case 'p': return _T_POINTER;
        case 'I': case 'i': return _T_INTEGER;
        case 'B': case 'b': return _T_BOOL;
        default:
            throw Exception(Exception::ArgumentError, "Invalid MiniFFI type code: %u",
                            static_cast<unsigned char>(code));
    }
}

// MiniFFI.new(library, function[, imports[, exports]])
// Yields itself in blocks

RB_METHOD_GUARD(MiniFFI_initialize) {
    VALUE libname, func, imports, exports;
    rb_scan_args(argc, argv, "22", &libname, &func, &imports, &exports);
    SafeStringValue(libname);
    SafeStringValue(func);
#ifdef SDL_PLATFORM_APPLE
    auto *hlib = SDL_LoadObject(paths::toUtf8((std::filesystem::current_path() / paths::fromRgss(RSTRING_PTR(libname))).lexically_normal().make_preferred().u8string()).c_str());
#else
    auto *hlib = SDL_LoadObject(RSTRING_PTR(libname));
#endif
    setPrivateData(self, hlib);
    auto hfunc = MiniFFI_GetFunctionHandle(hlib, RSTRING_PTR(func));
#ifdef SDL_PLATFORM_WIN32
    if (hlib && !hfunc) {
        VALUE func_a = rb_str_new3(func);
        func_a = rb_str_cat(func_a, "A", 1);
        hfunc = SDL_LoadFunction(hlib, RSTRING_PTR(func_a));
    }
#endif
    if (!hfunc)
        throw Exception(Exception::RuntimeError, "%s", SDL_GetError());
    
    rb_iv_set(self, "_func", MVAL2RB((mffi_value)hfunc));
    rb_iv_set(self, "_funcname", func);
    rb_iv_set(self, "_libname", libname);
    
    VALUE ary_imports = rb_ary_new();
    auto appendImport = [&](char code) {
        const int type = miniFFIType(code);
        // Win32API's V/v import marker denotes no argument.
        if (type == _T_VOID) return;
        if (RARRAY_LEN(ary_imports) >= MINIFFI_MAX_ARGS)
            throw Exception(Exception::ArgumentError, "Too many MiniFFI parameters (maximum %ld)", MINIFFI_MAX_ARGS);
        rb_ary_push(ary_imports, INT2FIX(type));
    };
    if (RB_TYPE_P(imports, RUBY_T_ARRAY)) {
        for (long i = 0; i < RARRAY_LEN(imports); ++i) {
            VALUE entry = rb_ary_entry(imports, i);
            SafeStringValue(entry);
            appendImport(*RSTRING_PTR(entry));
        }
    } else if (!NIL_P(imports)) {
        SafeStringValue(imports);
        for (long i = 0; i < RSTRING_LEN(imports); ++i)
            appendImport(RSTRING_PTR(imports)[i]);
    }
    rb_iv_set(self, "_imports", ary_imports);
    int exportType = _T_VOID;
    if (!NIL_P(exports)) {
        SafeStringValue(exports);
        exportType = miniFFIType(*RSTRING_PTR(exports));
    }
    rb_iv_set(self, "_exports", INT2FIX(exportType));
    if (rb_block_given_p())
        rb_yield(self);
    return Qnil;
}
RB_METHOD_GUARD_END

typedef struct {
    MINIFFI_FUNC function;
    MiniFFIFuncArgs *args;
    int nparams;
} MFFICallCBArgs;

void* miniffi_call_cb(void *args) {
    MFFICallCBArgs *a = (MFFICallCBArgs*)args;
    return (void*)miniffi_call_intern(a->function, a->args, a->nparams);
    }

RB_METHOD_GUARD(MiniFFI_call) {
    MiniFFIFuncArgs param{};
#define params param.params
    VALUE func = rb_iv_get(self, "_func");
    VALUE own_imports = rb_iv_get(self, "_imports");
    VALUE own_exports = rb_iv_get(self, "_exports");
    MINIFFI_FUNC ApiFunction = (MINIFFI_FUNC)RB2MVAL(func);
    VALUE args;
    int items = rb_scan_args(argc, argv, "0*", &args);
    int nimport = RARRAY_LEN(own_imports);
    if (items != nimport)
        throw Exception(Exception::RuntimeError,
                 "wrong number of parameters: expected %d, got %d", nimport, items);
    
    for (int i = 0; i < nimport; i++) {
        VALUE str = rb_ary_entry(args, i);
        mffi_value lParam = 0;
        switch (FIX2INT(rb_ary_entry(own_imports, i))) {
            case _T_POINTER:
                if (NIL_P(str)) {
                    lParam = 0;
                } else if (FIXNUM_P(str)) {
                    lParam = RB2MVAL(str);
                } else {
                    StringValue(str);
                    rb_str_modify(str);
                    lParam = (mffi_value)RSTRING_PTR(str);
                }
                break;
                
            case _T_BOOL: {
                bool value = false;
                rb_bool_arg(rb_ary_entry(args, i), &value);
                lParam = value ? 1 : 0;
                break;
            }
                
            case _T_INTEGER:
#if INTPTR_MAX == INT64_MAX
                lParam = RB2MVAL(rb_ary_entry(args, i)) & UINT32_MAX;
                break;
#endif
            case _T_NUMBER:
            default:
                lParam = RB2MVAL(rb_ary_entry(args, i));
                break;
        }
        params[i] = lParam;
    }
    MFFICallCBArgs cb_args {ApiFunction, &param, nimport};
    mffi_value ret = (mffi_value)rb_thread_call_without_gvl(miniffi_call_cb, &cb_args, 0, 0);
    
    switch (FIX2INT(own_exports)) {
        case _T_NUMBER:
        case _T_INTEGER:
            return MVAL2RB(ret);
            
        case _T_POINTER:
            return rb_utf8_str_new_cstr((char *)ret);
            
        case _T_BOOL:
            return rb_bool_new(ret);
            
        case _T_VOID:
        default:
            return MVAL2RB(0);
    }
}
RB_METHOD_GUARD_END

void MiniFFIBindingInit() {
    VALUE cMiniFFI = rb_define_class("MiniFFI", rb_cObject);
    rb_define_alloc_func(cMiniFFI, classAllocate<&MiniFFIType>);
    _rb_define_method(cMiniFFI, "initialize", MiniFFI_initialize);
    _rb_define_method(cMiniFFI, "call", MiniFFI_call);
    rb_define_alias(cMiniFFI, "Call", "call");
    
    rb_define_const(rb_cObject, "Win32API", cMiniFFI);
}
