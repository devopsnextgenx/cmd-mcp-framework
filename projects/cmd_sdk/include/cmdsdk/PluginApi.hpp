#pragma once

#include "cmdsdk/CommandRegistry.hpp"

#if defined(_WIN32)
#define CMDSDK_API __declspec(dllexport)
#else
#define CMDSDK_API __attribute__((visibility("default")))
#endif

namespace cmdsdk {

using RegisterCommandsFn = void (*)(CommandRegistry& registry);

}  // namespace cmdsdk
