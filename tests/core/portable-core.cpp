#include "util/paths.h"
#include "filesystem/directory-search.h"
#include "util/exception.h"
#include "util/serial-util.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string utf8(std::u8string_view value) {
    return {reinterpret_cast<const char *>(value.data()), value.size()};
}

struct TempDirectory {
    fs::path original = fs::current_path();
    fs::path root;

    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("mkxp-core-" + std::to_string(stamp));
        // Only delete directories that this test created successfully.
        check(fs::create_directory(root), "create temporary directory");
    }

    ~TempDirectory() {
        std::error_code error;
        fs::current_path(original, error);
        fs::remove_all(root, error);
    }
};

void testDirectorySearch() {
    TempDirectory temp;
    const auto patch = temp.root / "patch";
    const auto game = temp.root / "game";
    fs::create_directories(patch / "Graphics");
    fs::create_directories(game / "Graphics");
    std::ofstream(patch / "Graphics" / "Hero.png") << "patch";
    std::ofstream(game / "Graphics" / "Hero.png") << "game";
    std::ofstream(game / "Graphics" / "Map.jpg") << "map";
    DirectorySearch search(false);
    search.add(patch);
    search.add(game);
    check(paths::readFile(search.resolve("Graphics/Hero.png")) == "patch", "first mount wins");
    check(paths::readFile(search.resolve("Graphics/Map.jpg")) == "map", "later mount supplements");
    auto entries = search.list("Graphics");
    check(entries.size() == 2, "merge duplicate names across mounts");
    check(paths::readFile(entries.at("Hero.png")) == "patch", "enumeration keeps mount priority");
    check(search.resolve("../patch/Graphics/Hero.png").empty(), "reject parent traversal");
    check(search.resolve(fs::absolute(game / "Graphics/Hero.png")).empty(), "reject absolute resource name");
    check(search.list("graphics", true).contains("Hero.png"), "cache enumerates mixed case directories");
    const auto variant = temp.root / "case";
    fs::create_directories(variant / "gRaPhIcS");
    std::ofstream(variant / "gRaPhIcS" / "hero.png") << "lower priority";
    std::ofstream(variant / "gRaPhIcS" / "extra.dat") << "extra";
    search.add(variant);
    entries = search.list("graphics", true);
    check(entries.contains("Hero.png") && !entries.contains("hero.png"), "case-fold collisions preserve mount priority");
    check(entries.contains("extra.dat"), "merge differently cased directory names across mounts");
    check(search.remove(patch), "unmount directory");
    check(paths::readFile(search.resolve("Graphics/Hero.png")) == "game", "unmount reveals next mount");
    search.add(patch, "DLC/Textures");
    check(search.list({}).contains("DLC"), "mount point exposes virtual parent");
    check(search.list("DLC").contains("Textures"), "nested mount point enumeration");
    check(paths::readFile(search.resolve("DLC/Textures/Graphics/Hero.png")) == "patch", "nested mount resolution");
    check(search.list("DLC/Textures/Graphics").contains("Hero.png"), "nested mount content enumeration");
    const auto unicodeName = paths::fromUtf8(utf8(u8"\u96e8\u8840.dat"));
    std::ofstream(game / unicodeName) << "unicode";
    check(paths::readFile(search.resolve(unicodeName)) == "unicode", "resolve Unicode resource name");
    check(search.list({}).contains(paths::toUtf8(unicodeName.u8string())), "enumerate Unicode resource name");
    bool rejected = false;
    try { search.add(game / "Graphics/Hero.png"); }
    catch (const Exception &error) { rejected = error.type == Exception::IOError; }
    check(rejected, "reject archive and file mounts");
    std::error_code ec;
    fs::create_directory_symlink(patch, game / "linked", ec);
    if (!ec) {
        check(!search.list({}).contains("linked"), "exclude symlink enumeration when disabled");
        check(search.resolve("linked/Graphics/Hero.png").empty(), "exclude traversal through symlink when disabled");
        DirectorySearch symlinks(true);
        symlinks.add(game);
        check(!symlinks.resolve("linked/Graphics/Hero.png").empty(), "allow configured symlinks");
    }
}

void testFilesystem() {
    TempDirectory temp;
    const auto directoryText = utf8(u8"\u96e8\u8840 \U0001f3ae");
    const auto fileText = utf8(u8"\u5b58\u6a94.bin");
    const auto folder = temp.root / paths::fromUtf8(directoryText);
    const auto filename = folder / paths::fromUtf8(fileText);
    fs::create_directory(folder);
    const std::string payload("abc\r\ndef\0ghi", 12);
    {
        std::ofstream stream(filename, std::ios::binary);
        stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        check(stream.good(), "write Unicode file");
    }
    check(paths::readFile(filename) == payload, "read binary bytes unchanged");
    const auto encoded = paths::toUtf8(filename.u8string());
    check(fs::equivalent(paths::fromUtf8(encoded), filename), "UTF-8/native path round trip");
    fs::current_path(folder);
    const auto cwd = paths::toUtf8(fs::current_path().u8string());
    check(fs::equivalent(paths::fromUtf8(cwd), folder), "UTF-8 current directory round trip");

    bool missingThrew = false;
    try {
        (void)paths::readFile("missing");
    } catch (const Exception &error) {
        missingThrew = error.type == Exception::NoFileError;
    }
    check(missingThrew, "missing read raises NoFileError");
    std::ofstream("empty", std::ios::binary).close();
    check(paths::readFile("empty").empty(), "empty file is valid");
    check(paths::fromRgss("a\\b\\..\\c").generic_u8string() == u8"a/c",
          "Windows separators normalize on every host");
    check(paths::fromRgss("a/./b/../c").generic_u8string() == u8"a/c", "dot components normalize");
    check(paths::fromRgss(encoded).is_absolute(), "absolute input stays absolute");
    check(paths::fromRgss(encoded).generic_u8string() == filename.generic_u8string(), "absolute Unicode path survives");
    check(paths::fromRgss("").empty(), "empty relative path");
    const auto unicodeRelative = utf8(u8"\u96e8\u8840\\..\\\u5b58\u6a94");
    check(paths::fromRgss(unicodeRelative).generic_u8string() == u8"\u5b58\u6a94", "Unicode dot normalization");
    const auto native = fs::absolute(paths::fromRgss("a\\..\\file")).lexically_normal().make_preferred();
    // macOS temp paths may use /var while current_path resolves /private/var.
    check(fs::equivalent(native.parent_path(), folder) && native.filename() == "file",
          "absolute native path for SDL/Ruby");
}

void testSerialization() {
    static_assert(serial_detail::littleEndian<std::endian::big>(uint32_t{0x12345678}) == 0x78563412);
    static_assert(serial_detail::littleEndian<std::endian::little>(uint32_t{0x12345678}) == 0x12345678);
    static_assert(serial_detail::littleEndian<std::endian::big>(uint64_t{0x0123456789abcdef}) == 0xefcdab8967452301);

    struct IntegerFixture { int32_t value; std::array<unsigned char, 4> wire; };
    constexpr IntegerFixture integers[] = {
        {0, {0, 0, 0, 0}}, {1, {1, 0, 0, 0}}, {-1, {255, 255, 255, 255}},
        {0x12345678, {0x78, 0x56, 0x34, 0x12}},
        {std::numeric_limits<int32_t>::min(), {0, 0, 0, 128}},
        {std::numeric_limits<int32_t>::max(), {255, 255, 255, 127}},
    };
    for (const auto &fixture : integers) {
        // Deliberately unaligned, with guard bytes on both sides.
        std::array<char, 6> buffer{};
        buffer.front() = buffer.back() = 0x5a;
        char *output = buffer.data() + 1;
        writeInt32(&output, fixture.value);
        check(output == buffer.data() + 5, "integer write cursor");
        check(std::memcmp(buffer.data() + 1, fixture.wire.data(), 4) == 0, "integer wire fixture");
        const char *input = buffer.data() + 1;
        check(readInt32(&input) == fixture.value, "integer read fixture");
        check(input == output, "integer read cursor");
        check(buffer.front() == 0x5a && buffer.back() == 0x5a, "integer guard bytes");
    }

    struct DoubleFixture { uint64_t bits; std::array<unsigned char, 8> wire; };
    constexpr DoubleFixture doubles[] = {
        {0, {0, 0, 0, 0, 0, 0, 0, 0}},
        {0x8000000000000000, {0, 0, 0, 0, 0, 0, 0, 0x80}},
        {0x3ff0000000000000, {0, 0, 0, 0, 0, 0, 0xf0, 0x3f}},
        {0xc004000000000000, {0, 0, 0, 0, 0, 0, 4, 0xc0}},
        {0x7ff0000000000000, {0, 0, 0, 0, 0, 0, 0xf0, 0x7f}},
        {0xfff0000000000000, {0, 0, 0, 0, 0, 0, 0xf0, 0xff}},
        {1, {1, 0, 0, 0, 0, 0, 0, 0}},
        {0x7ff8123456789abc, {0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, 0xf8, 0x7f}},
    };
    for (const auto &fixture : doubles) {
        std::array<char, 10> buffer{};
        buffer.front() = buffer.back() = 0x5a;
        char *output = buffer.data() + 1;
        writeDouble(&output, std::bit_cast<double>(fixture.bits));
        check(output == buffer.data() + 9, "double write cursor");
        check(std::memcmp(buffer.data() + 1, fixture.wire.data(), 8) == 0, "double wire fixture");
        const char *input = buffer.data() + 1;
        check(std::bit_cast<uint64_t>(readDouble(&input)) == fixture.bits, "double bit preservation");
        check(input == output, "double read cursor");
        check(buffer.front() == 0x5a && buffer.back() == 0x5a, "double guard bytes");
    }
}

int main() {
    try {
        testSerialization();
        testFilesystem();
        testDirectorySearch();
        std::cout << "Portable core tests passed\n";
        return 0;
    } catch (const Exception &error) {
        std::cerr << error.msg.c_str() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
