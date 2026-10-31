#include "util/paths.h"
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
        std::cout << "Portable core tests passed\n";
        return 0;
    } catch (const Exception &error) {
        std::cerr << error.msg.c_str() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
