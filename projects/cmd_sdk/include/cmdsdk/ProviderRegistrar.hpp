#pragma once

// -----------------------------------------------------------------------------
// cmdsdk/ProviderRegistrar.hpp
//
// Guard rail: plugin developers MUST NOT define RegisterCommands themselves.
// Instead, they declare their provider with CMDSDK_REGISTER_PROVIDER(ClassName).
//
// A single RegisterCommands symbol is provided by ProviderRegistrar.cpp
// (compiled into every plugin). It iterates all self-registered factories
// and calls registerCommand for each one.
//
// Usage (in each CmdProvider .cpp file):
//
//   #include "cmdsdk/ProviderRegistrar.hpp"
//   CMDSDK_REGISTER_PROVIDER(MathCmdProvider)
//   CMDSDK_REGISTER_PROVIDER(GeometryCmdProvider)
//
// That's it. No RegisterCommands, no boilerplate.
// -----------------------------------------------------------------------------

#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

#include "cmdsdk/ICmd.hpp"
#include "cmdsdk/SubCmd.hpp"           // required for SubCmd base and buildMetadata()
#include "cmdsdk/CommandRegistry.hpp"
#include "cmdsdk/PluginApi.hpp"

// -----------------------------------------------------------------------------
// Compile-time guard: if a plugin TU defines RegisterCommands directly,
// the macro below will cause a clear compile error.
// -----------------------------------------------------------------------------
#define RegisterCommands                                                        \
  static_assert(false,                                                          \
    "[cmdsdk] Do not define RegisterCommands directly in plugin code. "         \
    "Use CMDSDK_REGISTER_PROVIDER(ClassName) in your provider .cpp file "       \
    "and compile ProviderRegistrar.cpp into your plugin instead.")

namespace cmdsdk {

// -----------------------------------------------------------------------------
// ProviderRegistrar
//
// A process-global singleton that collects provider factories via static
// initialisation (before main / RegisterCommands is called).
// Each CMDSDK_REGISTER_PROVIDER invocation pushes one factory here.
// -----------------------------------------------------------------------------
class ProviderRegistrar {
 public:
  using Factory = std::function<std::unique_ptr<ICmd>()>;

  struct Entry {
    Factory factory;
  };

  // Singleton accessor.
  static ProviderRegistrar& instance() {
    static ProviderRegistrar registrar;
    return registrar;
  }

  // Called by CMDSDK_REGISTER_PROVIDER at static-init time.
  void addFactory(Factory factory) {
    entries_.push_back({std::move(factory)});
  }

  // Called once by RegisterCommands (defined in ProviderRegistrar.cpp).
  // Instantiates each provider once to call buildMetadata(), then registers it.
  // All factories are guaranteed to produce SubCmd* by AutoRegister's static_assert.
  void registerAll(CommandRegistry& registry) const {
    for (const auto& entry : entries_) {
      auto instance = entry.factory();
      // Safe: AutoRegister<T> enforces std::is_base_of<SubCmd, T> at compile time.
      auto* provider = static_cast<SubCmd*>(instance.get());

      std::string error;
      if (!registry.registerCommand(
              provider->buildMetadata(),
              entry.factory,
              error)) {
        fprintf(stderr,
                "[cmdsdk] ProviderRegistrar: failed to register provider: %s\n",
                error.c_str());
      }
    }
  }

 private:
  ProviderRegistrar() = default;
  std::vector<Entry> entries_;
};

// -----------------------------------------------------------------------------
// AutoRegister<T>
//
// Instantiated at namespace scope by the macro below. Its constructor fires
// during static initialisation and pushes the provider factory into the
// ProviderRegistrar singleton before RegisterCommands is ever called.
// -----------------------------------------------------------------------------
template <typename T>
struct AutoRegister {
  AutoRegister() {
    // Compile-time guard: every registered provider must extend cmdsdk::SubCmd.
    // If you see this error, make your provider class inherit from cmdsdk::SubCmd.
    static_assert(std::is_base_of<SubCmd, T>::value,
                  "[cmdsdk] CMDSDK_REGISTER_PROVIDER requires T to extend cmdsdk::SubCmd. "
                  "Ensure your provider inherits from cmdsdk::SubCmd.");
    ProviderRegistrar::instance().addFactory(
        []() -> std::unique_ptr<ICmd> { return std::make_unique<T>(); });
  }
};

}  // namespace cmdsdk

// -----------------------------------------------------------------------------
// CMDSDK_REGISTER_PROVIDER(ClassName)
//
// Place this macro once per provider class, in the provider's .cpp file,
// at file scope (outside any namespace).
//
// It creates a file-static AutoRegister<ClassName> whose constructor runs
// at static-init time to enqueue the provider factory.
//
// The macro deliberately uses __COUNTER__ to guarantee a unique variable name
// even when multiple providers are registered in the same translation unit.
// -----------------------------------------------------------------------------
#define CMDSDK_CONCAT_IMPL(a, b) a##b
#define CMDSDK_CONCAT(a, b) CMDSDK_CONCAT_IMPL(a, b)

#define CMDSDK_REGISTER_PROVIDER(ClassName)                                \
  static ::cmdsdk::AutoRegister<ClassName>                                 \
      CMDSDK_CONCAT(_cmdsdk_auto_register_, __COUNTER__)  // NOLINT