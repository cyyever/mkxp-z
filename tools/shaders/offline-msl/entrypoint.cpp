#include <spirv_cross_c.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Offline metadata companion for SDL_shadercross's MSL producer. Query the
// same official SPIRV-Cross API used by SDL_shadercross instead of guessing
// that its exported Metal function retains the original HLSL name.
#ifdef _WIN32
int wmain(int argc, wchar_t **argv) try {
#else
int main(int argc, char **argv) try {
#endif
    if (argc != 5) throw std::runtime_error("usage: mkxp-msl-entrypoint input.spirv entry vertex|fragment output.json");
    const auto check = [](spvc_result result) {
        if (result != SPVC_SUCCESS) throw std::runtime_error("SPIRV-Cross reflection failed");
    };
    const auto identifier = [](std::string_view value) {
        return !value.empty() && value.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") == value.npos;
    };
    const auto entry = std::filesystem::path(argv[2]).string();
    std::ifstream input(std::filesystem::path(argv[1]), std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (length <= 0 || length % 4) throw std::runtime_error("Invalid SPIR-V input");
    std::vector<SpvId> words(static_cast<size_t>(length) / 4);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char *>(words.data()), length)) throw std::runtime_error("SPIR-V read failed");
    spvc_context raw = nullptr;
    check(spvc_context_create(&raw));
    const std::unique_ptr<spvc_context_s, decltype(&spvc_context_destroy)> context(raw, spvc_context_destroy);
    spvc_parsed_ir ir = nullptr;
    check(spvc_context_parse_spirv(raw, words.data(), words.size(), &ir));
    spvc_compiler compiler = nullptr;
    check(spvc_context_create_compiler(raw, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler));
    const auto stage = std::filesystem::path(argv[3]).string();
    if (stage != "vertex" && stage != "fragment") throw std::runtime_error("Invalid stage");
    const auto model = stage == "vertex" ? SpvExecutionModelVertex : SpvExecutionModelFragment;
    check(spvc_compiler_set_entry_point(compiler, entry.c_str(), model));
    spvc_compiler_options options = nullptr;
    check(spvc_compiler_create_compiler_options(compiler, &options));
    check(spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, 20000));
    check(spvc_compiler_install_compiler_options(compiler, options));
    const char *translated = nullptr;
    check(spvc_compiler_compile(compiler, &translated));
    const char *name = spvc_compiler_get_cleansed_entry_point_name(compiler, entry.c_str(), model);
    if (!name || !identifier(name) || !identifier(entry)) throw std::runtime_error("Invalid reflected entrypoint");
    std::ofstream output(std::filesystem::path(argv[4]), std::ios::binary);
    output << "{\"msl\":{\"" << entry << "\":\"" << name << "\"}}\n";
    output.close();
    if (!output) throw std::runtime_error("Metadata write failed");
    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
}
