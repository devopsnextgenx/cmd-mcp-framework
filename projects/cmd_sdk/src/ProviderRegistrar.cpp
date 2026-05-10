// -----------------------------------------------------------------------------
// ProviderRegistrar.cpp
//
// This translation unit is compiled into EVERY plugin shared library.
// It owns the single extern "C" RegisterCommands symbol, which the plugin
// loader calls after dlopen/LoadLibrary.
//
// Plugin developers must NEVER define RegisterCommands themselves.
// The guard #define in ProviderRegistrar.hpp will produce a compile error
// if they try to do so after including that header.
//
// All provider self-registration happens via CMDSDK_REGISTER_PROVIDER(),
// which fires during static initialisation before this function runs.
// -----------------------------------------------------------------------------

// Pull in the registrar types WITHOUT activating the RegisterCommands guard
// macro (which is only injected when plugin developer headers are included).
// We define the real function here, so we include the internals directly.
#include "cmdsdk/ProviderRegistrar.hpp"

// Immediately undef the guard so this TU can legitimately define the symbol.
#undef RegisterCommands

#include <iostream>
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"

extern "C" CMDSDK_API void RegisterCommands(cmdsdk::CommandRegistry& registry) {
  cmdsdk::ProviderRegistrar::instance().registerAll(registry);
}