#pragma once

#include <cstdio>
#include <string>

#include "errors.hpp"

namespace rkt {

/**
 * RAII wrapper around a C stdio FILE handle.
 *
 * This class provides scoped ownership of a FILE pointer and guarantees
 * that the underlying stream is closed when the File object is destroyed.
 * Copy construction and copy assignment are disabled to enforce unique
 * ownership semantics. Ownership may be transferred using move construction
 * or move assignment.
 *
 * Files may be opened using a pathname via fopen or by adopting an existing
 * file descriptor via fdopen. The associated file descriptor is cached and
 * can be queried using fileno().
 *
 * Unless otherwise noted, operations throw on error. Callers are expected
 * to ensure the file is open before invoking read or write operations.
 *
 * This class is not thread safe.
 */
class file {
private:
    using ptr = FILE*;

    /** Owned FILE handle, or nullptr if no file is open */
    ptr m_handle{nullptr};

    /** File descriptor associated with the FILE handle, or -1 if none */
    int m_fd{-1};

public:
    /**
     * Default constructor.
     *
     * Constructs an empty File with no open FILE handle.
     */
    file() {}

    /**
     * Constructs a File and opens the specified file.
     *
     * @param name Path to the file to open
     * @param mode fopen mode string
     * @param throwOnError If true, throws if the file cannot be opened
     */
    file(std::string const& name,
         std::string const& mode = std::string("r"),
         bool throwOnError = true) : file() {
        open(name, mode, throwOnError);
    }

    /** Copy construction is disabled */
    file(file const&) = delete;

    /** Copy assignment is disabled */
    file& operator=(file const&) = delete;

    /**
     * Move constructor.
     *
     * Transfers ownership of the FILE handle from another File.
     * The source File is left empty.
     *
     * @param other File to move from
     */
    file(file&& other) noexcept
        : m_handle(other.m_handle), m_fd(other.m_fd)
    {
        other.m_handle = nullptr;
        other.m_fd = -1;
    }

    /**
     * Move assignment operator.
     *
     * Closes any currently open file and transfers ownership of the
     * FILE handle from another File. The source File is left empty.
     *
     * @param other File to move from
     * @return reference to this File
     */
    file& operator=(file&& other) noexcept {
        if (this != &other) {
            close();
            m_handle = other.m_handle;
            m_fd = other.m_fd;
            other.m_handle = nullptr;
            other.m_fd = -1;
        }
        return *this;
    }

    /**
     * Destructor.
     *
     * Closes the file if it is still open.
     */
    ~file() noexcept { close(); }

    /**
     * Opens a file by pathname.
     *
     * If throwOnError is false, failures leave the File unopened.
     *
     * @param name Path to the file
     * @param mode fopen mode string
     * @param throwOnError If true, throws if the file cannot be opened
     */
    void open(std::string const& name,
              std::string const& mode,
              bool throwOnError = true) {
        close();
        m_handle = fopen(name.c_str(), mode.c_str());
        if (m_handle == nullptr) {
            m_fd = -1;
            if (throwOnError) throwError("Error opening file " + name);
        } else {
            m_fd = ::fileno(m_handle);
        }
    }

    /**
     * Attempts to open a file by pathname without throwing.
     *
     * @param name Path to the file
     * @param mode fopen mode string
     * @return true if the file was successfully opened
     */
    bool try_open(std::string const& name,
                  std::string const& mode) {
        open(name, mode, false);
        return is_open();
    }

    /**
     * Opens a FILE stream by adopting an existing file descriptor.
     *
     * @param fd File descriptor to associate with a FILE stream
     * @param mode fdopen mode string
     *
     * @throws if the descriptor cannot be associated with a FILE handle
     */
    void open(int fd, std::string const& mode) {
        close();
        m_handle = fdopen(fd, mode.c_str());
        if (m_handle == nullptr) throwError("fdopen");
        m_fd = ::fileno(m_handle);
    }

    /**
     * Returns the underlying FILE pointer.
     *
     * Intended for interoperability with APIs that require a FILE*.
     *
     * @return the owned FILE pointer, or nullptr if no file is open
     */
    explicit operator ptr() const noexcept { return m_handle; }

    /**
     * Returns the file descriptor associated with the FILE handle.
     *
     * @return file descriptor, or -1 if no file is open
     */
    int fileno() const noexcept { return m_fd; }

    /**
     * Indicates whether a file is currently open.
     *
     * @return true if a FILE handle is present
     */
    bool is_open() const noexcept { return m_handle != nullptr; }

    /**
     * Closes the file if one is currently open.
     *
     * Safe to call multiple times.
     */
    void close() noexcept {
        if (m_handle) {
            fclose(m_handle);
            m_handle = nullptr;
        }
        m_fd = -1;
    }

    /**
     * Reads up to the specified number of bytes from the file.
     *
     * @param buffer Destination buffer
     * @param size Maximum number of bytes to read
     * @return number of bytes read
     *
     * @throws on read error
     */
    size_t read(void* buffer, size_t size) const {
        if (!m_handle) throwError("File not open");
        clearerr(m_handle);
        size_t bytesRead = fread(buffer, 1, size, m_handle);
        if (ferror(m_handle)) throwError("Error reading from file");
        return bytesRead;
    }

    /**
     * Writes the specified number of bytes to the file.
     *
     * @param buf Source buffer
     * @param size Number of bytes to write
     * @return number of bytes written
     *
     * @throws on short write or write error
     */
    size_t write(const void* buf, size_t size) const {
        if (!m_handle) throwError("File not open");
        clearerr(m_handle);
        size_t bytesWritten = fwrite(buf, 1, size, m_handle);
        if (bytesWritten != size) throwError("Error writing to file");
        return bytesWritten;
    }
};

}
