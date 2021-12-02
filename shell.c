#include <signal.h>
#include <unistd.h>
#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT
    // // handle redirections
    // // Assumes valid command/pipe.
    // if (token[i] == T_INPUT && i+1 < ntokens && token[i+1] != NULL) {
    //   int fd = Open(token[i+1], O_RDONLY, 0);
    //   *inputp = fd;
    //   token[i] = mode; // for what is mode defined?
    //   token[i+1] = NULL;
    // if (token[i] == T_OUTPUT && i+1 < ntokens && token[i+1] != NULL) {
    //   int fd = Open(token[i+1], O_WRONLY | O_CREAT, S_IWUSR);
    //   *outputp = fd;
    //   token[i] = NULL;
    //   token[i+1] = NULL;
    
    // Initialize n=ntokens and not 0 ;/
    if (i == 0)
      n = ntokens;

    // go backwards
    int j = ntokens - i -1;
    // Consume tokens related to redirection UP TO first appearence of pipe | operator.
    // AND! starting from the end.
    if (token[j] == T_PIPE)
      break;

    // handle redirections
    // this accepts command "< file cat" what I call a feature
    if (token[j] == T_INPUT && j+1 < ntokens && token[j+1] != NULL) {
      int fd = Open(token[j+1], O_RDONLY, 0);
      *inputp = fd;
      token[j] = mode; // for what is mode defined?
      token[j+1] = NULL;
      if (n == j+2)
        n = j;
    }
    if (token[j] == T_OUTPUT && j+1 < ntokens && token[j+1] != NULL) {
      int fd = Open(token[j+1], O_WRONLY | O_CREAT, S_IWUSR);
      *outputp = fd;
      token[j] = NULL;
      token[j+1] = NULL;
      if (n == j+2)
        n = j;
    }
    // leaks opened file descriptors if multiple redirects

#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  int pid;
  if ((pid = Fork()) == 0) {
    // in child
    Setpgid(0, 0);
    if (!bg)
      setfgpgrp(getpgrp());

    // set stdin/stdout
    if (input >= 0)
      Dup2(input, 0);
    if (output >= 0)
      Dup2(output, 1);
    MaybeClose(&input);
    MaybeClose(&output);

    // unmask everything and unignore signals
    sigset_t set;
    sigemptyset(&set);
    Sigprocmask(SIG_SETMASK, &set, NULL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);
    
    // execve, fg builtin command above
    external_command(token);

  } else {
    // in parent
    
    // ignore EACCES error - children already performed execve 
    setpgid(pid, pid);

    MaybeClose(&input);
    MaybeClose(&output);

    // char *cmdtext[] = {"cmd"};
    int j = addjob(pid, bg);
    // dprintf(STDERR_FILENO, "%s %s\n", token[0], token[1]);
    addproc(j, pid, token);
    if (!bg) {
      setfgpgrp(pid);
      monitorjob(&mask);
    } else {
      setfgpgrp(getpgrp());
      dprintf(STDIN_FILENO, "[%d] running '%s'\n", j, jobcmd(j));
      // write(STDIN_FILENO, "running sth\n", 12);
    }

  }

#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT

  if (pid == 0) {
    // child
    setpgid(0, pgid);
    if (!bg)
      setfgpgrp( getpgrp() );
    // set stdin/stdout
    if (input >= 0)
      Dup2(input, 0);
    if (output >= 0)
      Dup2(output, 1);
    MaybeClose(&input);
    MaybeClose(&output);

    // unmask everything and unignore signals
    sigset_t set;
    sigemptyset(&set);
    Sigprocmask(SIG_SETMASK, &set, NULL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGTTIN, SIG_DFL);
    Signal(SIGTTOU, SIG_DFL);

    if (builtin_command(token) < 0) {
      external_command(token);
    }

  } else {
    // ignore EACCES - children already performed execve
    setpgid(pid, pgid);
    if (!bg) {
      setfgpgrp(pgid ? pgid : pid);
    } else {
      setfgpgrp(getpgrp());
    }
    MaybeClose(&input);
    MaybeClose(&output);
    // parent
  }



#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  // (void)input;
  // (void)job;
  // (void)pid;
  // (void)pgid;
  // (void)do_stage;
  int next_output;

  // start with some process
  int i = ntokens-1;
  for (; i>=0 && token[i] != T_PIPE;i--) {;}
  pgid = do_stage(0, &mask, next_input, -1, &token[i+1], ntokens-i-1, bg);
  next_output = output;
  job = addjob(pgid, bg);
  token[i] = NULL;
  addproc(job, pgid, &token[i+1]);

  // continue with the rest
  while (1) {
    int j = i;
    for (; i>=0 && token[i] != T_PIPE;i--) {;}
    
    if (token[i] == T_PIPE) {
      mkpipe(&input, &output);
      pid = do_stage(pgid, &mask, input, next_output, &token[i+1], j-i-1, bg);
      next_output = output;
      addproc(job, pid, &token[i+1]);
      token[i] = NULL;
    } else { // i < 0
      pid = do_stage(pgid, &mask, -1, next_output, token, j, bg);
      addproc(job, pid, token);
      break;
    }
  }

  if (!bg) {
    setfgpgrp(pgid);
    monitorjob(&mask);
  } else {
    // przezorny zawsze ubezpieczony
    setfgpgrp(getpgrp());
  }

  // int i = 0;
  // while (i < ntokens) {
  //   // create process command
  //   token_t *newtoken = Malloc(sizeof(token_t));
  //   int j=0;
  //   while (i < ntokens && token[i] != T_PIPE) {

  //     // iterate up to next T_PIPE, compact a command into newtoken
  //     for (int i = 0; token[i] != T_PIPE && i < ntokens; i++) {
  //       if (token[i] != NULL) {
  //         newtoken[j] = token[i];
  //         Realloc(newtoken, sizeof(token_t) * (j+1));
  //         j = j+1;
  //       }
  //     }
  //     newtoken[j] = NULL;
  //   }
  // }
  

#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
