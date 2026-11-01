#ifndef MKXP_DIRECTORY_SEARCH_H
#define MKXP_DIRECTORY_SEARCH_H

#include "util/paths.h"
#include "util/exception.h"
#include "util/filename-normalization.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Ordered directory mounts. The first mount containing a name wins, just as
// with the old search path; entries from later mounts still supplement it.
class DirectorySearch {
public:
    using Entries = std::map<std::string, std::filesystem::path>;
    struct CachedEntry {
        std::filesystem::path actual;
        std::string mixed;
    };
    using CachedEntries = std::map<std::string, CachedEntry>;

    static std::string key(std::string_view value) {
        auto result = paths::normalizedNfc(value);
        std::ranges::transform(result, result.begin(), [](unsigned char c) {
            return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
        });
        return result;
    }

private:
    using Stamp = std::optional<std::filesystem::file_time_type>;
    using Stamps = std::map<std::filesystem::path, Stamp>;
    struct Mount {
        std::filesystem::path root;
        std::filesystem::path point;
    };
    struct CachedDirectory {
        CachedEntries entries;
        Stamps stamps;
        bool ready = false;
    };
    std::vector<Mount> mounts;
    bool allowSymlinks;
    mutable std::map<std::string, CachedDirectory> cache;

    static Stamp stamp(const std::filesystem::path &path) {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(path, error);
        return error ? std::nullopt : Stamp(time);
    }

    static void watch(Stamps *stamps, const std::filesystem::path &path) {
        if (stamps) stamps->try_emplace(path, stamp(path));
    }

    static bool valid(const std::filesystem::path &path) {
        if (path.has_root_path()) return false;
        for (const auto &part : path)
            if (part == "..") return false;
        return true;
    }

    // Unlike lexically_relative, match mount points case-insensitively when
    // requested, and preserve the actual spelling of the remaining components.
    static std::optional<std::filesystem::path> relativeTo(
            const std::filesystem::path &path, const std::filesystem::path &base,
            bool insensitive) {
        auto part = path.begin();
        for (const auto &prefix : base) {
            if (prefix == "." || prefix.empty()) continue;
            while (part != path.end() && (*part == "." || part->empty())) ++part;
            if (part == path.end()) return std::nullopt;
            if (insensitive ? key(paths::toUtf8(part->u8string())) != key(paths::toUtf8(prefix.u8string()))
                            : *part != prefix)
                return std::nullopt;
            ++part;
        }
        std::filesystem::path result;
        for (; part != path.end(); ++part)
            if (*part != "." && !part->empty()) result /= *part;
        return result;
    }

    std::filesystem::path locate(const Mount &mount, const std::filesystem::path &relative,
                                 bool insensitive, Stamps *stamps) const {
        auto actual = mount.root;
        watch(stamps, actual);
        for (const auto &part : relative) {
            if (part == "." || part.empty()) continue;
            auto next = actual / part;
            if (insensitive && !std::filesystem::exists(next) && std::filesystem::is_directory(actual)) {
                const auto wanted = key(paths::toUtf8(part.u8string()));
                for (const auto &entry : std::filesystem::directory_iterator(actual))
                    if (key(paths::toUtf8(entry.path().filename().u8string())) == wanted) {
                        next = entry.path();
                        break;
                    }
            }
            watch(stamps, next);
            if (!allowSymlinks && std::filesystem::is_symlink(std::filesystem::symlink_status(next)))
                return {};
            actual = next;
        }
        return actual;
    }

    Entries list(const std::filesystem::path &directory, bool insensitive, Stamps *stamps) const {
        Entries entries;
        if (!valid(directory)) return entries;
        std::set<std::string> names;
        const auto insert = [&](std::string name, const std::filesystem::path &actual) {
            if (insensitive && !names.insert(key(name)).second) return;
            entries.try_emplace(std::move(name), actual);
        };
        for (const auto &mount : mounts) {
            watch(stamps, mount.root);
            const auto relative = relativeTo(directory, mount.point, insensitive);
            if (!relative) {
                // A mount at Graphics/Pictures also exposes Graphics at the root.
                const auto descendant = relativeTo(mount.point, directory, insensitive);
                if (descendant && !descendant->empty())
                    insert(paths::toUtf8(descendant->begin()->u8string()), {});
                continue;
            }
            const auto actual = locate(mount, *relative, insensitive, stamps);
            if (actual.empty() || !std::filesystem::is_directory(actual)) continue;
            for (const auto &entry : std::filesystem::directory_iterator(actual)) {
                if (!allowSymlinks && entry.is_symlink()) continue;
                insert(paths::toUtf8(entry.path().filename().u8string()), entry.path());
            }
        }
        return entries;
    }

public:
    explicit DirectorySearch(bool allowSymlinks) : allowSymlinks(allowSymlinks) {}

    void clearCache() const { cache.clear(); }

    void add(const std::filesystem::path &root, std::filesystem::path point = {}) {
        if (point == "/" || point == ".") point.clear();
        point = point.lexically_normal();
        if (!valid(point) || !std::filesystem::is_directory(root))
            throw Exception(Exception::IOError, "Resource path must be a directory: %s",
                            paths::toUtf8(root.u8string()).c_str());
        const auto absolute = std::filesystem::absolute(root).lexically_normal();
        for (const auto &mount : mounts)
            if (mount.root == absolute) return;
        mounts.push_back({absolute, point});
        clearCache();
    }

    bool remove(const std::filesystem::path &root) {
        const auto absolute = std::filesystem::absolute(root).lexically_normal();
        const bool removed = std::erase_if(mounts, [&](const Mount &mount) { return mount.root == absolute; }) != 0;
        if (removed) clearCache();
        return removed;
    }

    std::filesystem::path resolve(const std::filesystem::path &name) const {
        if (!valid(name)) return {};
        for (const auto &mount : mounts) {
            const auto relative = relativeTo(name, mount.point, false);
            if (!relative) continue;
            const auto actual = locate(mount, *relative, false, nullptr);
            if (!actual.empty() && std::filesystem::exists(actual)) return actual;
        }
        return {};
    }

    Entries list(const std::filesystem::path &directory, bool insensitive = false) const {
        return list(directory, insensitive, nullptr);
    }

    const CachedEntries &cachedList(std::filesystem::path directory) const {
        static const CachedEntries empty;
        if (!valid(directory)) return empty;
        directory = directory.lexically_normal();
        if (directory == ".") directory.clear();
        while (!directory.empty() && directory.filename().empty()) directory = directory.parent_path();
        auto &cached = cache[key(paths::toUtf8(directory.generic_u8string()))];
        if (cached.ready && std::ranges::all_of(cached.stamps, [](const auto &observed) {
                return stamp(observed.first) == observed.second;
            }))
            return cached.entries;

        auto mixedDirectory = directory;
        if (!directory.empty()) {
            const auto &parent = cachedList(directory.parent_path());
            if (const auto entry = parent.find(key(paths::toUtf8(directory.filename().u8string())));
                entry != parent.end())
                mixedDirectory = paths::fromUtf8(entry->second.mixed);
        }
        Stamps stamps;
        CachedEntries entries;
        for (const auto &[name, actual] : list(mixedDirectory, true, &stamps)) {
            const auto mixed = paths::toUtf8((mixedDirectory / paths::fromUtf8(name)).generic_u8string());
            entries.try_emplace(key(name), actual, mixed);
        }
        cached = {std::move(entries), std::move(stamps), true};
        return cached.entries;
    }

    const CachedEntry *cachedEntry(const std::filesystem::path &name) const {
        if (!valid(name)) return nullptr;
        const auto &directory = cachedList(name.parent_path());
        const auto entry = directory.find(key(paths::toUtf8(name.filename().u8string())));
        return entry == directory.end() ? nullptr : &entry->second;
    }
};

#endif
