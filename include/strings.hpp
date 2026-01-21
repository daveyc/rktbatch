#pragma once

#include <string>
#include <cstring>

#include "spdlog/logger.h"
#include "spdlog/spdlog.h"

namespace rkt::strings {

/**
 * Duplicate a C-string using `strdup()`.
 *
 * Allocates a new null-terminated C-string that is a copy of `old`.
 * The caller is responsible for freeing the returned pointer (e.g. with `free()`).
 *
 * @param old Null-terminated C-string to duplicate.
 * @return Pointer to newly allocated C-string.
 * @throws std::bad_alloc if allocation fails.
 */
inline char* checked_strdup(const char* old) {
    char* s = strdup(old);
    spdlog::trace("Checked strdup {}", s);
    if (!s) throw std::bad_alloc();
    return s;
}

/**
 * Duplicate a `std::string` by delegating to the C-string overload.
 *
 * Allocates a new null-terminated C-string whose contents match `old`.
 * The caller is responsible for freeing the returned pointer (e.g. with `free()`).
 *
 * @param old The std::string to duplicate.
 * @return Pointer to newly allocated C-string.
 * @throws std::bad_alloc if allocation fails.
 */
inline char* checked_strdup(const std::string& old) {
    return checked_strdup(old.c_str());
}

/**
 * Remove leading characters contained in `delims` from `s`.
 *
 * Trims characters from the start of the string while they are present
 * in `delims`. The default `delims` are space, tab and newline.
 * If the string contains only delimiter characters, the result is an empty string.
 *
 * @param s String to trim in-place.
 * @param delims Characters to remove from the start of `s`.
 */
inline void ltrim(std::string& s, const std::string& delims = " \t\n") {
    s.erase(0, s.find_first_not_of(delims));
}

/**
 * Check whether `s` starts with `prefix`.
 *
 * Efficiently tests if `prefix` is a prefix of `s`.
 *
 * @param s The string to test.
 * @param prefix The prefix to look for.
 * @return `true` if `s` begins with `prefix`, otherwise `false`.
 */
inline bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

} // namespace rkt::strings
