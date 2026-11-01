// Compile the production Ruby typed-data helpers under the engine's C++ standard.
#include "binding-util.h"
#include <vector>

extern "C" void Init_json_settings_test();

struct Payload { int value = 42; };
DEF_TYPE(Payload);

static VALUE verifyNativeClass(VALUE) {
    VALUE original = rb_define_class("NativeReplacement", rb_cObject);
    VALUE replacement = rb_define_class("NativeReplacement", rb_cArray);
    if (original == replacement || rb_class_superclass(replacement) != rb_cArray)
        rb_raise(rb_eRuntimeError, "native RGSS class replacement failed");
    return Qtrue;
}

static VALUE verifyTypedData(VALUE) {
    VALUE object = rb_data_typed_object_wrap(rb_cObject, new Payload, &PayloadType);
    Payload *payload;
    TypedData_Get_Struct(object, Payload, &PayloadType, payload);
    return INT2NUM(payload->value);
}

int main(int argc, char **argv) {
    ruby_sysinit(&argc, &argv);
    RUBY_INIT_STACK;
    ruby_init();
    Init_json_settings_test();
    rb_define_global_function("verify_native_class", RUBY_METHOD_FUNC(verifyNativeClass), 0);
    rb_define_global_function("verify_typed_data", RUBY_METHOD_FUNC(verifyTypedData), 0);
    char noGems[] = "--disable-gems";
    std::vector<char *> args{argv[0], noGems};
    args.insert(args.end(), argv + 1, argv + argc);
    return ruby_run_node(ruby_options(static_cast<int>(args.size()), args.data()));
}
