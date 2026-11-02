#pragma once

#include <cstdint>

#define MINIFFI_MAX_ARGS 16l

using mffi_value = uintptr_t;
// Be sure to add more args to the below line if MINIFFI_MAX_ARGS is bumped
// in the future.
using MINIFFI_FUNC = mffi_value (*)(mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value,
                                   mffi_value, mffi_value);

struct MiniFFIFuncArgs {
  mffi_value params[MINIFFI_MAX_ARGS];
};

mffi_value miniffi_call_intern(MINIFFI_FUNC target, MiniFFIFuncArgs *p, int nparams);
