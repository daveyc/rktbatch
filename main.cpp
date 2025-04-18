#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <sys/__messag.h>
#include <spawn.h>
#include <pthread.h>
#include <pwd.h>
#include <ctest.h>
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

#include "include/errors.hpp"
#include "include/file.hpp"
#include "include/pipe.hpp"

#pragma runopts(posix(on))

extern "OS_UPSTACK" { typedef int bpxwdyn_t(const char *); }

static void alloc(const std::string &alloc) {
    static bpxwdyn_t *bpxwdyn;
    if (bpxwdyn == 0) {
        bpxwdyn = reinterpret_cast<bpxwdyn_t *>(fetch("BPXWDYN"));
        if (bpxwdyn == 0) std::runtime_error("Fetch failed for BPXWDYN");
    }
    int rc = bpxwdyn(alloc.c_str());
    if (rc != 0) std::runtime_error("BPXWDYN allocation failed");
}

static int duplicate(int oldfd) {
    int fd = dup(oldfd);
    if (fd == -1) throwError("dup() failed");
    return fd;
}

struct DeletePtr {
    template<typename T>
    void operator()(T *ptr) const { std::free((void *)ptr); }
};

static char* dupstr(const char *old) {
    char *s = strdup(old);
    if (!s) throw std::bad_alloc();
    return s;
}

static char* dupstr(const std::string &old) { return dupstr(old.c_str()); }

static int shutdown_ecb = 0;
static int return_code = 0;
static pid_t child;

static int killProcess(pid_t pid, int signal) {
    int status;
    kill(-pid, signal);
    waitpid(pid, &status, 0);
    __asm(" POST (%0),0   post the shutdown ECB\n"
         :
         : "a"(&shutdown_ecb)
         : "r0", "r1" );
    return 0;
}

// console listener to shutdown the server with P (stop) console command
extern "C" void* consoleThread(void *context) {
    int concmd = 0;
    char modstr[128];
    while (true) {
        if (__console(NULL, modstr, &concmd)) {
            if (errno == EINTR) {
                std::cerr << "__console() interrupted: " << strerror(errno) << "\n";
            } else {
                std::cerr << "__console() error: " << strerror(errno) << "\n";
            }
            break;
        }
        if (concmd == _CC_stop) {
            killProcess(child, SIGTERM);
        }
    }
    return 0;
}

extern "C" void handle_sigchld(int sig) {
    int status;
    // reap zombies
    while (waitpid((pid_t)(-1), &status, WNOHANG) > 0) {}
    if (WIFEXITED(status)) {
        return_code = WEXITSTATUS(status);
    }
    killProcess(child, SIGTERM);
    __asm(" POST (%0),0   post the shutdown ECB\n"
          :
          : "a"(&shutdown_ecb)
          : "r0", "r1");
}

static void ltrim(std::string &s, const std::string &delims = " \t\n") {
    s.erase(0, s.find_first_not_of(delims));
}

static int run(int argc, const char *argv[]) {
    // open the SYSOUT data set
    File sysout("//DD:SYSOUT", "w", false);
    if (!sysout.is_open()) {
        // SYSOUT DD is missing so use dynamic allocation
        alloc("ALLOC FI(SYSOUT) SYSOUT(X) MSG(2)");
        sysout.open("//DD:SYSOUT", "w");
    }

    // Open STDIN for reading
    File dd_stdin("//DD:STDIN", "r");
    // Open STDOUT and STDERR for writing. If either DD is missing fall back to SYSOUT.
    File dd_stdout("//DD:STDOUT", "w", false);
    File *stdout_ptr = dd_stdout.is_open() ? &dd_stdout : &sysout;
    File dd_stderr("//DD:STDERR", "w", false);
    File *stderr_ptr = dd_stderr.is_open() ? &dd_stderr : &sysout;

    // create the pipes
    Pipe shell_stdin;
    Pipe shell_stdout;
    Pipe shell_stderr;

    // Set up file descriptor map for child process
    int fd_map[3];
    fd_map[0] = duplicate(shell_stdin.read_handle());   // child stdin  is read end of pipe
    fd_map[1] = duplicate(shell_stdout.write_handle()); // child stdout is write end of pipe
    fd_map[2] = duplicate(shell_stderr.write_handle()); // child stderr is write end of pipe

    // close the unused ends of the pipes
    shell_stdin.close(Pipe::READ);
    shell_stdout.close(Pipe::WRITE);
    shell_stderr.close(Pipe::WRITE);

    // environment variables. _BPX_SHAREAS=MUST is essential for local spawn
    std::vector<const char *> envp;
    envp.push_back(dupstr("LIBPATH=/lib:/usr/lib"));
    envp.push_back(dupstr("PATH=/bin:/usr/bin"));
    envp.push_back(dupstr("_BPXK_AUTOCVT=ON"));
    envp.push_back(dupstr("_BPXK_JOBLOG=STDERR"));
    envp.push_back(dupstr("_BPX_SHAREAS=MUST"));
    envp.push_back(dupstr("_BPX_SPAWN_SCRIPT=YES"));
    envp.push_back(dupstr("_EDC_ADD_ERRNO2=1"));
    auto *userid = __getlogin1();
    if (userid == 0) throwError("__getlogin1() failed");
    struct passwd *p;
    if ((p = getpwnam(userid)) == NULL) throwError("getpwnam() failed");
    std::string home("HOME="); home += p->pw_dir;
    std::string pwd("PWD="); pwd += p->pw_dir;
    envp.push_back(dupstr(home));
    envp.push_back(dupstr(pwd));
    // if the STDENV DD exists read the environment variables from the file and
    // add them to the environment variable vector
    {
        std::ifstream filein("//DD:STDENV");
        if (filein) {
            for (std::string line; std::getline(filein, line);) {
                ltrim(line);
                // ignore comments and blank lines
                if (line.empty() || line[0] == '#') continue;
                envp.push_back(dupstr(line));
            }
        }
    }
    envp.push_back(0);  // end of argument list

    umask(022); // set the default umask

    // register the signal handler
    struct sigaction sa;
    sa.sa_handler = &handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror(0);
        exit(1);
    }

    // spawn the console thread listener to wait for shutdown commands
    pthread_t thread;
    int r = pthread_create(&thread, NULL, consoleThread, 0);
    if (r == -1) throwError("Error creating console listener thread");
    pthread_detach(thread);

    if (argc > 1) {  // program name specified as a program argument
        // spawn the program
        __inheritance inheritence = { 0 };
        inheritence.flags  = SPAWN_SETGROUP | SPAWN_SETSIGDEF | SPAWN_SETSIGMASK;
        inheritence.pgroup = SPAWN_NEWPGROUP;
        child = __spawnp2(argv[1], 3, fd_map, &inheritence, &argv[1], &envp[0]);
        if (child == -1) throwError("__spawnp2() failed running program " + std::string(argv[1]));
    } else { // spawn a shell process
        // prepend a '-' to the program name to run a login shell (this may not
        // work from all shells such as bash).
        std::string pgmname("-");
        pgmname += p->pw_shell;
        // program arguments
        const char *argv[] = { pgmname.c_str(), 0 };
        // spawn the shell process
        __inheritance inheritence = { 0 };
        child = __spawnp2(p->pw_shell, 3, fd_map, &inheritence, argv, &envp[0]);
        if (child == -1) throwError("__spawnp2() failed running program " + pgmname);
    }
    // free environment variables C strings on the heap
    std::for_each(envp.begin(), envp.end(), DeletePtr());

    int maxfd = std::max(shell_stdin.write_handle(), std::max(shell_stdout.read_handle(), shell_stderr.read_handle()));
    int rc = 0;
    int waitpid_options = WNOHANG | WUNTRACED | WCONTINUED;
    pid_t w;
    while (true) {
        int status = 0;
        fd_set writefds;
        FD_ZERO(&writefds);
        if (shell_stdin.is_write_open()) {
            FD_SET(shell_stdin.write_handle(), &writefds);
        }
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(shell_stdout.read_handle(), &readfds);
        FD_SET(shell_stderr.read_handle(), &readfds);
        timeval timeout = { 0 };
        timeout.tv_sec = 1;
        int rc = selectex(maxfd + 1, &readfds, &writefds, NULL, NULL, &shutdown_ecb);
        if (rc < 0) throwError("select() failed");
        if (rc == 0) break; // shutdown
        char buf[4096];
        if (shell_stdin.is_write_open() &&
            FD_ISSET(shell_stdin.write_handle(), &writefds)) {
            int numBytes = dd_stdin.read(buf, sizeof buf);
            if (numBytes > 0) {
                shell_stdin.write(buf, numBytes);
            } else { // shell script has been fully read
                shell_stdin.close(Pipe::WRITE);
                dd_stdin.close();
            }
        }
        if (FD_ISSET(shell_stderr.read_handle(), &readfds)) {
            int numBytes = shell_stderr.read(buf, sizeof buf);
            stderr_ptr->write(buf, numBytes);
        }
        if (FD_ISSET(shell_stdout.read_handle(), &readfds)) {
            int numBytes = shell_stdout.read(buf, sizeof buf);
            stdout_ptr->write(buf, numBytes);
        }

    }
    return return_code;
}

int main(int argc, const char *argv[]) {
    setenv("_EDC_ADD_ERRNO2", "1", 1); // add errno2 info to LE messages
    signal(SIGPIPE, SIG_IGN); // ignore broken pipe signals           
    int rc = 0;
    try {
        rc = run(argc, argv);
    } catch (std::exception &e) {
        rc = 12;
        std::cerr << e.what() << "\n";
    }
    return rc;
}

