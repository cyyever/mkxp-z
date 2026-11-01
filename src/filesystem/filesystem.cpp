/*
** filesystem.cpp
**
** This file is part of mkxp.
**
** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
**
** mkxp is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 2 of the License, or
** (at your option) any later version.
**
** mkxp is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "filesystem.h"
#include "directory-search.h"
#include "util/paths.h"
#include "display/font.h"

#include <memory>
#include <algorithm>
#include <set>

namespace fs = std::filesystem;

static SDL_IOStream *openStream(const fs::path &path) {
  SDL_IOStream *stream = SDL_IOFromFile(paths::toUtf8(path.u8string()).c_str(), "rb");
  if (!stream) throw Exception(Exception::SDLError, "%s", SDL_GetError());
  return stream;
}

struct FileSystemPrivate {
  DirectorySearch search;
  bool havePathCache = false;
  explicit FileSystemPrivate(bool allowSymlinks) : search(allowSymlinks) {}
};

FileSystem::FileSystem(const char *, bool allowSymlinks)
    : p(std::make_unique<FileSystemPrivate>(allowSymlinks)) {}

FileSystem::~FileSystem() = default;

void FileSystem::addPath(const char *path, const char *mountpoint, bool reload) {
  try {
    p->search.add(paths::fromRgss(path), mountpoint ? paths::fromRgss(mountpoint) : fs::path{});
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
  if (reload) reloadPathCache();
}

void FileSystem::removePath(const char *path, bool reload) {
  try {
    if (!p->search.remove(paths::fromRgss(path)))
      throw Exception(Exception::IOError, "Resource directory is not mounted: %s", path);
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
  if (reload) reloadPathCache();
}

void FileSystem::createPathCache() {
  // Index directories on demand; unused trees and symlink cycles need no scan.
  p->search.clearCache();
  p->havePathCache = true;
}

void FileSystem::reloadPathCache() {
  if (p->havePathCache) createPathCache();
}

void FileSystem::initFontSets(SharedFontState &sfs) {
  try {
    for (const auto &[folder, actual] : p->search.list({})) {
      if (DirectorySearch::key(folder) != "fonts" || (!actual.empty() && !fs::is_directory(actual))) continue;
      for (const auto &[name, path] : p->search.list(paths::fromUtf8(folder))) {
        const auto ext = DirectorySearch::key(paths::toUtf8(path.extension().u8string()));
        if (ext != ".ttf" && ext != ".otf") continue;
        std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(openStream(path), SDL_CloseIO);
        const auto filename = folder + "/" + name;
        sfs.initFontSetCB(stream.get(), filename.c_str());
      }
    }
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
}

void FileSystem::openRead(OpenHandler &handler, const char *filename) {
  const auto requested = paths::fromRgss(filename);
  const auto file = p->havePathCache ? DirectorySearch::key(paths::toUtf8(requested.filename().u8string()))
                                   : paths::toUtf8(requested.filename().u8string());
  bool matched = false, missing = false;
  std::set<fs::path> failedDecodes;
  const auto tryFile = [&](const std::string &name, const fs::path &actual) {
    if (!name.starts_with(file) || (name.size() != file.size() && name[file.size()] != '.')) return false;
    if (actual.empty() || failedDecodes.contains(actual)) return false;
    const auto status = fs::status(actual); // Same single stat as is_regular_file(path).
    if (!fs::is_regular_file(status)) {
      missing |= !fs::exists(status);
      return false;
    }
    matched = true;
    const auto dot = name.find_last_of('.');
    const char *ext = dot == std::string::npos ? nullptr : name.c_str() + dot + 1;
    // OpenHandler takes ownership, including on a failed decode.
    if (handler.tryRead(openStream(actual), ext)) return true;
    failedDecodes.insert(actual); // Only failed decodes allocate; successful hits do not.
    return false;
  };
  try {
    if (p->havePathCache) {
      for (int attempt = 0; attempt < 2; ++attempt) {
        missing = false;
        const auto &entries = p->search.cachedList(requested.parent_path());
        if (const auto exact = entries.find(file); exact != entries.end())
          if (tryFile(exact->first, exact->second.actual)) return;
        if (!missing) {
          const auto prefix = file + '.';
          for (auto entry = entries.lower_bound(prefix);
               entry != entries.end() && entry->first.starts_with(prefix); ++entry) {
            if (tryFile(entry->first, entry->second.actual)) return;
            if (missing) break;
          }
        }
        // A deleted winning mount can hide the same name in a later mount.
        // Refresh outside iteration, once, on a stale match or a complete miss.
        if (attempt != 0 || (!missing && matched)) break;
        p->search.invalidateCache(requested.parent_path());
      }
    } else {
      for (const auto &[name, actual] : p->search.list(requested.parent_path()))
        if (tryFile(name, actual)) return;
    }
    if (!matched) {
      // filename may be a desensitize() cache view invalidated by the retry.
      throw Exception(Exception::NoFileError, "%s", paths::toUtf8(requested.u8string()).c_str());
    }
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
}

SDL_IOStream *FileSystem::openReadRaw(const char *filename) {
  try {
    const auto path = p->search.resolve(paths::fromRgss(filename));
    if (!path.empty() && fs::is_regular_file(path)) return openStream(path);
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
  throw Exception(Exception::NoFileError, "%s", filename);
}

bool FileSystem::exists(const char *filename) {
  try {
    return !p->search.resolve(paths::fromRgss(filename)).empty();
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
}

const char *FileSystem::desensitize(const char *filename) {
  if (p->havePathCache) {
    try {
      if (const auto *entry = p->search.cachedEntry(paths::fromRgss(filename)))
        return entry->mixed.c_str();
    } catch (const fs::filesystem_error &error) {
      throw Exception(Exception::IOError, "%s", error.what());
    }
  }
  return filename;
}
