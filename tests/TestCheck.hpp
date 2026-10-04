// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 OpenHDK contributors
#pragma once

#include <iostream>

namespace OpenHDK::Test {

inline int reportFailure(const char* condition, const char* file, int line, int exitCode) {
    std::cerr << file << ':' << line << ": test failed (exit code " << exitCode << ")\n"
              << "  unexpected condition: " << condition << '\n';
    return exitCode;
}

} // namespace OpenHDK::Test

// Use in int-returning test functions. Keep the original failure condition and
// exit code; evaluate the condition once, with its existing short-circuit order.
// Variadic arguments allow commas in braced initializers and lambda captures.
// Unlike assert(), this check also runs when NDEBUG is defined.
#define OPENHDK_FAIL_IF(exitCode, ...) \
    do { \
        if ((__VA_ARGS__)) { \
            return ::OpenHDK::Test::reportFailure(#__VA_ARGS__, __FILE__, __LINE__, (exitCode)); \
        } \
    } while (false)
