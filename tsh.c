/*
 * tsh - A tiny shell program with job control
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* Per-job data */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, FG, BG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

volatile sig_atomic_t ready; /* Is the newest child in its own process group? */

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);
void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);
void sigusr1_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int freejid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

void sectioning_pipe(char **argv, char **argv_sec, int *file_inpt, int *file_opt, int index, int pipe_count);
int total_num_pipes(char **argv);
struct job_t* checkingType(char *arg, struct job_t *total_job);
void getting_redirect(int fl_input, int fl_output, int i2, int pipe_counter, int *pipefds);
/*
 * main - The shell's main routine
 */
int main(int argc, char **argv) {
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(STDOUT_FILENO, STDERR_FILENO);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != -1) {
        switch (c) {
            case 'h':             /* print help message */
                usage();
                break;
            case 'v':             /* emit additional diagnostic info */
                verbose = 1;
                break;
            case 'p':             /* don't print a prompt */
                emit_prompt = 0;  /* handy for automatic testing */
                break;
            default:
                usage();
        }
    }

    /* Install the signal handlers */

    Signal(SIGUSR1, sigusr1_handler); /* Child is ready */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
*/
void eval(char *cmdline) {
    int command_bg, argc;
    char *argv[MAXARGS];
    char buf[MAXLINE];

    pid_t current_pid;
    sigset_t initial_maks;
    sigset_t old_mask;

    strcpy(buf, cmdline);
    argc = parseline(buf, argv);

    if (argc > 0) {
        char lastArgFirstChar = *argv[argc - 1];
        if (lastArgFirstChar == '&') {
            command_bg = 1;
        } else {
            command_bg = 0;
        }
    } else {
        command_bg = 0;
    }

    if (argv[0] == NULL)
        return;


    int isBuiltIn = builtin_cmd(argv);
    if (isBuiltIn == 0) {
        int pipe_counter = total_num_pipes(argv);
        int pipefds[2 * pipe_counter];
        pid_t pids[pipe_counter + 1];

        int i = 0;
        while (i < pipe_counter) {
            int pipe_index = i * 2;
            if (pipe(pipefds + pipe_index) < 0) {
                unix_error("Issue caused by pipe error");
            }
            i = i + 1;
        }

        int i2 = 0;
        while (i2 <= pipe_counter) {
            char *part_argv[MAXARGS];

            int fl_input, fl_output;
            sectioning_pipe(argv, part_argv, &fl_input, &fl_output, i2, pipe_counter);

            sigprocmask(SIG_BLOCK, &initial_maks, &old_mask);


            if ((current_pid = fork()) == 0) {
                setpgid(0, 0);
                getting_redirect(fl_input, fl_output, i2, pipe_counter, pipefds);

                int term_ind = 0;
                while (term_ind < 2 * pipe_counter) {
                    close(pipefds[term_ind]);
                    term_ind = term_ind + 1;
                }

                sigprocmask(SIG_SETMASK, &old_mask, NULL);
                if (command_bg) {
                    part_argv[argc-1] = NULL;
                }

                int condition_execution;
                condition_execution = execvp(part_argv[0], part_argv);
                if (condition_execution < 0) {
                    printf("%s: No expected command found\n", part_argv[0]);
                    exit(0);
                }

            } else if (current_pid < 0) {
                unix_error("Encountered a Fork error");
            } else {
                if (i2 == 0) {
                    int jobType = command_bg ? BG : FG;
                    addjob(jobs, current_pid, jobType, cmdline);
                }
                pids[i2] = current_pid;
            }

            sigprocmask(SIG_SETMASK, &old_mask, NULL);
            i2 = i2 + 1;
        }


        int position_index = 0;
        while (position_index < 2 * pipe_counter) {
            close(pipefds[position_index]);
            position_index = position_index + 1;
        }

        if (!command_bg) {
            int index_jb = 0;
            while (index_jb <= pipe_counter) {
                waitfg(pids[index_jb]);
                index_jb = index_jb + 1;
            }
        } else {
            int jobId = pid2jid(pids[0]);
            printf("[%d] (%d) %s", jobId, pids[0], cmdline);
        }
    }
}



void getting_redirect(int fl_input, int fl_output, int i2, int pipe_counter, int *pipefds) {
    if (fl_input != -1) {
        dup2(fl_input, STDIN_FILENO);
        close(fl_input);
    } else if (i2 > 0) {
        int x = (i2 - 1);
        dup2(pipefds[(x) * 2], STDIN_FILENO);
    }

    if (fl_output != -1) {
        dup2(fl_output, STDOUT_FILENO);
        close(fl_output);
    } else if (i2 < pipe_counter) {
        int y = i2 * 2;
        dup2(pipefds[y + 1], STDOUT_FILENO);
    }
}


void sectioning_pipe(char **argv, char **argv_sec, int *file_inpt, int *file_opt, int index, int pipe_count) {
    int checking = pipe_count;
    int position_idx = 0;

    int placement_arg = 0;
    int condition_1 = 1;

    *file_inpt = -1;
    *file_opt = -1;

    while (condition_1 && *argv && checking >= 0) {
        if (strcmp(*argv, "|") == 0) {
            if (position_idx != index) {
                position_idx = position_idx + 1;
                placement_arg = 0;
            } else {
                condition_1 = 0;
            }

        } else if (position_idx == index) {
            if (strcmp(*argv, "<") == 0) {
                checking = checking + 1;
                argv = argv + 1;
                FILE *final_input = fopen(*argv, "r");
                if (!final_input) {
                    perror("given fopen error");
                    exit(1);
                }
                *file_inpt = fileno(final_input);
            } else {
                if (strcmp(*argv, ">") == 0) {
                    checking = checking + 1;
                    argv = argv + 1;
                    FILE *final_output = fopen(*argv, "w");
                    if (!final_output) {
                        perror("given fopen error");
                        exit(1);
                    }
                    *file_opt = fileno(final_output);
                } else {

                    argv_sec[placement_arg] = *argv;
                    if (placement_arg >= 0){
                        placement_arg = placement_arg + 1;
                    }
                }
            }
        }
            argv = argv + 1;
    }
    argv_sec[placement_arg] = NULL;
}


int total_num_pipes(char **argv) {
    int tracker = 0;
    int con = 5;
    while (con == 5) {
        if (*argv[0] == '|' && argv[0][1] == '\0')
        {
            tracker = tracker + 1;
        }
        argv = argv + 1;
        if (!(*argv)){
            con = con + 1;
        }
    }
    return tracker;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return number of arguments parsed.
 */
int parseline(const char *cmdline, char **argv) {
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to space or quote delimiters */
    int argc;                   /* number of args */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
        delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;

    return argc;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */
int builtin_cmd(char **argv) {
    int condition_one, condition_two;
    int final = 0;
    if (argv[0] == NULL) {
        return 1;
    }
    if (strcmp(argv[0], "quit") == 0) {
        exit(0);
    } else if (strcmp(argv[0], "jobs") == 0) {
        listjobs(jobs);
        final = 1;
    } else{
        condition_one = strcmp(argv[0], "bg");
        condition_two = strcmp(argv[0], "fg");
        if (condition_one == 0 || condition_two == 0){
            do_bgfg(argv);
            final = 1;
        }
    }
    return final;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */

void do_bgfg(char **argv) {
    struct job_t *placement = NULL;
    if (!argv[1]) {
        printf("%s PID is needed for such %%jobid argument\n", argv[0]);
        return;
    }

    placement = checkingType(argv[1], jobs);
    if (!placement) {

        if (argv[1][0] == '%') {
            printf("%s:Job was not found\n", argv[1]);
        } else if (isdigit(argv[1][0])) {
            printf("%s: Given process was not encountered\n", argv[1]);
        } else {
            printf("%s: The given argument needs to be a PID or a given %%jobid\n", argv[1]);
        }
        return;
    }

    kill(-(placement->pid), SIGCONT);

    int condition_bg = !strcmp(argv[0], "bg");
    int condition_fg = !strcmp(argv[0], "fg");

    if (condition_bg || condition_fg) {
        if (condition_bg) {
            placement->state = BG;
            printf("[%d] (%d) %s", placement->jid, placement->pid, placement->cmdline);
        } else {
            placement->state = FG;
            waitfg(placement->pid);
        }
    }
}


struct job_t* checkingType(char *arg, struct job_t *total_job) {
    struct job_t *placement = NULL;
    if (arg[0] == '%') {
        int jid = atoi(&arg[1]);
        placement = getjobjid(total_job, jid);
    } else if (isdigit(arg[0])) {
        pid_t pid = atoi(arg);
        placement = getjobpid(total_job, pid);
    }
    return placement;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */


void waitfg(pid_t pid) {

    int condition = 1;
    while (condition) {
        struct job_t *placement = getjobpid(jobs, pid);

        if (placement == NULL){
            condition = 0;
        } else if (!(placement->state == FG)) {
            condition = 0;
        } else {
            sigset_t initial_mask, old_mask;
            sigemptyset(&initial_mask);
            sigaddset(&initial_mask, SIGCHLD);
            sigprocmask(SIG_BLOCK, &initial_mask, &old_mask);

            sigsuspend(&old_mask);
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
        }
    }
}


/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */


void sigchld_handler(int sig) {
    sigset_t total_msk;
    sigset_t old_msks;
    sigfillset(&total_msk);

    int stamped_error = errno;
    pid_t tracking_pid;

    int state;
    int x = -1;

    do {
        tracking_pid = waitpid(x, &state, WNOHANG | WUNTRACED);

        if (tracking_pid > 0 ) {
            sigprocmask(SIG_BLOCK, &total_msk, &old_msks);
            struct job_t *placement = getjobpid(jobs, tracking_pid);

            if (placement == NULL) {

            } else {
                if (!WIFSTOPPED(state)) {
                    int wasExited = WIFEXITED(state);
                    if (WIFSIGNALED(state)) {
                        printf("Job [%d] (%d) was terminated due to the following  signal %d\n", pid2jid(tracking_pid), tracking_pid, WTERMSIG(state));
                    } else if (wasExited) {}
                    deletejob(jobs, tracking_pid);
                } else {
                    placement->state = ST;
                    printf("Job [%d] (%d) was halted due to the following signal %d\n", pid2jid(tracking_pid), tracking_pid, WSTOPSIG(state));
                }
            }
            sigprocmask(SIG_SETMASK, &old_msks, NULL);
        }

    } while (!(tracking_pid <= 0));

    errno = stamped_error;
}


/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
    pid_t checking_pid = fgpid(jobs);
    if (checking_pid != 0)
        kill(-checking_pid, sig);
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) {
    pid_t checking_pid = fgpid(jobs);
    if (checking_pid != 0) {
        int result = kill(-checking_pid, SIGTSTP);
        if (!(result >= 0)) {
            unix_error("sigtstp_handler kill error");
        }
    }
}

/*
 * sigusr1_handler - child is ready
 */
void sigusr1_handler(int sig) {
    ready = 1;
}


/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* freejid - Returns smallest free job ID */
int freejid(struct job_t *jobs) {
    int i;
    int taken[MAXJOBS + 1] = {0};
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid != 0)
            taken[jobs[i].jid] = 1;
    for (i = 1; i <= MAXJOBS; i++)
        if (!taken[i])
            return i;
    return 0;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
    int i;

    if (pid < 1)
        return 0;
    int free = freejid(jobs);
    if (!free) {
        printf("Tried to create too many jobs\n");
        return 0;
    }
    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = free;
            strcpy(jobs[i].cmdline, cmdline);
            if(verbose){
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    return 0; /*suppress compiler warning*/
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
                case BG:
                    printf("Running ");
                    break;
                case FG:
                    printf("Foreground ");
                    break;
                case ST:
                    printf("Stopped ");
                    break;
                default:
                    printf("listjobs: Internal error: job[%d].state=%d ",
                           i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message and terminate
 */
void usage(void) {
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}