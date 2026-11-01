#ifndef MKXP_JSON_BINDING_H
#define MKXP_JSON_BINDING_H

#include <ruby.h>
#include "util/json5pp.hpp"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace json_binding {
// Ruby exceptions must unwind native containers before returning to the VM.
struct RubyJump { int state; };
struct ConversionError : std::invalid_argument {
    using std::invalid_argument::invalid_argument;
};

template<class Function>
auto call(Function &&function) {
    using Result = std::invoke_result_t<Function>;
    struct Context { Function &function; Result result{}; } context{function};
    int state = 0;
    rb_protect([](VALUE opaque) -> VALUE {
        auto &context = *reinterpret_cast<Context *>(opaque);
        context.result = context.function();
        return Qnil;
    }, reinterpret_cast<VALUE>(&context), &state);
    if (state)
        throw RubyJump{state};
    return context.result;
}

inline VALUE exception(VALUE klass, const char *message, int &state) {
    struct Context { VALUE klass; const char *message; } context{klass, message};
    return rb_protect([](VALUE opaque) -> VALUE {
        const auto &context = *reinterpret_cast<Context *>(opaque);
        return rb_exc_new_cstr(context.klass, context.message);
    }, reinterpret_cast<VALUE>(&context), &state);
}

// Call only at the Ruby entry point, with reference/primitive captures. Native
// temporaries inside the callable are destroyed before rb_jump_tag/rb_exc_raise.
template<class Function>
VALUE guard(Function &&function) {
    static_assert(std::is_trivially_destructible_v<Function>);
    int state = 0;
    VALUE error = Qnil;
    try {
        return function();
    } catch (const RubyJump &jump) {
        state = jump.state;
    } catch (const ConversionError &errorValue) {
        error = exception(rb_eTypeError, errorValue.what(), state);
    } catch (const std::range_error &errorValue) {
        error = exception(rb_eRangeError, errorValue.what(), state);
    } catch (const std::invalid_argument &errorValue) {
        error = exception(rb_eArgError, errorValue.what(), state);
    } catch (const std::exception &errorValue) {
        error = exception(rb_eRuntimeError, errorValue.what(), state);
    }
    if (state)
        rb_jump_tag(state);
    rb_exc_raise(error);
    return Qnil;
}
} // namespace json_binding

VALUE json2rb(const json5pp::value &value);
json5pp::value rb2json(VALUE value);

#endif
