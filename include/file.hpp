#pragma once

#include <cstdio>

/** Wrapper for stdio stream API with RAII semantics */
class File {
private:
    typedef FILE *ptr;
    ptr handle;
    int fd;

public:
    File() : handle(0), fd(0) { }

    File(std::string const &name, std::string const &mode = std::string("r"), bool throwOnError = true) {
        open(name, mode, throwOnError);
    }

    File(int fd, std::string const &mode = std::string("r")) { open(fd, mode); }

    void open(std::string const &name, std::string const &mode, bool throwOnError = true) {
        handle = fopen(name.c_str(), mode.c_str());
        if (handle == 0) {
            if (throwOnError) throwError("Error opening file " + name);
        } else fd = ::fileno(handle);
    }

    void open(int fd, std::string const &mode) {
        handle = fdopen(fd, mode.c_str());
        if (handle == 0) throwError("fdopen");
        this->fd = ::fileno(handle);
    }

    operator ptr() const { return handle; }

    int fileno() const { return fd; }

    bool is_open() const { return handle != 0; }

    ~File() { close(); }

    void close() { if (handle) {fclose(handle); handle = 0; } }

    int read(void *buffer, size_t size) {
        int bytesRead = fread(buffer, 1, size, handle);
        if (ferror(handle)) throwError("Error reading from file");
        return bytesRead;
    }

    int write(const void *buf, size_t size) {
        auto bytesWritten = fwrite(buf, 1, size, handle);
        if (bytesWritten != size) throwError("Error writing to file");
        return bytesWritten;
    }
};
