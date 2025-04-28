#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <sys/__messag.h>
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
#include "strutils.hpp"

#include "argparse/argparse.hpp"

#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"

#pragma runopts(posix(on))

using namespace strutils;

extern "OS_UPSTACK" { int bpxwdyn_fn(const char *); }

using bpxwdyn_t = decltype(bpxwdyn_fn);

struct UnixBatchRunner {

    static UnixBatchRunner * instance;
    int shutdown_ecb = 0;
    int return_code = 0;
    int fd_map[3];
    pid_t child = 0;

    UnixBatchRunner() { instance = this; }

    void alloc(const std::string &alloc) {
        static bpxwdyn_t *bpxwdyn = nullptr;
        if (!bpxwdyn) {
            bpxwdyn = reinterpret_cast<bpxwdyn_t *>(fetch("BPXWDY2"));
            if (!bpxwdyn) throw std::runtime_error("Fetch failed for BPXWDY2");
        }
        if (bpxwdyn(alloc.c_str()) != 0) {
            throw std::runtime_error("BPXWDYN allocation failed");
        }
    }

    int duplicate(int oldfd) {
        int fd = dup(oldfd);
        if (fd == -1) throwError("dup() failed");
        return fd;
    }

    struct delete_ptr {
        template<typename T>
        void operator()(T *ptr) const { std::free((void *) ptr); }
    };

    void kill_process(pid_t pid, int signal) {
        spdlog::debug("Sending signal {} to PID {}", signal, pid);
        kill(-pid, signal);
        int status;
        waitpid(pid, &status, 0);
    }

    static void handle_sigchld(int /*sig*/) {
        spdlog::debug("Handling SIGCHLD signal");
        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0) {} // reap zombies
        if (WIFEXITED(status)) {
            instance->return_code = WEXITSTATUS(status);
        }
        post_shutdown_ecb(&instance->shutdown_ecb);
    }

    static void post_shutdown_ecb(int *ecb) {
        spdlog::debug("Posting shutdown ECB from timer");
        __asm(" POST (%[ecb]),0\n" : : [ecb]"a"(ecb) : "r0", "r1");
    }

    std::vector<const char *> make_env() {
        std::vector<const char *> envp = {
            dupstr("LIBPATH=/lib:/usr/lib"),
            dupstr("PATH=/bin:/usr/bin"),
            dupstr("_BPXK_AUTOCVT=ON"),
            dupstr("_BPXK_JOBLOG=STDERR"),
            dupstr("_BPX_SPAWN_SCRIPT=YES"),
            dupstr("_EDC_ADD_ERRNO2=1")
        };

        const char *userid = __getlogin1();
        if (!userid) throwError("__getlogin1() failed");

        struct passwd *p = getpwnam(userid);
        if (!p) throwError("getpwnam() failed");

        envp.push_back(dupstr("HOME=" + std::string(p->pw_dir)));
        envp.push_back(dupstr("PWD=" + std::string(p->pw_dir)));

        bool share_address_space = true; // default to sharing address space
        std::ifstream stdenv("//DD:STDENV");
        if (stdenv) {
            std::string line;
            while (std::getline(stdenv, line)) {
                ltrim(line);
                if (!line.empty() && line[0] != '#') {
                    if (starts_with(line, "_BPX_SHAREAS=")) { // start_with hack
                        share_address_space = false; // use the explict value in STDENV
                    }
                    envp.push_back(dupstr(line));
                }
            }
        }
        if (share_address_space) {
            envp.push_back(dupstr("_BPX_SHAREAS=MUST"));
        }
        envp.push_back(nullptr);

        return envp;
    }

    void setup_signal_handlers() {
        signal(SIGPIPE, SIG_IGN);
        struct sigaction sa = {};
        sa.sa_handler = UnixBatchRunner::handle_sigchld;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
        if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
            throwError("sigaction() failed");
        }
    }

    void spawn_program(const std::vector<std::string> &args) {
        auto envp = make_env();
        if (!args.empty()) {
            spdlog::info("Running program {}", args[0]);
            std::vector<const char *> spawn_args;
            for (auto &arg: args) spawn_args.push_back(dupstr(arg));
            spawn_args.push_back(nullptr);

            __inheritance inherit = {};
            inherit.flags = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
            inherit.pgroup = SPAWN_NEWPGROUP;

            child = __spawnp2(spawn_args[0], 3, fd_map, &inherit, &spawn_args[0], &envp[0]);
            std::for_each(spawn_args.begin(), spawn_args.end(), delete_ptr());

            if (child == -1) throwError("__spawnp2() failed running program " + std::string(spawn_args[0]));
        } else {
            const char *userid = __getlogin1();
            struct passwd *p = getpwnam(userid);
            std::string shell_cmd = "-" + std::string(p->pw_shell);

            const char *spawn_argv[] = {shell_cmd.c_str(), nullptr};

            child = __spawnp2(p->pw_shell, 3, fd_map, nullptr, spawn_argv, &envp[0]);
            if (child == -1) throwError("__spawnp2() failed running shell");
        }

        std::for_each(envp.begin(), envp.end(), delete_ptr());
    }

    int run(int argc, const char *argv[]) {
        spdlog::set_level(spdlog::level::trace);
        bool disable_console_commands = false;
        std::string log_level;
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
            .help("the name of the program to run. Default is the shell");

        program.parse_args(argc, argv);

        if (log_level == "trace") spdlog::set_level(spdlog::level::trace);
        else if (log_level == "debug") spdlog::set_level(spdlog::level::debug);
        else if (log_level == "info") spdlog::set_level(spdlog::level::info);
        else if (log_level == "warn") spdlog::set_level(spdlog::level::warn);
        else if (log_level == "error") spdlog::set_level(spdlog::level::err);

        File sysout("//DD:SYSOUT", "w", false);
        if (!sysout.is_open()) {
            alloc("ALLOC FI(SYSOUT) SYSOUT(X) MSG(2)");
            sysout.open("//DD:SYSOUT", "w");
        }

        File dataset_stdin("//DD:STDIN", "r");
        File dataset_stdout("//DD:STDOUT", "w", false);
        File dataset_stderr("//DD:STDERR", "w", false);

        File * dataset_stdout_ptr = dataset_stdout.is_open() ? &dataset_stdout : &sysout;
        File * dataset_stderr_ptr = dataset_stderr.is_open() ? &dataset_stderr : &sysout;

        Pipe pipe_stdin, pipe_stdout, pipe_stderr;

        fd_map[0] = duplicate(pipe_stdin.read_handle());
        fd_map[1] = duplicate(pipe_stdout.write_handle());
        fd_map[2] = duplicate(pipe_stderr.write_handle());

        pipe_stdin.close(Pipe::READ);
        pipe_stdout.close(Pipe::WRITE);
        pipe_stderr.close(Pipe::WRITE);

        umask(022);
        setup_signal_handlers();

        if (!disable_console_commands) {
            std::thread console_thread([this]() {
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
                        kill_process(child, SIGTERM);
                    }
                }
            });
            console_thread.detach();
        }

        spawn_program(program.get<std::vector<std::string>>("program"));

        int maxfd = std::max({pipe_stdin.write_handle(), pipe_stdout.read_handle(), pipe_stderr.read_handle()});
        char buf[4096];

        while (true) {
            fd_set readfds, writefds;
            FD_ZERO(&readfds);
            FD_ZERO(&writefds);

            if (pipe_stdin.is_write_open()) {
                FD_SET(pipe_stdin.write_handle(), &writefds);
            }

            FD_SET(pipe_stdout.read_handle(), &readfds);
            FD_SET(pipe_stderr.read_handle(), &readfds);

            int select_rc = selectex(maxfd + 1, &readfds, &writefds, nullptr, nullptr, &shutdown_ecb);

            if (select_rc < 0) throwError("selectex() failed");
            if (select_rc == 0) break; // shutdown requested

            if (pipe_stdin.is_write_open() && FD_ISSET(pipe_stdin.write_handle(), &writefds)) {
                int bytes_read = dataset_stdin.read(buf, sizeof(buf));
                spdlog::trace("Read {} bytes from STDIN", bytes_read);
                if (bytes_read > 0) {
                    pipe_stdin.write(buf, bytes_read);
                } else {
                    pipe_stdin.close(Pipe::WRITE);
                    dataset_stdin.close();
                }
            }
            if (FD_ISSET(pipe_stdout.read_handle(), &readfds)) {
                int bytes_read = pipe_stdout.read(buf, sizeof(buf));
                dataset_stdout_ptr->write(buf, bytes_read);
            }
            if (FD_ISSET(pipe_stderr.read_handle(), &readfds)) {
                int bytes_read = pipe_stderr.read(buf, sizeof(buf));
                dataset_stderr_ptr->write(buf, bytes_read);
            }
        }

        return return_code;
    }
};

UnixBatchRunner *UnixBatchRunner::instance = nullptr;

int main(int argc, const char *argv[]) {
    setenv("_EDC_ADD_ERRNO2", "1", 1);
    try {
        UnixBatchRunner runner;
        return runner.run(argc, argv);
    } catch (const std::exception &e) {
        spdlog::error(e.what());
        return 12;
    }
}