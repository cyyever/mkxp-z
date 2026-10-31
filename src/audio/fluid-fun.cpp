#include "fluid-fun.h"

struct FluidFunctions fluid;

void initFluidFunctions()
{
#define FLUID_FUN(name, type) \
    fluid.name = fluid_##name;
#define FLUID_FUN2(name, type, real_name) \
    fluid.name = real_name;

    FLUID_FUNCS
    FLUID_FUNCS2

#undef FLUID_FUN
#undef FLUID_FUN2
}
