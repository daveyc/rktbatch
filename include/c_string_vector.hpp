#pragma once

#include <string>
#include <cstdlib>
#include <vector>
#include <iostream>
#include <initializer_list>

#include "spdlog/spdlog.h"

#include "strings.hpp"

namespace rkt {

/**
 * c_string_vector
 *
 * A tiny container that stores heap-allocated C-style strings. The purpose of this container is to create
 * exception safe C-string arrays for use with POSIX system calls.
 *
 * The container owns the pointers it stores (allocated with `strdup`) and releases them with `free` in the destructor.
 */
class c_string_vector {
    std::vector<const char*> data;

public:
    using size_type = std::vector<const char*>::size_type;

    /**
      * Default construct an empty container.
      */
    c_string_vector() = default;

    /**
     * Construct from an initializer list of C strings.
     * @param strings List of null-terminated C strings to duplicate and store.
     */
    c_string_vector(const std::initializer_list<const char*> strings) {
        for (const char* s : strings) {
            push_back(s);
        }
    }

    /**
     * Construct from a vector of std::string.
     * @param strings Vector of std::string to duplicate and store.
     */
    c_string_vector(const std::vector<std::string>& strings) {
        for (const auto& s : strings) {
            push_back(s);
        }
    }

    /**
     * Copying is disabled; this class has unique ownership of its pointers.
     */
    c_string_vector(const c_string_vector&) = delete;
    c_string_vector& operator=(const c_string_vector&) = delete;

    /**
     * Move construct by taking ownership of other's pointers.
     * @param other Rvalue reference to the source container.
     *
     * After the move the source may be left in a valid but unspecified state.
     */

    c_string_vector(const c_string_vector&& other) noexcept
        : data(std::move(other.data)) {
    }

    /**
     * Move construct by taking ownership of other's pointers.
     * @param other Rvalue reference to the source container.
     *
     * After the move the source may be left in a valid but unspecified state.
     */
    c_string_vector&& operator=(const c_string_vector&& other) noexcept {
        data = std::move(other.data);
        return std::move(*this);
    }

    /**
     * Factory function to create a c_string_vector from a vector of std::string.
     * @param sv Vector of std::string to duplicate and store.
     * @return A new c_string_vector containing duplicates of the strings.
     */
    static c_string_vector from(const std::vector<std::string>& sv) {
        c_string_vector csv(sv);
        return csv;
    }

    /**
     * Append a null-terminated C string by duplicating it.
     * @param s Pointer to the source C string (can be null).
     *
     * The string is copied using `strdup`, and the container takes ownership
     * of the allocated buffer.
     */
    void push_back(const char* s) {
        data.push_back(s ? strings::checked_strdup(s) : nullptr);
    }

    /**
     * Append a `std::string` by duplicating its contents.
     * @param s Reference to the source `std::string`.
     *
     * The contents are copied into a heap-allocated C string via `strdup`.
     */
    void push_back(const std::string& s) {
        data.push_back(strings::checked_strdup(s));
    }

    /**
     * Indexed access to a stored C-string pointer.
     *
     * Returns a reference to the pointer stored at the given position.
     * The caller may read or modify the pointer value. The characters
     * pointed to are read only.
     *
     * The index is bounds checked and an exception is thrown if it is
     * out of range.
     *
     * @param pos Index of the element to access.
     * @return Reference to the stored const char pointer.
     * @throws std::out_of_range if pos is not a valid index.
     */
    const char*& operator[](size_type pos) {
        return data.at(pos);
    }

    /**
     * Indexed access to a stored C-string pointer.
     *
     * Returns a read only reference to the pointer stored at the given
     * position. The pointer and the data it refers to cannot be modified.
     *
     * @param pos Index of the element to access.
     * @return Const reference to the stored const char pointer.
     * @throws std::out_of_range if pos is not a valid index.
     */
    const char* const& operator[](size_type pos) const {
        return data.at(pos);
    }

    /**
     * Number of stored strings.
     * @return Number of elements in the container.
     */
    size_type size() const { return data.size(); }

    /**
     * Check whether the container is empty.
     * @return true if the container has no elements, false otherwise.
     */
    bool is_empty() const { return data.empty(); }

    /**
     * Iterator to the beginning of the container.
     * @return Iterator to first element.
     */
    auto begin() { return data.begin(); }

    /**
     * Iterator to the end of the container.
     * @return Iterator past the last element.
     */
    auto end() { return data.end(); }

    /**
     * Destructor frees every allocated C-string.
     */
    ~c_string_vector() {
        for (const char* s : data) {
            std::free(const_cast<char*>(s));
        }
        data.clear();
    }
};

} // namespace rkt