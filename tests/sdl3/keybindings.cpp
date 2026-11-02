// Exercise the production keybinding reader/writer against legacy binary files.
#include "input/keybindings.h"
#include "config.h"
#include "util/paths.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

// These tests set the two configuration fields used by keybinding persistence;
// they do not link the engine's configuration parser or initialize a game.
Config::Config() = default;

namespace fs = std::filesystem;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static bool same(const BDescVec &left, const BDescVec &right) {
    return std::ranges::equal(left, right, [](const auto &a, const auto &b) {
        return a.src == b.src && a.target == b.target;
    });
}

struct Fixture {
    fs::path root = fs::temp_directory_path() /
        (u8"mkxp-按鍵-" + paths::fromUtf8(std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())).u8string());
    fs::path file = root / "keybindings.mkxp1";
    Fixture() { fs::create_directory(root); }
    ~Fixture() {
        std::error_code ignored;
        fs::remove(file, ignored);
        fs::remove(root, ignored);
    }
};

static std::string readBytes(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

static void writeBytes(const fs::path &path, const std::string &bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    require(bool(file), "fixture write failed");
}

int main() {
    try {
        Fixture fixture;
        Config config;
        config.rgssVersion = 1;
        config.customDataPath = paths::toUtf8(fixture.root.u8string());
        const auto defaults = genDefaultBindings(config);
        require(same(loadBindings(config), defaults), "missing file did not use default bindings");

        BindingDesc key{};
        key.src.type = Key;
        key.src.d.scan = SDL_SCANCODE_F1;
        key.target = Input::A;
        BindingDesc axis{};
        axis.src.type = CAxis;
        axis.src.d.ca = {SDL_GAMEPAD_AXIS_LEFTX, Negative};
        axis.target = Input::Left;
        const BDescVec bindings{key, axis};

        // Write exactly the previous implementation's native-ABI version 3
        // header and descriptors, independently of the SDL writer.
        const std::array<uint32_t, 3> header{3, 1, 2};
        std::string legacy(reinterpret_cast<const char *>(header.data()), sizeof(header));
        legacy.append(reinterpret_cast<const char *>(bindings.data()), bindings.size() * sizeof(BindingDesc));
        writeBytes(fixture.file, legacy);
        require(same(loadBindings(config), bindings), "legacy bindings did not load from a Unicode path");
        storeBindings(bindings, config);
        require(readBytes(fixture.file) == legacy, "SDL writer changed the legacy binary format");

        for (size_t size : {sizeof(header) - 1, legacy.size() - 1}) {
            writeBytes(fixture.file, legacy.substr(0, size));
            require(same(loadBindings(config), defaults), "truncated file did not use defaults");
        }

        storeBindings({}, config);
        require(loadBindings(config).empty(), "empty binding list did not round trip");
        std::cout << "PASS: SDL3 keybinding IO, legacy bytes, Unicode paths and corrupt files\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
