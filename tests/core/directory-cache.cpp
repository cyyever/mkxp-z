#include "filesystem/directory-search.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct TempDirectory {
    fs::path root = fs::temp_directory_path() /
        ("mkxp-directory-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDirectory() { check(fs::create_directory(root), "create test directory"); }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(root, ignored); }
};

static void write(const fs::path &path, const char *value) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << value;
    check(stream.good(), "write test resource");
}

static std::string read(DirectorySearch &search, const fs::path &name) {
    const auto *entry = search.cachedEntry(name);
    check(entry != nullptr, "cached resource found");
    return paths::readFile(entry->actual);
}

static void testChanges() {
    TempDirectory temp;
    const auto patch = temp.root / "patch";
    const auto game = temp.root / "game";
    fs::create_directories(patch / "Graphics");
    write(game / "Graphics/Hero.png", "game");
    DirectorySearch search(false);
    search.add(patch);
    search.add(game);
    check(read(search, "graphics/HERO.PNG") == "game", "initial case-insensitive cache lookup");
    check(search.cachedEntry("smoke.png") == nullptr, "screenshot absent before creation");
    write(game / "smoke.png", "saved screenshot");
    check(read(search, "SMOKE.PNG") == "saved screenshot", "new screenshot visible after startup cache");
    write(patch / "Graphics/hero.PNG", "patch");
    check(read(search, "graphics/HERO.png") == "patch", "new patch shadows cached lower-priority file");
    const auto *spelling = search.cachedEntry("graphics/hero.png");
    check(spelling && spelling->mixed == "Graphics/hero.PNG", "keep actual virtual spelling");
    fs::remove(patch / "Graphics/hero.PNG");
    check(read(search, "graphics/hero.png") == "game", "deleted patch reveals game file");
    fs::remove(game / "smoke.png");
    check(search.cachedEntry("smoke.png") == nullptr, "deleted resource leaves cache");
    check(search.cachedList("screenshots/new").empty(), "new nested directory initially absent");
    write(game / "Screenshots/New/Frame.PNG", "nested");
    check(read(search, "screenshots/new/frame.png") == "nested", "new differently cased nested directory found");
    search.remove(patch);
    search.add(patch, "DLC/Textures");
    write(patch / "Graphics/extra.png", "mounted");
    check(read(search, "dlc/textures/graphics/EXTRA.PNG") == "mounted", "case-insensitive virtual mount point");
    check(search.list("dlc", true).contains("Textures"), "case-insensitive virtual mount parent");
    check(search.cachedEntry("../game/smoke.png") == nullptr, "cache rejects parent traversal");
    check(search.cachedEntry(game / "Graphics/Hero.png") == nullptr, "cache rejects absolute paths");
    search.remove(patch);
    check(search.cachedEntry("dlc/textures/graphics/extra.png") == nullptr, "unmounted cache entries disappear");
}

static void testUnicodeAndSymlinks() {
    TempDirectory temp;
    const auto game = temp.root / "game";
    const auto external = temp.root / "external";
    const auto unicode = paths::fromUtf8("\xe9\x9b\xa8\xe8\xa1\x80.PNG");
    write(game / "Graphics" / unicode, "unicode");
    write(external / "item.png", "external");
    DirectorySearch denied(false);
    denied.add(game);
    check(read(denied, fs::path("graphics") / unicode) == "unicode", "Unicode cached resource");
    check(denied.cachedList("linked").empty(), "symlink target absent before creation");
    std::error_code error;
    fs::create_directory_symlink(external, game / "Linked", error);
    if (!error) {
        check(denied.cachedEntry("linked/item.png") == nullptr, "case lookup does not bypass symlink policy");
        DirectorySearch allowed(true);
        allowed.add(game);
        check(read(allowed, "linked/item.png") == "external", "configured symlinks remain readable");
        fs::create_directory_symlink(game, game / "Loop", error);
        if (!error)
            check(read(allowed, fs::path("loop/loop/graphics") / unicode) == "unicode",
                  "finite lookups through symlink loops do not trigger recursive scans");
    }
#ifdef __APPLE__
    const auto patch = temp.root / "patch";
    const auto nfd = paths::fromUtf8("cafe\xcc\x81.png");
    const auto nfc = paths::fromUtf8("caf\xc3\xa9.png");
    write(game / "Graphics" / nfc, "composed game");
    fs::create_directories(patch / "Graphics");
    DirectorySearch normalized(false);
    normalized.add(patch);
    normalized.add(game);
    check(read(normalized, fs::path("graphics") / nfc) == "composed game", "initial NFC resource");
    write(patch / "Graphics" / nfd, "decomposed patch");
    check(read(normalized, fs::path("graphics") / nfc) == "decomposed patch", "new NFD patch keeps priority over NFC game file");
    check(normalized.cachedEntry(fs::path("graphics") / nfc)->mixed.find(paths::toUtf8(nfd.u8string())) != std::string::npos,
          "normalization preserves original on-disk spelling");
#endif
}

int main() {
    try {
        testChanges();
        testUnicodeAndSymlinks();
        std::cout << "Directory cache tests passed\n";
        return 0;
    } catch (const Exception &error) {
        std::cerr << error.msg.c_str() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
    }
    return 1;
}
