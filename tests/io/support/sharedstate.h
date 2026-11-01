#pragma once
#include "filesystem.h"
struct IOTestState {
    FileSystem files;
    void *data = nullptr;
    FileSystem &fileSystem() { return files; }
    void *bindingData() { return data; }
};
extern IOTestState *shState;
