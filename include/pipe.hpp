#pragma once

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>

#include <utility>

#include "errors.hpp"

namespace rkt {

/**
 * RAII wrapper around a POSIX pipe.
 *
 * The Pipe class owns the two file descriptors created by pipe and ensures
 * they are closed when the Pipe object is destroyed. Copy construction and
 * copy assignment are disabled to enforce unique ownership. Ownership may be
 * transferred using move construction or move assignment.
 *
 * The pipe exposes a read end and a write end, accessible via READ and WRITE
 * constants. Helper methods are provided to query, close, and use each end.
 *
 * Operations throw on error unless otherwise noted.
 *
 * This class is not thread safe.
 */
class pipe {
private:
    int fd[2]{-1, -1};

public:
    using size_t = std::size_t;
    static constexpr int READ = 0;
    static constexpr int WRITE = 1;

    /**
     * Default constructor.
     *
     * Creates a new pipe using the POSIX pipe system call.
     *
     * @throws if the pipe cannot be created
     */
    pipe() {
        errno = 0;
        if (const int rc = ::pipe(fd); rc == -1) throwError("Error creating pipe");
    }

    pipe(pipe const& other) = delete;
    pipe& operator=(pipe const& other) = delete;

    /**
     * Move constructor.
     *
     * Transfers ownership of both pipe file descriptors from another Pipe.
     * The source Pipe is left empty.
     */
    pipe(pipe&& other) noexcept
        : fd{other.fd[0], other.fd[1]}
    {
        other.fd[0] = -1;
        other.fd[1] = -1;
    }

    /**
     * Move assignment operator.
     *
     * Closes any open file descriptors and transfers ownership from another
     * Pipe. The source Pipe is left empty.
     */
    pipe& operator=(pipe&& other) noexcept {
        if (this != &other) {
            close();
            fd[0] = other.fd[0];
            fd[1] = other.fd[1];
            other.fd[0] = -1;
            other.fd[1] = -1;
        }
        return *this;
    }

    /**
     * Destructor.
     *
     * Closes both ends of the pipe if they are still open.
     */
    ~pipe() noexcept {
        close();
    }

    /**
     * Returns the file descriptor for the specified side of the pipe.
     *
     * @param side Either READ or WRITE
     * @return file descriptor for the requested side
     *
     * @throws std::invalid_argument if side is not READ or WRITE
     */
    int fileno(int side) const {
        if (side < READ || side > WRITE) throw std::invalid_argument("Logic error: Pipe indexes must be 0 or 1");
        return fd[side];
    }

    int read_handle() const { return fileno(READ); }
    int write_handle() const { return fileno(WRITE); }

    bool is_open(int side) const {
        int f = fileno(side);
        return f != -1;
    }

    bool is_read_open() const { return is_open(READ); }
    bool is_write_open() const { return is_open(WRITE); }

    /**
     * Closes the specified side of the pipe.
     *
     * @param side Either READ or WRITE
     */
    void close(int side) noexcept {
        if (side < READ || side > WRITE) return;
        int f = fd[side];
        if (f != -1) {
            // Best-effort close; ignore failures
            errno = 0;
            ::close(f);
        }
        fd[side] = -1;
    }

    /**
     * Closes both ends of the pipe.
     */
    void close() noexcept {
        close(READ);
        close(WRITE);
    }
    /** Closes the read end of the pipe. */
    void close_read() noexcept { close(READ); }

    /** Closes the write end of the pipe. */
    void close_write() noexcept { close(WRITE); }

    /**
     * Reads data from the read end of the pipe.
     *
     * This method retries automatically if interrupted by a signal.
     *
     * @param buffer Destination buffer
     * @param size Maximum number of bytes to read
     * @return number of bytes read
     *
     * @throws on read error
     */
    int read(void* buffer, size_t size) const {
        if (!is_read_open()) throwError("Pipe read end not open");
        int bytesRead;
        do {
            errno = 0;
            bytesRead = ::read(fd[READ], buffer, size);
        } while (bytesRead == -1 && errno == EINTR);

        if (bytesRead == -1) throwError("Error reading from pipe");

        return bytesRead;
    }

    /**
     * Writes data to the write end of the pipe.
     *
     * This method retries automatically if interrupted by a signal.
     *
     * @param buf Source buffer
     * @param size Number of bytes to write
     * @return number of bytes written
     *
     * @throws on write error
     */
    int write(const void* buf, size_t size) const {
        if (!is_write_open()) throwError("Pipe write end not open");
        int bytesWritten;
        do {
            errno = 0;
            bytesWritten = ::write(fd[WRITE], buf, size);
        } while (bytesWritten == -1 && errno == EINTR);

        if (bytesWritten == -1) throwError("Error writing to pipe");

        return bytesWritten;
    }

    /**
     * Writes a null terminated string to the write end of the pipe.
     *
     * @param str String to write
     * @return number of bytes written
     */
    int write(const char* str) const {
        return write(str, std::strlen(str));
    }
};

} // namespace rkt