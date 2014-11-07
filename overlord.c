#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>

typedef struct {
    char *line;
    int  pid;
    FILE *fp;
} Command;

// Number of commands in input
int n = 0;

// List of commands in input
Command *commands = NULL;

// Buffer for getline()
char *line = NULL;
size_t  linecap = 0;
ssize_t linelen = 0;

// Set when first SIGINT is received
int shutdown_pending = 0;

pthread_t output_thread;
pthread_mutex_t mutex;
pthread_cond_t cond;

int _run_command(Command *cmd) {
    int filedes[2];
    if (pipe(filedes) == -1) return -1;

    pid_t pid = fork();
    if (pid == -1) {
        // Error
        close(filedes[0]);
        close(filedes[1]);
        return -1;
    } else if (pid == 0) {
        // Child
        dup2(filedes[1], STDOUT_FILENO);
        close(filedes[1]);
        close(filedes[0]);
        execl("/bin/sh", "sh", "-c", cmd->line, NULL);
        _exit(127); // unreached because of execl
    } else {
        // Parent
        FILE *fp = fdopen(filedes[0], "r");
        close(filedes[1]);
        cmd->pid = pid;
        cmd->fp = fp;
        return 0;
    }
}

// run_command is similar to popen except that it calls setpgrp before execl
// ands sets the pid of cmd.
// Returns 0 on success and -1 on failure and errno is set.
int run_command(Command *cmd) {
    // Block SIGINT and SIGTERM during _run_command()
    sigset_t block_mask, previous;
    sigemptyset(&block_mask);
    sigemptyset(&previous);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);

    sigprocmask(SIG_BLOCK, &block_mask, &previous);
    int rc = _run_command(cmd);
    sigprocmask(SIG_SETMASK, &previous, NULL);
    return rc;
}

// If signal is SIGTERM, SIGTERM will be sent to all child processes and
// overlord will exit after all children is terminated.
// If signal is SIGINT, the first signal does the same action as SIGTERM.
// If another SIGINT is received while waiting for termination of
// child processes, a SIGKILL will be sent to child processes.
void sig_handler(int signo) {
    printf("overlord: received signal: %s\n", strsignal(signo));
    switch (signo) {
    case SIGINT:
        if (shutdown_pending) {
            killpg(getpgrp(), SIGKILL);
            _exit(EXIT_SUCCESS);
        }
        shutdown_pending = 1;
    case SIGTERM:
        for (int i = 0; i < n; ++i) {
            printf("overlord: sending SIGTERM to PID: %d\n", commands[i].pid);
            kill(commands[i].pid, SIGTERM);
        }
    }
}

void *print_output() {
    fd_set read_fds;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    while (1) {
        pthread_mutex_lock(&mutex);
        if (n == 0) {
            pthread_mutex_unlock(&mutex);
            break;
        }

        FD_ZERO(&read_fds);
        for (int i = 0; i < n; ++i) {
            if (commands[i].fp == NULL) continue;
            FD_SET(fileno(commands[i].fp), &read_fds);
        }

        // Wait for IO
        if (select(FD_SETSIZE, &read_fds, NULL, NULL, &timeout) == -1) {
            pthread_mutex_unlock(&mutex);
            continue;
        }

        // Read lines from ready streams
        for (int i = 0; i < n; ++i) {
            FILE *f = commands[i].fp;
            if (f == NULL) continue;
            if (FD_ISSET(fileno(f), &read_fds)) {
                linelen = getline(&line, &linecap, f);
                if (linelen != -1) {
                    // Write output
                    fwrite(line, linelen, 1, stdout);
                    continue;
                }
            }
            if (feof(f) || ferror(f)) {
                fclose(f);
                commands[i].fp = NULL;
                pthread_cond_signal(&cond);
            }
        }

        pthread_mutex_unlock(&mutex);
    }

    pthread_exit(NULL);
}

int main() {
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    // Register signal handlers
    struct sigaction act;
    sigset_t block_mask;
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);
    act.sa_handler = sig_handler;
    act.sa_mask = block_mask;
    act.sa_flags = 0;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    // Read lines
    while ((linelen = getline(&line, &linecap, stdin)) != -1) {
        // TODO Trim whitespace
        // TODO Ignore empty lines

        // Ignore comments
        if (*line == '#') continue;

        commands = realloc(commands, sizeof(Command) * ++n);
        if (commands == NULL) {
            printf("overlord: %s\n", strerror(errno));
            return 1;
        }

        commands[n-1].line = malloc(linelen+1);
        if (commands[n-1].line == NULL) {
            printf("overlord: %s\n", strerror(errno));
            return 2;
        }

        strcpy(commands[n-1].line, line);
    }
    if (errno != 0) {
        printf("overlord: %s\n", strerror(errno));
        return 3;
    }

    // Run commands
    for (int i = 0; i < n; ++i) {
        if (run_command(&commands[i]) == -1) {
            printf("overlord: %s\n", strerror(errno));
            return 4;
        }
    }

    // Start output read thread
    if (pthread_create(&output_thread, NULL, print_output, NULL) != 0) {
        return 5;
    }

    pid_t pid;
    int stat;
    while (1) {
        pid = wait(&stat);
        if (pid == -1) {
            if (errno == ECHILD) break;
            if (errno == EINTR) continue;
            printf("overlord: %s\n", strerror(errno));
            return 7;
        }
        for (int i = 0; i < n; ++i) {
            if (commands[i].pid == pid) {
                pthread_mutex_lock(&mutex);

                // Wait until all output is consumed by output thread
                while (commands[i].fp) pthread_cond_wait(&cond, &mutex);

                if (shutdown_pending) {
                    // Remove command from the list
                    commands[i] = commands[--n];
                } else {
                    // Restart command
                    if (run_command(&commands[i]) == -1) {
                        printf("overlord: %s\n", strerror(errno));
                        return 6;
                    }
                }

                pthread_mutex_unlock(&mutex);
                break;
            }
        }
    }

    // Wait threads to finish then exit
    pthread_join(output_thread, NULL);
    return 0;
}
