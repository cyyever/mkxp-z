#include "json-binding.h"

#include <ruby/encoding.h>
#include <algorithm>
#include <exception>
#include <vector>

namespace {
using json_binding::call;

struct ActiveContainer {
    std::vector<VALUE> &active;
    ActiveContainer(std::vector<VALUE> &active, VALUE value) : active(active) {
        if (std::find(active.begin(), active.end(), value) != active.end())
            throw json_binding::ConversionError("Cyclic arrays/hashes cannot be JSON settings");
        active.push_back(value);
    }
    ~ActiveContainer() { active.pop_back(); }
};

json5pp::value toJson(VALUE value, std::vector<VALUE> &active) {
    if (NIL_P(value))
        return nullptr;
    if (value == Qtrue || value == Qfalse)
        return value == Qtrue;
    if (RB_INTEGER_TYPE_P(value))
        // json5pp's native integer storage is int. Let Ruby report RangeError
        // for larger integers rather than silently truncate or round them.
        return call([&] { return NUM2INT(value); });
    if (RB_TYPE_P(value, T_FLOAT))
        return RFLOAT_VALUE(value);
    if (RB_TYPE_P(value, T_STRING))
        return std::string(RSTRING_PTR(value), RSTRING_LEN(value));
    if (RB_TYPE_P(value, T_ARRAY)) {
        ActiveContainer visiting(active, value);
        auto result = json5pp::array({});
        for (long index = 0; index < RARRAY_LEN(value); ++index)
            result.as_array().push_back(toJson(rb_ary_entry(value, index), active));
        RB_GC_GUARD(value);
        return result;
    }
    if (RB_TYPE_P(value, T_HASH)) {
        ActiveContainer visiting(active, value);
        auto result = json5pp::object({});
        struct Context {
            json5pp::value &result;
            std::vector<VALUE> &active;
            std::exception_ptr error;
        } context{result, active, {}};
        call([&] {
            rb_hash_foreach(value, [](VALUE key, VALUE item, VALUE opaque) -> int {
                auto &context = *reinterpret_cast<Context *>(opaque);
                try {
                    VALUE name = call([&] { return rb_str_to_str(key); });
                    std::string nativeName(RSTRING_PTR(name), RSTRING_LEN(name));
                    context.result.as_object().emplace(std::move(nativeName), toJson(item, context.active));
                    RB_GC_GUARD(name);
                } catch (...) {
                    // No C++ exception may cross the Ruby hash iterator frame.
                    context.error = std::current_exception();
                    return ST_STOP;
                }
                return ST_CONTINUE;
            }, reinterpret_cast<VALUE>(&context));
            return Qnil;
        });
        if (context.error)
            std::rethrow_exception(context.error);
        RB_GC_GUARD(value);
        return result;
    }
    throw json_binding::ConversionError("JSON settings require nil, booleans, numbers, strings, arrays or string-keyed hashes");
}
} // namespace

json5pp::value rb2json(VALUE value) {
    std::vector<VALUE> active;
    return toJson(value, active);
}

VALUE json2rb(const json5pp::value &value) {
    using json_binding::call;
    if (value.is_null())
        return Qnil;
    // is_number() includes integers, so test the integer branch first.
    if (value.is_integer())
        return call([&] { return INT2NUM(value.as_integer()); });
    if (value.is_number())
        return call([&] { return rb_float_new(value.as_number()); });
    if (value.is_boolean())
        return value.as_boolean() ? Qtrue : Qfalse;
    if (value.is_string()) {
        const auto &string = value.as_string();
        return call([&] { return rb_utf8_str_new(string.data(), static_cast<long>(string.size())); });
    }
    if (value.is_array()) {
        VALUE result = call([] { return rb_ary_new(); });
        for (const auto &item : value.as_array()) {
            VALUE converted = json2rb(item);
            call([&] { return rb_ary_push(result, converted); });
        }
        RB_GC_GUARD(result);
        return result;
    }
    VALUE result = call([] { return rb_hash_new(); });
    for (const auto &[name, item] : value.as_object()) {
        VALUE key = call([&] { return rb_utf8_str_new(name.data(), static_cast<long>(name.size())); });
        VALUE converted = json2rb(item);
        call([&] { return rb_hash_aset(result, key, converted); });
        RB_GC_GUARD(key);
    }
    RB_GC_GUARD(result);
    return result;
}
