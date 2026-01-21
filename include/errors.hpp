#pragma once 

#include <stdexcept>
#include <cerrno>

#include <string>
#include <cstring>

/**
 * Throws a std::runtime_error with a message that includes the current errno description.
 *
 * @param msg The custom error message to include.
 * @throws std::runtime_error Always throws with the constructed error message.
 */
[[noreturn]] inline void throwError(const std::string& msg) {
    std::string detail;
    if (errno != 0) detail = std::string(": ") + std::strerror(errno);
    throw std::runtime_error(msg + detail);
}