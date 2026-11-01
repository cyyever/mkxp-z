#include "etc/table.h"
#include "util/exception.h"
#include "util/serial-util.h"
#include <array>
#include <climits>
#include <cstring>
#include <iostream>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void rejects(F function, Exception::Type expected) {
    try { function(); } catch (const Exception &error) {
        check(error.type == expected, "wrong exception type");
        return;
    }
    throw std::runtime_error("invalid Table accepted");
}
static std::vector<char> header(int x, int y, int z, int size) {
    std::vector<char> bytes(20);
    char *cursor = bytes.data();
    for (int value : {3, x, y, z, size}) writeInt32(&cursor, value);
    return bytes;
}
static void roundtrip() {
    const std::array<int16_t, 8> values{-32768, -1, 0, 1, 0x1234, 32767, -2, 456};
    const std::array<unsigned char, 36> expected{
        3,0,0,0, 2,0,0,0, 2,0,0,0, 2,0,0,0, 8,0,0,0,
        0,128, 255,255, 0,0, 1,0, 52,18, 255,127, 254,255, 200,1};
    Table original(2, 2, 2);
    for (int z=0; z<2; ++z) for (int y=0; y<2; ++y) for (int x=0; x<2; ++x)
        original.set(values[z*4+y*2+x], x,y,z);
    std::vector<char> encoded(original.serialSize());
    original.serialize(std::span<char>(encoded));
    check(encoded.size()==expected.size() && !std::memcmp(encoded.data(),expected.data(),expected.size()),
          "RGSS little-endian wire bytes changed");
    std::unique_ptr<Table> loaded(Table::deserialize(std::span<const char>(encoded)));
    check(loaded->xSize()==2 && loaded->ySize()==2 && loaded->zSize()==2,"dimensions changed");
    for (int z=0; z<2; ++z) for (int y=0; y<2; ++y) for (int x=0; x<2; ++x)
        check(loaded->get(x,y,z)==values[z*4+y*2+x],"cell changed");
    std::vector<char> adapter(encoded.size());
    loaded->serialize(adapter.data());
    check(adapter==encoded,"public pointer adapter changed bytes");
    for (size_t length=0; length<encoded.size(); ++length)
        rejects([&] { std::unique_ptr<Table> bad(Table::deserialize(encoded.data(), static_cast<int>(length))); },Exception::RGSSError);
    encoded.push_back('x');
    rejects([&] { std::unique_ptr<Table> bad(Table::deserialize(std::span<const char>(encoded))); },Exception::RGSSError);
    rejects([&] { original.serialize(std::span<char>(adapter).first(19)); },Exception::ArgumentError);
    rejects([&] { original.serialize(nullptr); },Exception::ArgumentError);
}
static void invalidDimensions() {
    auto negative = header(-1,-1,1,1);
    negative.resize(22); // The old negative*negative product admitted this payload.
    for (const auto &bytes : {negative,header(65536,65536,1,0),
                              header(INT_MAX,INT_MAX,INT_MAX,0),header(1,1,1,-1),
                              header(0,1,1,1),header(1,1,1,0)})
        rejects([&] { std::unique_ptr<Table> bad(Table::deserialize(std::span<const char>(bytes))); },Exception::RGSSError);
    rejects([] { std::unique_ptr<Table> bad(Table::deserialize(nullptr,20)); },Exception::RGSSError);
    rejects([] { Table bad(-1,0,1); },Exception::ArgumentError);
    rejects([] { Table bad(65536,65536,1); },Exception::ArgumentError);
    rejects([] { Table bad(INT_MAX,1,1); },Exception::ArgumentError);
    Table preserved(2,2,1);
    preserved.set(42,1,1,0);
    rejects([&] { preserved.resize(INT_MAX,INT_MAX,1); },Exception::ArgumentError);
    check(preserved.xSize()==2 && preserved.ySize()==2 && preserved.get(1,1,0)==42,
          "failed resize changed existing Table");
}
static void zeroDimensionsAndResize() {
    for (const auto &dimensions : {std::array{0,INT_MAX,INT_MAX},std::array{INT_MAX,0,INT_MAX},std::array{INT_MAX,INT_MAX,0}}) {
        Table empty(dimensions[0],dimensions[1],dimensions[2]);
        check(empty.serialSize()==20,"zero-dimensional payload is not empty");
        std::vector<char> bytes(20);empty.serialize(std::span<char>(bytes));
        std::unique_ptr<Table> loaded(Table::deserialize(std::span<const char>(bytes)));
        check(loaded->xSize()==dimensions[0] && loaded->ySize()==dimensions[1] && loaded->zSize()==dimensions[2],
              "zero-dimensional shape changed");
        loaded->resize(0,INT_MAX-1,INT_MAX); // Must skip zero-width rows without enormous loops.
        loaded->resize(2,2,2);check(loaded->get(1,1,1)==0,"new cells not zero initialized");
    }
    Table table(3,2,2);
    for (int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<3;++x) table.set(static_cast<int16_t>(100*z+10*y+x),x,y,z);
    table.resize(2,3,3);
    for (int z=0;z<3;++z) for(int y=0;y<3;++y) for(int x=0;x<2;++x)
        check(table.get(x,y,z)==((z<2&&y<2)?100*z+10*y+x:0),"resize overlap/padding changed");
    table.resize(0,INT_MAX,INT_MAX);
    check(table.serialSize()==20,"resize to zero shape failed");
    table.resize(1,1,1);check(table.get(0)==0,"resize from zero shape failed");
}
static void corpus(const char *path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "cannot open Table corpus");
    const std::vector<char> data((std::istreambuf_iterator<char>(input)), {});
    std::span<const char> remaining(data);
    size_t count = 0;
    while (!remaining.empty()) {
        check(remaining.size() >= 4, "truncated corpus size");
        const char *cursor = remaining.data();
        const int size = readInt32(&cursor);
        remaining = remaining.subspan(4);
        check(size >= 20 && static_cast<size_t>(size) <= remaining.size(), "invalid corpus Table size");
        const auto bytes = remaining.first(static_cast<size_t>(size));
        std::unique_ptr<Table> table(Table::deserialize(bytes));
        std::vector<char> encoded(table->serialSize());
        table->serialize(std::span<char>(encoded));
        // The first word is the dimension hint, canonicalized by the existing
        // serializer. Preserve every dimension, count and cell byte after it.
        check(encoded.size() == bytes.size() && std::equal(encoded.begin()+4, encoded.end(), bytes.begin()+4),
              "game Table corpus payload changed");
        remaining = remaining.subspan(static_cast<size_t>(size));
        ++count;
    }
    std::cout << count << " real game Table payloads roundtripped\n";
}
int main(int argc, char **argv) {
    try { roundtrip();invalidDimensions();zeroDimensionsAndResize();
          if (argc == 2) corpus(argv[1]);
          std::cout<<"Table wire format, spans, malformed headers and resize checks passed\n";return 0; }
    catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
    catch(const Exception &error) { std::cerr<<error.msg<<'\n';return 1; }
}
