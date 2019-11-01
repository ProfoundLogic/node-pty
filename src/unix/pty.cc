/**
 * Copyright (c) 2012-2015, Christopher Jeffrey (MIT License)
 * Copyright (c) 2017, Daniel Imms (MIT License)
 *
 * pty.cc:
 *   This file is responsible for starting processes
 *   with pseudo-terminal file descriptors.
 *
 * See:
 *   man pty
 *   man tty_ioctl
 *   man termios
 *   man forkpty
 *
 */

/**
 * Includes
 */

#include <napi.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>

/* forkpty */
/* http://www.gnu.org/software/gnulib/manual/html_node/forkpty.html */
#if defined(__GLIBC__) || defined(__CYGWIN__)
#include <pty.h>
#elif defined(__APPLE__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include <../include/util.h>

#elif defined(__FreeBSD__)
#include <libutil.h>
#elif defined(__sun)
#include <stropts.h> /* for I_PUSH */
#else
#include <pty.h>
#endif

#include <termios.h> /* tcgetattr, tty_ioctl */

/* environ for execvpe */
/* node/src/node_child_process.cc */
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#else
extern char **environ;
#endif

/* for pty_getproc */
#if defined(__linux__)
#include <stdio.h>
#include <stdint.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <libproc.h>
#endif

/**
 * Classes
 * This class allows another thread to wait for the exit code and then run the "onexit" callback in JavaScript.
 * Its destructor is automatically called when the class is no longer needed.
 */
class MyWorker : public Napi::AsyncWorker {
  public:
    MyWorker(Napi::Function& callback, pid_t& pid) : Napi::AsyncWorker(callback), pid(pid) {}

    // Once the OnOk or OnError methods are complete, the instance is destructed. The instance deletes itself using the delete operator.
    ~MyWorker() {}

    // Code to be executed on the worker thread. (replaces pty_waitpid)
    // When this returns, the OnOk or OnError functions are called, running on the main thread.
    void Execute() {
      my_waitpid();

      if (WIFEXITED(stat_loc)) {
        exit_code = WEXITSTATUS(stat_loc); // errno?
      }

      if (WIFSIGNALED(stat_loc)) {
        signal_code = WTERMSIG(stat_loc);
      }
    }

    void my_waitpid() {
      int ret;
      errno = 0;

      if ((ret = waitpid(pid, &stat_loc, 0)) != pid) {
        if (ret == -1 && errno == EINTR) {
          return my_waitpid();
        }
        if (ret == -1 && errno == ECHILD) {
          // XXX node v0.8.x seems to have this problem.
          // waitpid is already handled elsewhere.
          ;
        }
        else {
          assert(false);
        }
      }
    }
    
    // After the Execute method is done, the parent class's OnOk is called as part of the event loop,
    // causing JavaScript to invoke the callback. (replaces pty_after_waitpid).
    // GetResult provides arguments to the callback, 
    virtual std::vector<napi_value> GetResult(Napi::Env env) {
      Napi::HandleScope scope(env);
      std::vector<napi_value> argv;
      std::vector<napi_value>::iterator iter;
      iter = argv.begin();
      iter = argv.insert(iter, Napi::Number::New(env, exit_code));
      iter = argv.insert(iter, Napi::Number::New(env, signal_code));
      return argv;
    }

  private:
    pid_t pid;
    int stat_loc;
    int exit_code;
    int signal_code;
};


/**
 * Methods
 */

Napi::Value PtyFork(const Napi::CallbackInfo& info);
Napi::Value PtyOpen(const Napi::CallbackInfo& info);
Napi::Value PtyResize(const Napi::CallbackInfo& info);
Napi::Value PtyGetProc(const Napi::CallbackInfo& info);

#if defined(TIOCSIG) || defined(TIOCSIGNAL)
#define DEFINE_PTY_KILL
Napi::Value PtyKill(const Napi::CallbackInfo& info);
#else
#warning "The function PtyKill will be unavailable because the ioctls TIOCSIG and TIOCSIGNAL don't exist"
#endif

/**
 * Functions
 */

static int pty_execvpe(const char *, char **, char **);

static int pty_nonblock(int);

static char * pty_getproc(int, char *);

static int pty_openpty(int *, int *, char *,
            const struct termios *,
            const struct winsize *);

static pid_t pty_forkpty(int *, char *,
            const struct termios *,
            const struct winsize *);

Napi::Value PtyFork(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() != 10 ||
      !info[0].IsString() ||
      !info[1].IsArray() ||
      !info[2].IsArray() ||
      !info[3].IsString() ||
      !info[4].IsNumber() ||
      !info[5].IsNumber() ||
      !info[6].IsNumber() ||
      !info[7].IsNumber() ||
      !info[8].IsBoolean() ||
      !info[9].IsFunction()) {

    Napi::Error::New(env, "Usage: pty.fork(file, args, env, cwd, cols, rows, uid, gid, utf8, onexit)").ThrowAsJavaScriptException();
    return env.Null();
  }

  // Make sure the process still listens to SIGINT
  signal(SIGINT, SIG_DFL);

  // file
  Napi::String file(env, info[0].ToString());

  // args
  int i = 0;
  Napi::Array argv_ = info[1].As<Napi::Array>();
  int argc = argv_.Length();
  int argl = argc + 1 + 1;
  char **argv = new char*[argl];
  argv[0] = strdup( file.Utf8Value().c_str() );
  argv[argl-1] = NULL;
  for (; i < argc; i++) {
    Napi::String arg(env, argv_.Get(Napi::Number::New(env, i)).ToString());
    argv[i + 1] = strdup(arg.Utf8Value().c_str());
  }

  // env - argument for environ, for execvpe
  i = 0;
  Napi::Array env_ = info[2].As<Napi::Array>();
  int envc = env_.Length();
  char **envarg = new char*[envc+1];
  envarg[envc] = NULL;
  for (; i < envc; i++) {
    Napi::String pair(env, env_.Get(Napi::Number::New(env, i)).ToString());
    envarg[i] = strdup(pair.Utf8Value().c_str());
  }

  // cwd
  Napi::String cwd_(env, info[3].ToString());
  char* cwd = strdup( cwd_.Utf8Value().c_str());

  // size
  struct winsize winp;
  winp.ws_col = info[4].As<Napi::Number>().Int64Value();
  winp.ws_row = info[5].As<Napi::Number>().Int64Value();
  winp.ws_xpixel = 0;
  winp.ws_ypixel = 0;

  // termios
  struct termios t = termios();
  struct termios *term = &t;
  term->c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT;
  if (info[8].ToBoolean().Value()) {
#if defined(IUTF8)
    term->c_iflag |= IUTF8;
#endif
  }
  term->c_oflag = OPOST | ONLCR;
  term->c_cflag = CREAD | CS8 | HUPCL;
  term->c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;

  term->c_cc[VEOF] = 4;
  term->c_cc[VEOL] = -1;
  term->c_cc[VEOL2] = -1;
  term->c_cc[VERASE] = 0x7f;
  term->c_cc[VWERASE] = 23;
  term->c_cc[VKILL] = 21;
  term->c_cc[VREPRINT] = 18;
  term->c_cc[VINTR] = 3;
  term->c_cc[VQUIT] = 0x1c;
  term->c_cc[VSUSP] = 26;
  term->c_cc[VSTART] = 17;
  term->c_cc[VSTOP] = 19;
  term->c_cc[VLNEXT] = 22;
  term->c_cc[VDISCARD] = 15;
  term->c_cc[VMIN] = 1;
  term->c_cc[VTIME] = 0;

  #if (__APPLE__)
  term->c_cc[VDSUSP] = 25;
  term->c_cc[VSTATUS] = 20;
  #endif

  cfsetispeed(term, B38400);
  cfsetospeed(term, B38400);

  // uid / gid
  int uid = info[6].As<Napi::Number>().Int64Value();
  int gid = info[7].As<Napi::Number>().Int64Value();

  // fork the pty
  int master = -1;
  pid_t pid = pty_forkpty(&master, nullptr, term, &winp);

  if (pid) {
    for (i = 0; i < argl; i++) free(argv[i]);
    delete[] argv;
    for (i = 0; i < envc; i++) free(envarg[i]);
    delete[] envarg;
    free(cwd);
  }

  switch (pid) {
    case -1:
      Napi::Error::New(env, "forkpty(3) failed.").ThrowAsJavaScriptException();
      return env.Null();
    case 0:
      if (strlen(cwd)) {
        if (chdir(cwd) == -1) {
          perror("chdir(2) failed.");
          _exit(1);
        }
      }

      if (uid != -1 && gid != -1) {
        if (setgid(gid) == -1) {
          perror("setgid(2) failed.");
          _exit(1);
        }
        if (setuid(uid) == -1) {
          perror("setuid(2) failed.");
          _exit(1);
        }
      }

      pty_execvpe(argv[0], argv, envarg);

      perror("execvp(3) failed.");
      _exit(1);
    default:
      if (pty_nonblock(master) == -1) {
        Napi::Error::New(env, "Could not set master fd to nonblocking.").ThrowAsJavaScriptException();
        return env.Null();
      }

      Napi::Object obj = Napi::Object::New(env);
      (obj).Set(Napi::String::New(env, "fd"), Napi::Number::New(env, master));
      (obj).Set(Napi::String::New(env, "pid"), Napi::Number::New(env, pid));
      (obj).Set(Napi::String::New(env, "pty"), Napi::String::New(env, ptsname(master)));

      // Create new MyWorker and queue it. Assign the callback from the arguments. The instance's memory is freed automatically.
      Napi::Function cb = info[9].As<Napi::Function>();
      MyWorker* backgroundWorker = new MyWorker(cb, pid);
      backgroundWorker->Queue();

      return obj;
  }

  return env.Undefined();
}

Napi::Value PtyOpen(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() != 2 ||
      !info[0].IsNumber() ||
      !info[1].IsNumber()) {
    Napi::Error::New(env, "Usage: pty.open(cols, rows)").ThrowAsJavaScriptException();
    return env.Null();
  }

  // size
  struct winsize winp;
  winp.ws_col = info[0].As<Napi::Number>().Int64Value();
  winp.ws_row = info[1].As<Napi::Number>().Int64Value();
  winp.ws_xpixel = 0;
  winp.ws_ypixel = 0;

  // pty
  int master, slave;
  int ret = pty_openpty(&master, &slave, nullptr, NULL, &winp);

  if (ret == -1) {
    Napi::Error::New(env, "openpty(3) failed.").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (pty_nonblock(master) == -1) {
    Napi::Error::New(env, "Could not set master fd to nonblocking.").ThrowAsJavaScriptException();
    return env.Null();
  }

  if (pty_nonblock(slave) == -1) {
    Napi::Error::New(env, "Could not set slave fd to nonblocking.").ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Object obj = Napi::Object::New(env);
  (obj).Set(Napi::String::New(env, "master"), Napi::Number::New(env, master));
  (obj).Set(Napi::String::New(env, "slave"), Napi::Number::New(env, slave));
  (obj).Set(Napi::String::New(env, "pty"), Napi::String::New(env, ptsname(master)));

  return obj;
}

#ifdef DEFINE_PTY_KILL
Napi::Value PtyKill(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() != 2 ||
    !info[0].IsNumber() ||
    !info[1].IsNumber()) {
    Napi::Error::New(env, "Usage: pty.kill(fd, signal)").ThrowAsJavaScriptException();
  }
  else {
    int fd = info[0].As<Napi::Number>().Int64Value();
    int signal = info[1].As<Napi::Number>().Int64Value();

  #if defined(TIOCSIG)
    if (ioctl(fd, TIOCSIG, signal) == -1) {
      Napi::Error::New(env, "ioctl(2) failed.").ThrowAsJavaScriptException();
    }
  #elif defined(TIOCSIGNAL)
    if (ioctl(fd, TIOCSIGNAL, signal) == -1) {
      Napi::Error::New(env, "ioctl(2) failed.").ThrowAsJavaScriptException();
    }
  #endif
  }
  return env.Null();
}
#endif

Napi::Value PtyResize(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() != 3 ||
      !info[0].IsNumber() ||
      !info[1].IsNumber() ||
      !info[2].IsNumber()) {
    Napi::Error::New(env, "Usage: pty.resize(fd, cols, rows)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int fd = info[0].As<Napi::Number>().Int64Value();

  struct winsize winp;
  winp.ws_col = info[1].As<Napi::Number>().Int64Value();
  winp.ws_row = info[2].As<Napi::Number>().Int64Value();
  winp.ws_xpixel = 0;
  winp.ws_ypixel = 0;

  if (ioctl(fd, TIOCSWINSZ, &winp) == -1) {
    Napi::Error::New(env, "ioctl(2) failed.").ThrowAsJavaScriptException();
    return env.Null();
  }

  return env.Undefined();
}

/**
 * Foreground Process Name
 */
Napi::Value PtyGetProc(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  Napi::HandleScope scope(env);

  if (info.Length() != 2 ||
      !info[0].IsNumber() ||
      !info[1].IsString()) {
    Napi::Error::New(env, "Usage: pty.process(fd, tty)").ThrowAsJavaScriptException();
    return env.Null();
  }

  int fd = info[0].As<Napi::Number>().Int64Value();

  Napi::String tty_(env, info[1].ToString());
  char* tty = strdup(tty_.Utf8Value().c_str());
  char *name = pty_getproc(fd, tty);
  free(tty);

  if (name == NULL) {
    return env.Undefined();
  }

  Napi::String name_ = Napi::String::New(env, name);
  free(name);
  return name_;
}

/**
 * execvpe
 */

// execvpe(3) is not portable.
// http://www.gnu.org/software/gnulib/manual/html_node/execvpe.html
static int pty_execvpe(const char *file, char **argv, char **envp) {
  char **old = environ;
  environ = envp;
  int ret = execvp(file, argv);
  environ = old;
  return ret;
}

/**
 * Nonblocking FD
 */

static int pty_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * pty_getproc
 * Taken from tmux.
 */

// Taken from: tmux (http://tmux.sourceforge.net/)
// Copyright (c) 2009 Nicholas Marriott <nicm@users.sourceforge.net>
// Copyright (c) 2009 Joshua Elsasser <josh@elsasser.org>
// Copyright (c) 2009 Todd Carson <toc@daybefore.net>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
// IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
// OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#if defined(__linux__)

static char *
pty_getproc(int fd, char *tty) {
  FILE *f;
  char *path, *buf;
  size_t len;
  int ch;
  pid_t pgrp;
  int r;

  if ((pgrp = tcgetpgrp(fd)) == -1) {
    return NULL;
  }

  r = asprintf(&path, "/proc/%lld/cmdline", (long long)pgrp);
  if (r == -1 || path == NULL) return NULL;

  if ((f = fopen(path, "r")) == NULL) {
    free(path);
    return NULL;
  }

  free(path);

  len = 0;
  buf = NULL;
  while ((ch = fgetc(f)) != EOF) {
    if (ch == '\0') break;
    buf = (char *)realloc(buf, len + 2);
    if (buf == NULL) return NULL;
    buf[len++] = ch;
  }

  if (buf != NULL) {
    buf[len] = '\0';
  }

  fclose(f);
  return buf;
}

#elif defined(__APPLE__)

static char *
pty_getproc(int fd, char *tty) {
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0 };
  size_t size;
  struct kinfo_proc kp;

  if ((mib[3] = tcgetpgrp(fd)) == -1) {
    return NULL;
  }

  size = sizeof kp;
  if (sysctl(mib, 4, &kp, &size, NULL, 0) == -1) {
    return NULL;
  }

  if (*kp.kp_proc.p_comm == '\0') {
    return NULL;
  }

  return strdup(kp.kp_proc.p_comm);
}

#else

static char *
pty_getproc(int fd, char *tty) {
  return NULL;
}

#endif

/**
 * openpty(3) / forkpty(3)
 */

static int
pty_openpty(int *amaster,
            int *aslave,
            char *name,
            const struct termios *termp,
            const struct winsize *winp) {
#if defined(__sun)
  char *slave_name;
  int slave;
  int master = open("/dev/ptmx", O_RDWR | O_NOCTTY);
  if (master == -1) return -1;
  if (amaster) *amaster = master;

  if (grantpt(master) == -1) goto err;
  if (unlockpt(master) == -1) goto err;

  slave_name = ptsname(master);
  if (slave_name == NULL) goto err;
  if (name) strcpy(name, slave_name);

  slave = open(slave_name, O_RDWR | O_NOCTTY);
  if (slave == -1) goto err;
  if (aslave) *aslave = slave;

  ioctl(slave, I_PUSH, "ptem");
  ioctl(slave, I_PUSH, "ldterm");
  ioctl(slave, I_PUSH, "ttcompat");

  if (termp) tcsetattr(slave, TCSAFLUSH, termp);
  if (winp) ioctl(slave, TIOCSWINSZ, winp);

  return 0;

err:
  close(master);
  return -1;
#else
  return openpty(amaster, aslave, name, (termios *)termp, (winsize *)winp);
#endif
}

static pid_t
pty_forkpty(int *amaster,
            char *name,
            const struct termios *termp,
            const struct winsize *winp) {
#if defined(__sun)
  int master, slave;

  int ret = pty_openpty(&master, &slave, name, termp, winp);
  if (ret == -1) return -1;
  if (amaster) *amaster = master;

  pid_t pid = fork();

  switch (pid) {
    case -1:
      close(master);
      close(slave);
      return -1;
    case 0:
      close(master);

      setsid();

#if defined(TIOCSCTTY)
      // glibc does this
      if (ioctl(slave, TIOCSCTTY, NULL) == -1) {
        _exit(1);
      }
#endif

      dup2(slave, 0);
      dup2(slave, 1);
      dup2(slave, 2);

      if (slave > 2) close(slave);

      return 0;
    default:
      close(slave);
      return pid;
  }

  return -1;
#else
  return forkpty(amaster, name, (termios *)termp, (winsize *)winp);
#endif
}

/**
 * Init
 */

Napi::Object init(Napi::Env env, Napi::Object exports) {
  Napi::HandleScope scope(env);
  exports.Set(Napi::String::New(env, "fork"), Napi::Function::New(env, PtyFork));
  exports.Set(Napi::String::New(env, "open"), Napi::Function::New(env, PtyOpen));
#ifdef DEFINE_PTY_KILL
  exports.Set(Napi::String::New(env, "kill"), Napi::Function::New(env, PtyKill));
#endif
  exports.Set(Napi::String::New(env, "resize"), Napi::Function::New(env, PtyResize));
  exports.Set(Napi::String::New(env, "process"), Napi::Function::New(env, PtyGetProc));
  return exports;
}

NODE_API_MODULE(pty, init)
