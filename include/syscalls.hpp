#pragma once

#include "errors.hpp"

#include <unistd.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

/**
 * Wrapper functions for system calls that throw exceptions on error.
 */
namespace rkt::syscalls {

[[nodiscard]] inline int dup(int oldfd) {
    const int fd = ::dup(oldfd);
    if (fd == -1) throwError("dup() failed");
    return fd;
}

inline const char* checked_getlogin1() {
    const char* userid = __getlogin1();
    if (!userid) throwError("__getlogin1() failed");
    return userid;
}

inline passwd* checked_getpwnam(const char* userid) {
    passwd* p = ::getpwnam(userid);
    if (!p) throwError("getpwnam() failed");
    return p;
}

inline void checked_sigaction(int sig, const struct sigaction* new_sigaction, struct sigaction* old_sigaction) {
    int rc = ::sigaction(sig, new_sigaction, old_sigaction);
    if (rc == -1) throwError("sigaction() failed");
}

inline pid_t checked_spawnp2(
    const char* file,
    const int fd_count,
    const int fd_map[],
    const struct __inheritance* inherit,
    const char* argv[],
    const char* envp[]) {
    pid_t child = __spawnp2(file, fd_count, fd_map, inherit, argv, envp);
    if (child == -1) throwError("__spawnp2() failed running program " + std::string(argv[0]));
    return child;
}

inline int checked_selectex(
    int nmsgsfds,
    fd_set* readlist,
    fd_set* writelist,
    fd_set* exceptlist,
    timeval* timeout,
    int* ecbptr) {
    int rc = selectex(nmsgsfds, readlist, writelist, exceptlist, timeout, ecbptr);
    if (rc < 0) throwError("selectex() failed");
    return rc;
}

inline void checked_kill(pid_t pid, int sig) {
    if (int rc = kill(pid, sig); rc == -1) throwError("kill() failed");
}

inline pid_t checked_waitpid(pid_t pid, int* status, int options) {
    pid_t child_pid = waitpid(pid, status, options);
    if (child_pid == -1) throwError("waitpid() failed");
    return child_pid;
}

} // namespace rkt::syscalls
