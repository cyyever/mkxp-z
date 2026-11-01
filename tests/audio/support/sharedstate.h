#pragma once
#include "filesystem.h"
#include "eventthread.h"
struct AudioTestState {
    FileSystem fs;
    RGSSThreadData data;
    FileSystem &fileSystem() { return fs; }
    RGSSThreadData &rtData() { return data; }
};
extern AudioTestState *shState;
