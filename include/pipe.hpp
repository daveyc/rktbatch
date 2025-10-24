#pragma once

#include <unistd.h>

#include <utility>

#include "errors.hpp"

/** Wrapper for pipes with RAII semantics */
class Pipe {
private:
    int fd[2]{-1, -1};

public:
    static constexpr int READ = 0;
    static constexpr int WRITE = 1;

public:
    Pipe() {
        auto rc = pipe(fd);
        if (rc == -1) throwError("Error creating pipe");
    }

    Pipe(Pipe const & other) = delete;
    Pipe & operator=(Pipe const & other) = delete;

    Pipe(Pipe && other) noexcept {
        *this = std::move(other);
    }

    Pipe & operator=(Pipe && other) noexcept {
        if (this != &other) {
            close();
            fd[0] = other.fd[0];
            fd[1] = other.fd[1];
            other.fd[0] = -1;
            other.fd[1] = -1;
        }
        return *this;
    }

    ~Pipe() {
        close();
    }

    int fileno(int side) const {
        if (side < 0 || side > 1) throw std::invalid_argument("Logic error: Pipe indexes must be 0 or 1");
        return fd[side];
    }

    int read_handle() const { return fileno(READ); }

    int write_handle() const { return fileno(WRITE); }

    bool is_open(int side) const { return fileno(side) != -1; }

    bool is_read_open() const { return is_open(READ); }

    bool is_write_open() const { return is_open(WRITE); }

    void close(int side) { if (is_open(side)) ::close(fd[side]); fd[side] = -1; }

    void close() {
        close(READ);
        close(WRITE);
    }

    int read(void *buffer, size_t size) const {
        int bytesRead;
        do {
            bytesRead = ::read(fd[READ], buffer, size);
        } while (bytesRead == -1 && errno == EINTR);

        if (bytesRead == -1) throwError("Error reading from pipe");

        return bytesRead;
    }

    int write(const void *buf, size_t size) const {
        int bytesWritten;
        do {
            bytesWritten = ::write(fd[WRITE], buf, size);
        } while (bytesWritten == -1 && errno == EINTR);

        if (bytesWritten == -1) throwError("Error writing to pipe");

        return bytesWritten;
    }

    int write(const char *str) const  { return write(str,  strlen(str)); }
};
