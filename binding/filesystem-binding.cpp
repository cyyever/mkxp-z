/*
 ** filesystem-binding.cpp
 **
 ** This file is part of mkxp.
 **
 ** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
 **
 ** mkxp is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** mkxp is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iterator>
#include <limits>
#include <utility>

#include "binding-util.h"

#include "filesystem.h"
#include <SDL3/SDL_iostream.h>
#include "sharedstate.h"

#include "ruby/encoding.h"
#include "ruby/intern.h"

#include <ruby/thread.h>

struct LoadDataArgs {
    SDL_IOStream *stream;
    bool raw;
    void *bytes = nullptr;
    size_t size = 0;
    bool tooLarge = false;
    const char *failure = nullptr;
    char error[256]{};
};

static void *loadDataWithoutGVL(void *opaque) {
    auto &args = *static_cast<LoadDataArgs *>(opaque);
    args.tooLarge = SDL_GetIOSize(args.stream) > (std::numeric_limits<long>::max)();
    if (!args.tooLarge) {
        args.bytes = SDL_LoadFile_IO(args.stream, &args.size, false);
        const auto status = SDL_GetIOStatus(args.stream);
        if (!args.bytes || (status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF)) {
            args.failure = "Cannot read stream";
            SDL_strlcpy(args.error, SDL_GetError(), sizeof(args.error));
        }
    }
    if (!SDL_CloseIO(std::exchange(args.stream, nullptr))) {
        args.failure = "Cannot close stream";
        SDL_strlcpy(args.error, SDL_GetError(), sizeof(args.error));
    }
    return nullptr;
}

static VALUE readData(VALUE opaque) {
    auto &args = *reinterpret_cast<LoadDataArgs *>(opaque);
    rb_thread_call_without_gvl(loadDataWithoutGVL, &args, nullptr, nullptr);
    if (args.failure)
        rb_raise(rb_eIOError, "%s: %s", args.failure, args.error);
    if (args.tooLarge || args.size > static_cast<size_t>((std::numeric_limits<long>::max)()))
        rb_raise(rb_eRangeError, "stream is too large for a Ruby string");
    VALUE data = rb_str_new(static_cast<const char *>(args.bytes), static_cast<long>(args.size));
    SDL_free(std::exchange(args.bytes, nullptr));
    if (args.raw)
        return data;
    VALUE marshal = rb_const_get(rb_cObject, rb_intern("Marshal"));
    VALUE result = rb_funcall(marshal, rb_intern("load"), 1, data);
    RB_GC_GUARD(data);
    return result;
}

static VALUE closeData(VALUE opaque) {
    auto &args = *reinterpret_cast<LoadDataArgs *>(opaque);
    SDL_free(std::exchange(args.bytes, nullptr));
    if (args.stream && !SDL_CloseIO(std::exchange(args.stream, nullptr)))
        rb_raise(rb_eIOError, "Cannot close stream: %s", SDL_GetError());
    return Qnil;
}

VALUE kernelLoadDataInt(const char *filename, bool /* rubyExc */, bool raw) {
    // Only trivial state crosses Ruby calls: rb_ensure owns native IO/buffers
    // even when allocation, Marshal hooks, or GVL reacquisition raises.
    LoadDataArgs args{shState->fileSystem().openReadRaw(filename), raw};
    return rb_ensure(readData, reinterpret_cast<VALUE>(&args), closeData, reinterpret_cast<VALUE>(&args));
}

RB_METHOD_GUARD(kernelLoadData) {
    RB_UNUSED_PARAM;
    
    VALUE filename;
    VALUE raw;
    rb_scan_args(argc, argv, "11", &filename, &raw);
    SafeStringValue(filename);
    
    bool rawv;
    rb_bool_arg(raw, &rawv);
    return kernelLoadDataInt(RSTRING_PTR(filename), true, rawv);
}
RB_METHOD_GUARD_END

static VALUE writeData(VALUE arguments) {
    VALUE marshal = rb_const_get(rb_cObject, rb_intern("Marshal"));
    return rb_funcall(marshal, rb_intern("dump"), 2,
                      rb_ary_entry(arguments, 0), rb_ary_entry(arguments, 1));
}

static VALUE closeRubyFile(VALUE file) {
    return rb_io_close(file);
}

RB_METHOD(kernelSaveData) {
    RB_UNUSED_PARAM;
    VALUE object, filename;
    rb_get_args(argc, argv, "oS", &object, &filename RB_ARG_END);
    VALUE arguments = rb_ary_new_from_args(2, object, Qnil);
    VALUE file = rb_file_open_str(filename, "wb");
    rb_ary_store(arguments, 1, file);
    rb_ensure(writeData, arguments, closeRubyFile, file);
    return Qnil;
}

static VALUE stringForceUTF8(RB_BLOCK_CALL_FUNC_ARGLIST(arg, callback_arg))
{
    if (RB_TYPE_P(arg, RUBY_T_STRING) && ENCODING_IS_ASCII8BIT(arg))
        rb_enc_associate_index(arg, rb_utf8_encindex());
    
    return arg;
}


RB_METHOD(_marshalLoad) {
    RB_UNUSED_PARAM;
    VALUE port;
    rb_get_args(argc, argv, "o", &port RB_ARG_END);
    
    VALUE utf8Proc;
    utf8Proc = rb_proc_new(stringForceUTF8, Qnil);
    
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    
    VALUE v[] = {port, utf8Proc};
    return rb_funcall2(marsh, rb_intern("_mkxp_load_alias"), std::size(v), v);
}

void fileIntBindingInit() {
    _rb_define_module_function(rb_mKernel, "load_data", kernelLoadData);
    _rb_define_module_function(rb_mKernel, "save_data", kernelSaveData);
    
    /* We overload the built-in 'Marshal::load()' function to silently
     * insert our utf8proc that ensures all read strings will be
     * UTF-8 encoded */
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    rb_define_alias(rb_singleton_class(marsh), "_mkxp_load_alias", "load");
    _rb_define_module_function(marsh, "load", _marshalLoad);
}
