#pragma once
#include "filesystem.h"
struct AudioTestState {
    FileSystem fs;
    FileSystem &fileSystem() { return fs; }
};
extern AudioTestState *shState;
