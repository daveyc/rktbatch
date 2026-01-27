#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/msg.h>
#ifdef __MVS__
#include <sys/__messag.h>
#endif
#include <spawn.h>
#include <pthread.h>
#include <pwd.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>

#include "errors.hpp"
#include "file.hpp"
#include "pipe.hpp"
#include "strings.hpp"
#include "syscalls.hpp"
#include "c_string_vector.hpp"

#include "argparse/argparse.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

#pragma runopts(posix(on))

namespace syscalls = rkt::syscalls;
namespace strings = rkt::strings;

extern "OS_UPSTACK" {
    int bpxwdyn_fn(const char*);
}

using bpxwdyn_t = decltype(bpxwdyn_fn);

static int shutdown_ecb = 0;
static int fd_map[3];
static pid_t child_pid = 0;

// Post the given ECB to wake a waiting select or any WAIT.
static void post_shutdown_ecb(int* ecb) {
    __asm(" POST (%[ecb]),0\n" : : [ecb]"a"(ecb) : "r0", "r1");
}

// SIGCHLD handler.
// Posts the shutdown ECB to wake the main select loop.
static void handle_sigchld(int /*sig*/) {
    post_shutdown_ecb(&shutdown_ecb);
}

// Allocate a z/OS dataset using BPXWDYN. Throws on failure.
static void alloc(const std::string& alloc) {
    static bpxwdyn_t* bpxwdyn = nullptr;
    if (!bpxwdyn) {
        bpxwdyn = reinterpret_cast<bpxwdyn_t*>(fetch("BPXWDY2"));
        if (!bpxwdyn) throw std::runtime_error("Fetch failed for BPXWDY2");
    }
    if (bpxwdyn(alloc.c_str()) != 0) {
        throw std::runtime_error("BPXWDYN allocation failed");
    }
}

// Sends a signal to the process group (-pid) then waits for the child.
// Note: waitpid may block; ensure this is not called from a signal handler.
static void kill_process(pid_t pid, int signal) {
    spdlog::debug("Sending signal {} to PID {}", signal, pid);
    syscalls::checked_kill(-pid, signal);
}
// create the environment variables array for the spawned process.
static rkt::c_string_vector make_env() {
    rkt::c_string_vector envp = {
        "LIBPATH=/lib:/usr/lib",
        "PATH=/bin:/usr/bin",
        "_BPXK_AUTOCVT=ON",
        "_BPXK_JOBLOG=STDERR",
        "_BPX_SPAWN_SCRIPT=YES",
        "_EDC_ADD_ERRNO2=1"
    };

    // Set HOME and PWD to the user's home directory.
    passwd* p = syscalls::checked_getpwnam(syscalls::checked_getlogin1());
    envp.push_back("HOME=" + std::string(p->pw_dir));
    envp.push_back("PWD=" + std::string(p->pw_dir));

    // Read additional environment variables from STDENV data set.
    bool share_address_space = true; // default to sharing address space
    std::ifstream stdenv_file("//DD:STDENV");
    if (stdenv_file) {
        std::string line;
        while (std::getline(stdenv_file, line)) {
            strings::ltrim(line);
            if (!line.empty() && line[0] != '#') {
                if (strings::starts_with(line, "_BPX_SHAREAS=")) {
                    share_address_space = false; // use the explict value in STDENV
                }
                envp.push_back(line);
            }
        }
    }
    // If not overridden, enforce shared address space.
    if (share_address_space) {
        envp.push_back("_BPX_SHAREAS=MUST");
    }
    envp.push_back(nullptr);

    return envp;
}

// Install signal handlers used by the batch runner.
// SIGPIPE is ignored and SIGCHLD triggers shutdown handling.
static void setup_signal_handlers() {
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa = {};
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    syscalls::checked_sigaction(SIGCHLD, &sa, nullptr);
}

// Spawn the target program or login shell with redirected I/O.
// Sets up process group and inheritance options.
static void spawn_program(rkt::c_string_vector& args) {
    spdlog::debug("Spawning program...");
    auto envp = make_env();
    if (!args.is_empty()) {
        spdlog::debug("Running program {}", args[0]);
        args.push_back(nullptr); // Null-terminate argv array
        __inheritance inherit = {};
        inherit.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
        inherit.pgroup = SPAWN_NEWPGROUP;
        child_pid = syscalls::checked_spawnp2(args[0], 3, fd_map, &inherit, &args[0], &envp[0]);
    } else {
        // No program specified; spawn the user's login shell.
        spdlog::debug("No program specified; spawning login shell");
        const char* userid = syscalls::checked_getlogin1();
        passwd* p = syscalls::checked_getpwnam(userid);
        std::string shell_cmd = "-" + std::string(p->pw_shell);
        const char* spawn_argv[] = {shell_cmd.c_str(), nullptr};
        child_pid = syscalls::checked_spawnp2(p->pw_shell, 3, fd_map, nullptr, spawn_argv, &envp[0]);
    }
}

// Main execution loop.
// Parses arguments, sets up I/O redirection, spawns the child, and relays stdin/stdout/stderr until termination.
static int run(int argc, const char* argv[]) {
    bool disable_console_commands = false;
    std::string log_level;
    std::vector<std::string> program_args;
    argparse::ArgumentParser program("RKTBATCH");
    program.add_argument("--disable-console-commands")
           .help("disables console commands; by default only STOP (P) is supported")
           .store_into(disable_console_commands);
    program.add_argument("--log-level")
           .help("the log level - trace, debug, info, warn, error")
           .default_value(std::string{"info"})
           .choices("trace", "debug", "info", "warn", "error")
           .store_into(log_level);
    program.add_argument("program")
           .remaining()
           .store_into(program_args)
           .help("the name of the program to run. Default is the shell");

    program.parse_args(argc, argv);

    if (log_level == "trace") spdlog::set_level(spdlog::level::trace);
    else if (log_level == "debug") spdlog::set_level(spdlog::level::debug);
    else if (log_level == "info") spdlog::set_level(spdlog::level::info);
    else if (log_level == "warn") spdlog::set_level(spdlog::level::warn);
    else if (log_level == "error") spdlog::set_level(spdlog::level::err);

    // Ensure SYSOUT is allocated.
    rkt::file sysout("//DD:SYSOUT", "w", false);
    if (!sysout.is_open()) {
        alloc("ALLOC FI(SYSOUT) SYSOUT(X) MSG(2)");
        sysout.open("//DD:SYSOUT", "w");
    }

    // Open STDIN, STDOUT, STDERR datasets.
    rkt::file dataset_stdin("//DD:STDIN", "r");
    rkt::file dataset_stdout("//DD:STDOUT", "w", false);
    rkt::file dataset_stderr("//DD:STDERR", "w", false);

    spdlog::debug("stdout.is_open({}), stderr.is_open({}))",
                  dataset_stdout.is_open() ? "true" : "false",
                  dataset_stderr.is_open() ? "true" : "false");

    // Use SYSOUT if STDOUT or STDERR datasets are not allocated.
    rkt::file* dataset_stdout_ptr = dataset_stdout.is_open() ? &dataset_stdout : &sysout;
    rkt::file* dataset_stderr_ptr = dataset_stderr.is_open() ? &dataset_stderr : &sysout;

    // Create pipes for child process I/O redirection.
    rkt::pipe pipe_stdin, pipe_stdout, pipe_stderr;
    fd_map[0] = syscalls::dup(pipe_stdin.read_handle());
    fd_map[1] = syscalls::dup(pipe_stdout.write_handle());
    fd_map[2] = syscalls::dup(pipe_stderr.write_handle());

    // Close unused pipe ends in the parent process.
    pipe_stdin.close_read();
    pipe_stdout.close_write();
    pipe_stderr.close_write();

    setup_signal_handlers();

    // Start console command listener thread if not disabled.
    if (!disable_console_commands) {
        std::thread console_thread([]() {
            spdlog::info("Listening for console commands");
            int concmd = 0;
            char modstr[128] = {};
            while (true) {
                if (__console(nullptr, modstr, &concmd) == -1) {
                    spdlog::warn("__console() {}: {}", (errno == EINTR ? "interrupted" : "error", strerror(errno)));
                    break;
                }
                if (concmd == _CC_stop) {
                    spdlog::info("STOP command received");
                    kill_process(child_pid, SIGTERM);
                }
            }
        });
        console_thread.detach();
    }

    rkt::c_string_vector args(program_args);
    spawn_program(args);

    // Main I/O relay loop.
    // - Build read/write fd_sets for select: monitor child's stdout/stderr for readability
    //   and the parent → child stdin pipe for writability.
    // - Use selectex with the shutdown ECB so the loop can be interrupted by SIGCHLD.
    // - If selectex returns 0, the shutdown ECB was posted → exit the loop.
    // - When the stdin pipe is writable, read from the STDIN dataset and write to the
    //   child's stdin pipe. If read returns <= 0, close the write end to signal EOF
    //   to the child and close the dataset.
    // - When the child's stdout/stderr are readable, read from the corresponding pipe
    //   and write to the appropriate dataset (STDOUT/STDERR or SYSOUT fallback).
    int maxfd = std::max({pipe_stdin.write_handle(), pipe_stdout.read_handle(), pipe_stderr.read_handle()});
    char buf[4096];

    while (true) {
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        // Monitor stdin pipe for writability if still open.
        // This indicates we can send more data to the child.
        if (pipe_stdin.is_write_open()) {
            FD_SET(pipe_stdin.write_handle(), &writefds);
        }
        // Monitor child's stdout and stderr for readable data.
        FD_SET(pipe_stdout.read_handle(), &readfds);
        FD_SET(pipe_stderr.read_handle(), &readfds);
        // Wait for I/O activity or for the shutdown ECB to be posted by the SIGCHLD handler.
        int select_rc = syscalls::checked_selectex(
            maxfd + 1,
            &readfds,
            &writefds,
            nullptr,
            nullptr,
            &shutdown_ecb
        );
        // selectex returned because the shutdown ECB was posted (child exited or shutdown requested).
        if (select_rc == 0) break;
        // If the child's stdin pipe is writable, feed it data from the STDIN dataset.
        if (pipe_stdin.is_write_open() &&
            FD_ISSET(pipe_stdin.write_handle(), &writefds)) {
            int bytes_read = dataset_stdin.read(buf, sizeof(buf));
            spdlog::trace("Read {} bytes from STDIN", bytes_read);
            if (bytes_read > 0) {
                // Forward input to the child.
                (void)pipe_stdin.write(buf, bytes_read);
            } else {
                // EOF on input: close write end so the child receives EOF on stdin.
                spdlog::debug("Close the write end of the pipe to signal EOF to the child");
                pipe_stdin.close_write();
                dataset_stdin.close();
            }
        }
        // Child stdout is readable: forward to STDOUT dataset.
        if (FD_ISSET(pipe_stdout.read_handle(), &readfds)) {
            int bytes_read = pipe_stdout.read(buf, sizeof(buf));
            (void)dataset_stdout_ptr->write(buf, bytes_read);
        }
        // Child stderr is readable: forward to STDERR.
        if (FD_ISSET(pipe_stderr.read_handle(), &readfds)) {
            int bytes_read = pipe_stderr.read(buf, sizeof(buf));
            (void)dataset_stderr_ptr->write(buf, bytes_read);
        }
    }

    int return_code = 0;
    int status = 0;
    syscalls::checked_waitpid(child_pid, &status, 0);
    if (WIFEXITED(status)) {
        return_code = WEXITSTATUS(status);
        spdlog::debug("Child exited with status {} return_code {}", status, return_code);
        // Normalize SIGTERM exit code to 0.
        if (int SIGTERM_EXIT = 128 + SIGTERM; return_code == SIGTERM_EXIT) { return_code = 0; }
    }
    return return_code;
}

int main(int argc, const char* argv[]) {
    setenv("_EDC_ADD_ERRNO2", "1", 1);
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        spdlog::error(e.what());
        return 12;
    }
}
