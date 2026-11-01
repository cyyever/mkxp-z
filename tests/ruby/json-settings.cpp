#include "json-binding.h"
#include <ruby/encoding.h>

static int activeScopes = 0;
struct Scope {
    Scope() { ++activeScopes; }
    ~Scope() { --activeScopes; }
};
static VALUE roundTrip(VALUE, VALUE input) {
    return json_binding::guard([&] {
        Scope scope;
        const auto json = rb2json(input);
        return json2rb(json);
    });
}
static VALUE parse(VALUE, VALUE input) {
    StringValue(input);
    return json_binding::guard([&] {
        Scope scope;
        const auto json = json5pp::parse5(RSTRING_PTR(input), RSTRING_LEN(input));
        return json2rb(json);
    });
}
static VALUE serializedRoundTrip(VALUE, VALUE input) {
    return json_binding::guard([&] {
        Scope scope;
        const auto json = rb2json(input);
        const auto serialized = json.stringify5(json5pp::rule::space_indent<>());
        const auto parsed = json5pp::parse5(serialized.data(), serialized.size());
        return json2rb(parsed);
    });
}
static VALUE active(VALUE) { return INT2NUM(activeScopes); }

#ifdef _WIN32
#define JSON_TEST_EXPORT __declspec(dllexport)
#else
#define JSON_TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" JSON_TEST_EXPORT void Init_json_settings_test() {
    rb_define_global_function("json_settings_round_trip", RUBY_METHOD_FUNC(roundTrip), 1);
    rb_define_global_function("json_settings_parse", RUBY_METHOD_FUNC(parse), 1);
    rb_define_global_function("json_settings_serialized_round_trip", RUBY_METHOD_FUNC(serializedRoundTrip), 1);
    rb_define_global_function("json_settings_active_scopes", RUBY_METHOD_FUNC(active), 0);
}
