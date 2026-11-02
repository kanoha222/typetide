//
//  SettingsDiagnostics.h — private noexcept logging boundary for the settings modules.
//
//  util::Log can allocate (debug path / AppDataDir) and therefore throw. Settings
//  code logs from destructors, noexcept helpers, catch handlers and from the
//  point after a durable commit has already been published; a throwing log call
//  there must never terminate the process or turn a committed save into a
//  reported failure. Arguments are restricted to fixed-format primitives
//  (error codes); never pass user text, paths or credentials.
//
#pragma once
#include "Util.h"
#include <type_traits>

namespace settings_diagnostics {

template <typename... Args>
inline void Log(const char* format, Args... args) noexcept {
    static_assert((std::is_arithmetic_v<Args> && ...),
                  "settings diagnostics accept primitive arguments only");
    try {
        util::Log(format, args...);
    } catch (...) {
        // Fixed, allocation-free fallback; the original diagnostic is intentionally not retried.
        OutputDebugStringW(L"TypeTide: settings diagnostic unavailable\n");
    }
}

} // namespace settings_diagnostics
