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

#include "binding-util.h"

#include "filesystem.h"
#include "sharedstate.h"

#include "ruby/encoding.h"
#include "ruby/intern.h"

#include <ruby/thread.h>

static void fileIntFreeInstance(void *inst) {
    SDL_IOStream *ops = static_cast<SDL_IOStream *>(inst);
    
    if (ops)
        SDL_CloseIO(ops);
}

DEF_TYPE_CUSTOMFREE(FileInt, fileIntFreeInstance);

static VALUE fileIntForPath(const char *path) {
    VALUE klass = rb_const_get(rb_cObject, rb_intern("FileInt"));
    
    VALUE obj = rb_obj_alloc(klass);
    
    SDL_IOStream *ops = shState->fileSystem().openReadRaw(path);

    setPrivateData(obj, ops);
    
    return obj;
}

static SDL_IOStream *fileIntStream(VALUE self) {
    auto *ops = getPrivateDataNoRaise<SDL_IOStream>(self);
    if (!ops)
        rb_raise(rb_eIOError, "closed stream");
    return ops;
}

struct FileIntReadArgs {
    SDL_IOStream *ops;
    void *destination;
    size_t requested;
    size_t count = 0;
    SDL_IOStatus status = SDL_IO_STATUS_READY;
    char error[256]{};
};

static void *fileIntReadInto(void *opaque) {
    auto &args = *static_cast<FileIntReadArgs *>(opaque);
    args.count = SDL_ReadIO(args.ops, args.destination, args.requested);
    args.status = SDL_GetIOStatus(args.ops);
    if (args.status != SDL_IO_STATUS_READY && args.status != SDL_IO_STATUS_EOF)
        SDL_strlcpy(args.error, SDL_GetError(), sizeof(args.error));
    return nullptr;
}

RB_METHOD(fileIntRead) {
    VALUE requested = Qnil;
    rb_scan_args(argc, argv, "01", &requested);
    const bool readAll = NIL_P(requested);
    auto *ops = fileIntStream(self);
    long length;
    if (readAll) {
        const Sint64 position = SDL_TellIO(ops);
        const Sint64 size = SDL_GetIOSize(ops);
        if (position < 0 || size < 0)
            rb_raise(rb_eIOError, "Cannot determine remaining stream size: %s", SDL_GetError());
        const Sint64 remaining = size > position ? size - position : 0;
        if (remaining > (std::numeric_limits<long>::max)())
            rb_raise(rb_eRangeError, "stream is too large for a Ruby string");
        length = static_cast<long>(remaining);
    } else {
        length = NUM2LONG(requested);
        if (length < 0)
            rb_raise(rb_eArgError, "negative length %ld given", length);
    }
    if (length == 0)
        return rb_str_new(nullptr, 0);

    VALUE data = rb_str_new(nullptr, length);
    FileIntReadArgs args{ops, RSTRING_PTR(data), static_cast<size_t>(length)};
    rb_thread_call_without_gvl(fileIntReadInto, &args, nullptr, nullptr);
    if (args.status != SDL_IO_STATUS_READY && args.status != SDL_IO_STATUS_EOF)
        rb_raise(rb_eIOError, "Cannot read stream: %s", args.error);
    rb_str_set_len(data, static_cast<long>(args.count));
    RB_GC_GUARD(self);
    return args.count == 0 && !readAll ? Qnil : data;
}

RB_METHOD(fileIntClose) {
    RB_UNUSED_PARAM;
    auto *ops = getPrivateDataNoRaise<SDL_IOStream>(self);
    // setPrivateData would invoke dfree. Detach first so explicit close and
    // finalization cannot close the same opaque SDL3 stream twice.
    RTYPEDDATA_DATA(self) = nullptr;
    if (ops && !SDL_CloseIO(ops))
        rb_raise(rb_eIOError, "Cannot close stream: %s", SDL_GetError());
    return Qnil;
}

RB_METHOD(fileIntGetByte) {
    RB_UNUSED_PARAM;
    auto *ops = fileIntStream(self);
    unsigned char byte;
    if (SDL_ReadIO(ops, &byte, 1) == 1)
        return INT2NUM(byte);
    const auto status = SDL_GetIOStatus(ops);
    if (status != SDL_IO_STATUS_READY && status != SDL_IO_STATUS_EOF)
        rb_raise(rb_eIOError, "Cannot read stream: %s", SDL_GetError());
    return Qnil;
}

RB_METHOD(fileIntBinmode) {
    RB_UNUSED_PARAM;
    
    return Qnil;
}


struct LoadDataArgs { VALUE port; bool raw; };

static VALUE readData(VALUE opaque) {
    const auto &args = *reinterpret_cast<LoadDataArgs *>(opaque);
    VALUE data = fileIntRead(0, nullptr, args.port);
    if (args.raw)
        return data;
    VALUE marshal = rb_const_get(rb_cObject, rb_intern("Marshal"));
    return rb_funcall(marshal, rb_intern("load"), 1, data);
}

static VALUE closeData(VALUE port) {
    return fileIntClose(0, nullptr, port);
}

VALUE kernelLoadDataInt(const char *filename, bool /* rubyExc */, bool raw) {
    VALUE port = fileIntForPath(filename);
    LoadDataArgs args{port, raw};
    return rb_ensure(readData, reinterpret_cast<VALUE>(&args), closeData, port);
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
    VALUE klass = rb_define_class("FileInt", rb_cIO);
    rb_define_alloc_func(klass, classAllocate<&FileIntType>);
    
    _rb_define_method(klass, "read", fileIntRead);
    _rb_define_method(klass, "getbyte", fileIntGetByte);
    _rb_define_method(klass, "binmode", fileIntBinmode);
    _rb_define_method(klass, "close", fileIntClose);
    
    _rb_define_module_function(rb_mKernel, "load_data", kernelLoadData);
    _rb_define_module_function(rb_mKernel, "save_data", kernelSaveData);
    
    /* We overload the built-in 'Marshal::load()' function to silently
     * insert our utf8proc that ensures all read strings will be
     * UTF-8 encoded */
    VALUE marsh = rb_const_get(rb_cObject, rb_intern("Marshal"));
    rb_define_alias(rb_singleton_class(marsh), "_mkxp_load_alias", "load");
    _rb_define_module_function(marsh, "load", _marshalLoad);
}
