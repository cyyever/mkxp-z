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
#include "util/debugwriter.h"
#include "util/paths.h"
#include "display/font.h"
#include "eventthread.h"
#include "sharedstate.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <set>

#ifdef SDL_PLATFORM_APPLE
#include <iconv.h>
#endif

namespace fs = std::filesystem;

static std::string lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c); });
  return value;
}

// Keep the spelling used on disk separately: macOS may enumerate decomposed
// names even when the game asks for their composed UTF-8 representation.
static std::string normalized(std::string value) {
#ifdef SDL_PLATFORM_APPLE
  iconv_t converter = iconv_open("UTF-8", "UTF-8-MAC");
  if (converter == reinterpret_cast<iconv_t>(-1)) return value;
  std::string output(value.size() * 4 + 1, '\0');
  char *input = value.data();
  char *target = output.data();
  size_t inputSize = value.size(), outputSize = output.size();
  const auto result = iconv(converter, &input, &inputSize, &target, &outputSize);
  iconv_close(converter);
  if (result != static_cast<size_t>(-1)) {
    output.resize(output.size() - outputSize);
    return output;
  }
#endif
  return value;
}

static SDL_IOStream *openStream(const fs::path &path) {
  SDL_IOStream *stream = SDL_IOFromFile(paths::toUtf8(path.u8string()).c_str(), "rb");
  if (!stream) throw Exception(Exception::SDLError, "%s", SDL_GetError());
  return stream;
}

struct FileSystemPrivate {
  DirectorySearch search;
  bool havePathCache = false;
  struct CachedPath {
    fs::path actual;
    std::string mixed;
  };
  std::map<std::string, CachedPath> pathCache;
  std::map<std::string, std::vector<std::string>> fileLists;

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
  Debug() << "Loading path cache...";
  p->pathCache.clear();
  p->fileLists.clear();
  std::set<fs::path> ancestors;
  std::function<void(const fs::path &)> visit = [&](const fs::path &dir) {
    if (shState && shState->rtData().rqTerm)
      throw Exception(Exception::MKXPError, "Game close requested. Aborting path cache enumeration.");
    auto &files = p->fileLists[lower(normalized(paths::toUtf8(dir.generic_u8string())))];
    for (const auto &[name, actual] : p->search.list(dir, true)) {
      const auto virtualPath = dir / paths::fromUtf8(name);
      if (actual.empty() || fs::is_directory(actual)) {
        // Follow permitted symlinks, but never recurse into an ancestor again.
        const auto canonical = actual.empty() ? fs::path{} : fs::canonical(actual);
        if (!canonical.empty() && !ancestors.insert(canonical).second) continue;
        visit(virtualPath);
        if (!canonical.empty()) ancestors.erase(canonical);
      } else if (fs::is_regular_file(actual)) {
        const auto mixed = paths::toUtf8(virtualPath.generic_u8string());
        auto [entry, inserted] = p->pathCache.try_emplace(lower(normalized(mixed)), actual, mixed);
        if (inserted) files.push_back(lower(normalized(name)));
      }
    }
    std::ranges::sort(files);
  };
  try {
    visit({});
  } catch (const fs::filesystem_error &error) {
    throw Exception(Exception::IOError, "%s", error.what());
  }
  p->havePathCache = true;
  Debug() << "Path cache completed.";
}

void FileSystem::reloadPathCache() {
  if (p->havePathCache) createPathCache();
}

void FileSystem::initFontSets(SharedFontState &sfs) {
  try {
    for (const auto &[folder, actual] : p->search.list({})) {
      if (lower(folder) != "fonts" || (!actual.empty() && !fs::is_directory(actual))) continue;
      for (const auto &[name, path] : p->search.list(paths::fromUtf8(folder))) {
        const auto ext = lower(paths::toUtf8(path.extension().u8string()));
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
  auto file = paths::toUtf8(requested.filename().u8string());
  auto directory = paths::toUtf8(requested.parent_path().generic_u8string());
  std::map<std::string, fs::path> candidates;
  try {
    if (p->havePathCache) {
      file = lower(normalized(file));
      directory = lower(normalized(directory));
      const auto list = p->fileLists.find(directory);
      if (list != p->fileLists.end())
        for (const auto &name : list->second) {
          const auto key = directory.empty() ? name : directory + "/" + name;
          candidates.emplace(name, p->pathCache.at(key).actual);
        }
    } else {
      candidates = p->search.list(requested.parent_path());
    }
    size_t matches = 0;
    for (const auto &[name, actual] : candidates) {
      if (!name.starts_with(file) || (name.size() != file.size() && name[file.size()] != '.')) continue;
      if (actual.empty() || !fs::is_regular_file(actual)) continue;
      ++matches;
      const auto dot = name.find_last_of('.');
      const char *ext = dot == std::string::npos ? nullptr : name.c_str() + dot + 1;
      // OpenHandler takes ownership, including on a failed decode.
      if (handler.tryRead(openStream(actual), ext)) return;
    }
    if (!matches) throw Exception(Exception::NoFileError, "%s", filename);
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
    const auto key = lower(normalized(paths::toUtf8(paths::fromRgss(filename).generic_u8string())));
    if (auto found = p->pathCache.find(key); found != p->pathCache.end())
      return found->second.mixed.c_str();
  }
  return filename;
}
