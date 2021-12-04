#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  proc_t *proc;
  job_t *job;
  (void)pid;

  // loop over all jobs checking status for every process in an unfinished job
  for (int j = 0; j < njobmax; j++) {
    job = &jobs[j];
    if (job->pgid != 0) {
      // loop over all processes in a job
      for (int k = 0; k < job->nproc; k++) {
        proc = &(job->proc[k]);
        if (proc->state != FINISHED) {
          if ((pid = waitpid(proc->pid, &status, WNOHANG | WUNTRACED))) {

            if (pid > 0) {
              // if terminated save status as is to be later inspected
              if (WIFEXITED(status)) {
                //  exited normally
                proc->state = FINISHED;
                proc->exitcode = status;
              } else if (WIFSIGNALED(status)) {
                // procpid was terminated by signal
                proc->state = FINISHED;
                proc->exitcode = status; 
              } else if (WIFSTOPPED(status)) {
                proc->state = STOPPED;
              }
            }
          }
        }
      }

        // job state changes if all processes changed state to the same state.
      int st = job->proc[0].state;
      if (st != job->state) {
        // job state change is possible
        for (int m = 1; m<job->nproc; m++) {
          if (job->proc[m].state != st) {
            // state remains unchanged
            st = job->state;
            break;
          }
        }
      }
      job->state = st;
    }
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  if (state == FINISHED) {
    if (statusp != NULL)
      *statusp = exitcode(job);
    // remove finished job including processes
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT

  // if j=-1 take last job
  j = j < 0 ? njobmax-1 : j ;
  job_t *job = &jobs[j];

  if (!bg) {
    printf("continue '%s'\n", job->command);
    // send to fg, give terminal before SIGCONT, set terminal attributes
    setfgpgrp(job->pgid);
    Tcsetattr(tty_fd, TCSADRAIN, &jobs[j].tmodes);
    movejob(j, 0);
  }
  // run job
  job->state = RUNNING;
  Kill(-job->pgid, SIGCONT);

  // if fg monitor job
  if (!bg) {
    monitorjob(mask);
  }

#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  job_t *job = &jobs[j];
  Kill(-job->pgid, SIGTERM);
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    // int status;  
    if (jobs[j].state == which || which == ALL) {
      int state;
      int *statusp = Malloc(sizeof(int));

      // save job command
      const char *cmd = strdup(jobs[j].command);

      // jobstate removes FINISHED
      if ((state = jobstate(j,statusp)) == FINISHED) {
        if (WIFEXITED(*statusp)) {
          printf("[%d] %s '%s', status=%d\n", j, "exited", cmd, WEXITSTATUS(*statusp));   
        } else if (WIFSIGNALED(*statusp)) {
          printf("[%d] %s '%s' by signal %d\n", j, "killed", cmd, WTERMSIG(*statusp));    
        }
      } else if (state == STOPPED) {
        printf("[%d] %s '%s'\n", j, "suspended", cmd);    
      } else if (state == RUNNING) {
        printf("[%d] %s '%s'\n", j, "running", cmd);    
      }

      free(statusp);
      free((void *)cmd);
    }
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT

  // wait for a foreground job to finish or to be stopped, 
  // that is all job processes finish or all stop. 
  while ( (state=jobstate(FG, NULL)) == RUNNING ) {
    // wait for some process to change state from running, it can only happen after sigchld_handler is run
    Sigsuspend(mask);
  }
  // set shell to foreground
  setfgpgrp( getpgrp() );
  // put back terminal attributes
  Tcsetattr(tty_fd, TCSAFLUSH, &shell_tmodes);
  
  if (state == STOPPED) {
    // move job to background
    movejob(FG, allocjob());
  }

#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  // kill remaining jobs, wait for sigchld_handler to update the state of all of them to FINISHED

  // kill jobs 
  for (int j=0; j<njobmax; j++) {
    // job_t *job = &jobs[j];
    if (jobs[j].pgid != 0 && jobs[j].state != FINISHED)
      killjob(j);
  }

  // wait for a background jobs to finish, 
  while (1) {
    // all jobs finished?
    bool bl = true;
    for (int j=0; j<njobmax; j++) {
      if (jobs[j].pgid != 0 && jobs[j].state != FINISHED) {
        bl = false;
        break;
      } 
    }
    if (bl) {
      break;
    } else {
    // wait for some process to change state to FINISHED, it can only happen after sigchld_handler is run
      Sigsuspend(&mask);
    }
  }

#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
