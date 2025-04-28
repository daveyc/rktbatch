#pragma once

#include <cstdio>
#include <utility>

/** Wrapper for stdio stream API with RAII semantics */
class File {
private:
    using ptr = FILE*;
    ptr m_handle{nullptr};
    int m_fd{-1};

public:
    File() {}

    File(std::string const &name, std::string const &mode = std::string("r"), bool throwOnError = true) : File() {
        open(name, mode, throwOnError);
    }

    File(File const & other) = delete;
    File & operator=(File const & other) = delete;

    File(File && other) noexcept {
        *this = std::move(other);
    }

    File & operator=(File && other) noexcept {
        if (this != &other) {
            close();
            m_handle = other.m_handle;
            m_fd = other.m_fd;
            other.m_handle = nullptr;
            other.m_fd = -1;
        }
        return *this;
    }

    void open(std::string const &name, std::string const &mode, bool throwOnError = true) {
        m_handle = fopen(name.c_str(), mode.c_str());
        if (m_handle == 0) {
            if (throwOnError) throwError("Error opening file " + name);
        } else m_fd = ::fileno(m_handle);
    }

    bool try_open(std::string const &name, std::string const &mode) {
        open(name, mode, false);
        return is_open();
    }

    void open(int fd, std::string const &mode) {
        m_handle = fdopen(fd, mode.c_str());
        if (m_handle == nullptr) throwError("fdopen");
        this->m_fd = ::fileno(m_handle);
    }

    operator ptr() const { return m_handle; }

    int fileno() const { return m_fd; }

    bool is_open() const { return m_handle != 0; }

    ~File() { close(); }

    void close() { if (m_handle) {fclose(m_handle); m_handle = nullptr; } }

    int read(void *buffer, size_t size) {
        int bytesRead = fread(buffer, 1, size, m_handle);
        if (ferror(m_handle)) throwError("Error reading from file");
        return bytesRead;
    }

    int write(const void *buf, size_t size) {
        auto bytesWritten = fwrite(buf, 1, size, m_handle);
        if (bytesWritten != size) throwError("Error writing to file");
        return bytesWritten;
    }
};
