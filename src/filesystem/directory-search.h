#ifndef MKXP_DIRECTORY_SEARCH_H
#define MKXP_DIRECTORY_SEARCH_H

#include "util/paths.h"
#include "util/exception.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Ordered directory mounts. The first mount containing a name wins, just as
// with the old search path; entries from later mounts still supplement it.
class DirectorySearch {
    struct Mount {
        std::filesystem::path root;
        std::filesystem::path point;
    };
    std::vector<Mount> mounts;
    bool allowSymlinks;

    static std::string folded(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
        return value;
    }

    static std::filesystem::path locate(const Mount &mount, const std::filesystem::path &relative,
                                        bool insensitive) {
        auto actual = mount.root;
        for (const auto &part : relative) {
            if (part == ".") continue;
            auto next = actual / part;
            if (insensitive && !std::filesystem::exists(next) && std::filesystem::is_directory(actual)) {
                const auto wanted = folded(paths::toUtf8(part.u8string()));
                for (const auto &entry : std::filesystem::directory_iterator(actual))
                    if (folded(paths::toUtf8(entry.path().filename().u8string())) == wanted) {
                        next = entry.path();
                        break;
                    }
            }
            actual = next;
        }
        return actual;
    }

    static bool valid(const std::filesystem::path &path) {
        if (path.has_root_path()) return false;
        for (const auto &part : path)
            if (part == "..") return false;
        return true;
    }

    bool accessible(const Mount &mount, const std::filesystem::path &relative) const {
        if (allowSymlinks) return true;
        auto path = mount.root;
        for (const auto &part : relative) {
            path /= part;
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
                return false;
        }
        return true;
    }

public:
    explicit DirectorySearch(bool allowSymlinks) : allowSymlinks(allowSymlinks) {}

    void add(const std::filesystem::path &root, std::filesystem::path point = {}) {
        if (point == "/") point.clear();
        point = point.lexically_normal();
        if (!valid(point) || !std::filesystem::is_directory(root))
            throw Exception(Exception::IOError, "Resource path must be a directory: %s",
                            paths::toUtf8(root.u8string()).c_str());
        const auto absolute = std::filesystem::absolute(root).lexically_normal();
        for (const auto &mount : mounts)
            if (mount.root == absolute) return;
        mounts.push_back({absolute, point});
    }

    bool remove(const std::filesystem::path &root) {
        const auto absolute = std::filesystem::absolute(root).lexically_normal();
        return std::erase_if(mounts, [&](const Mount &mount) { return mount.root == absolute; }) != 0;
    }

    std::filesystem::path resolve(const std::filesystem::path &name) const {
        if (!valid(name)) return {};
        for (const auto &mount : mounts) {
            const auto relative = name.lexically_relative(mount.point.empty() ? "." : mount.point);
            if (relative.empty() || !valid(relative) || !accessible(mount, relative)) continue;
            auto actual = mount.root / relative;
            if (std::filesystem::exists(actual)) return actual;
        }
        return {};
    }

    std::map<std::string, std::filesystem::path> list(const std::filesystem::path &directory,
                                                   bool insensitive = false) const {
        std::map<std::string, std::filesystem::path> entries;
        std::set<std::string> foldedNames;
        const auto insert = [&](std::string name, const std::filesystem::path &actual) {
            if (insensitive && !foldedNames.insert(folded(name)).second) return;
            entries.try_emplace(std::move(name), actual);
        };
        if (!valid(directory)) return entries;
        const auto dir = directory.empty() ? std::filesystem::path(".") : directory;
        for (const auto &mount : mounts) {
            const auto point = mount.point.empty() ? std::filesystem::path(".") : mount.point;
            const auto relative = dir.lexically_relative(point);
            if (relative.empty() || !valid(relative)) {
                // A mount at Graphics/Pictures also exposes Graphics at the root.
                const auto descendant = point.lexically_relative(dir);
                if (!descendant.empty() && valid(descendant) && descendant != ".")
                    insert(paths::toUtf8(descendant.begin()->u8string()), std::filesystem::path{});
                continue;
            }
            const auto actual = locate(mount, relative, insensitive);
            if (!accessible(mount, actual.lexically_relative(mount.root))) continue;
            if (!std::filesystem::is_directory(actual)) continue;
            for (const auto &entry : std::filesystem::directory_iterator(actual)) {
                if (!allowSymlinks && entry.is_symlink()) continue;
                insert(paths::toUtf8(entry.path().filename().u8string()), entry.path());
            }
        }
        return entries;
    }
};

#endif
