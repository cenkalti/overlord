#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    char        *line;
    int         pid;
    FILE        *fp;
    pthread_t   thread;
} Command;

// Number of commands in input
int n = 0;

// List of commands in input
Command *commands = NULL;

// Set when SIGINT or SIGTERM is received
bool stop = false;

// Set when first SIGINT is received
bool shutdown_pending = false;

// Protects stop variable and pids of Commands
pthread_mutex_t mutex;

sigset_t block_mask;

// _run_command is similar to popen function except that it sets the PID of cmd.
// Returns 0 on success and -1 on failure and errno is set.
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
        setpgrp(); // Prevent sending SIGINT to childs when Ctrl-C is pressed
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

// We need to block signals during the execution of _run_command
// because our implementation is AS-Unsafe.
int run_command(Command *cmd) {
    // Block signals during _run_command()
    sigset_t previous;
    sigprocmask(SIG_BLOCK, &block_mask, &previous);
    int rc = _run_command(cmd);
    sigprocmask(SIG_SETMASK, &previous, NULL);
    return rc;
}

// send_signal sends signal to all running commands.
void send_signal(int signo) {
    for (int i = 0; i < n; ++i) {
        if (commands[i].pid) { // do not send to zero PID
            // fprintf(stderr, "overlord: sending signal: %s to PID: %d\n", strsignal(signo), pid);
            kill(commands[i].pid, signo);
        }
    }
}

// If signal is SIGTERM, SIGTERM will be sent to all child processes and
// overlord will exit after all children are terminated.
// If signal is SIGINT, the first signal does the same action as SIGTERM.
// If another SIGINT is received while waiting for termination of
// child processes, a SIGKILL will be sent to child processes.
void sig_handler(int signo) {
    // fprintf(stderr, "overlord: received signal: %s\n", strsignal(signo));
    pthread_mutex_lock(&mutex);
    switch (signo) {
    case SIGINT:
        if (shutdown_pending) {
            send_signal(SIGKILL);
        } else {
            send_signal(SIGTERM);
            shutdown_pending = true;
        }
        break;
    case SIGTERM:
        send_signal(SIGTERM);
        break;
    case SIGQUIT:
        send_signal(SIGKILL);
        break;
    case SIGABRT:
        _exit(EXIT_SUCCESS);
    }
    stop = true;
    pthread_mutex_unlock(&mutex);
}

void *watch_command(void *ptr) {
    Command *cmd = (Command*)ptr;
    while (1) {
        pthread_mutex_lock(&mutex);

        // Stop if shutdown is initiated by a signal
        if (stop) {
            cmd->pid = 0;
            pthread_mutex_unlock(&mutex);
            break;
        }

        if (run_command(cmd) == -1) {
            fprintf(stderr, "overlord: cannot run command: %s\n", strerror(errno));
            cmd->pid = 0;
            pthread_mutex_unlock(&mutex);
            sleep(1);
            continue; // Retry until stopped
        }

        pthread_mutex_unlock(&mutex);

        // Consume stream
        char *line = NULL;
        ssize_t linelen;
        size_t  linecap = 0;
        while ((linelen = getline(&line, &linecap, cmd->fp)) > 0)
            fwrite(line, linelen, 1, stdout); // fwrite is MT-Safe

        fclose(cmd->fp);

        // Wait for child process to exit
        int pstat;
        pid_t pid;
        do {
            pid = waitpid(cmd->pid, &pstat, 0);
        } while (pid == -1 && errno == EINTR);
    }

    pthread_exit(NULL);
}

char *trim_whitespace(char *str) {
    char *end;

    // Trim leading space
    while(isspace(*str)) str++;

    if(*str == 0)  // All spaces?
    return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace(*end)) end--;

    // Write new null terminator
    *(end+1) = 0;

    return str;
}

int main() {
    // Read lines from stdin
    char *line = NULL;
    ssize_t linelen;
    size_t  linecap = 0;
    while ((linelen = getline(&line, &linecap, stdin)) > 0) {
        // Ignore empty lines
        char *trimmed = trim_whitespace(line);
        if (*trimmed == '\0') continue;

        // Ignore comments
        if (*trimmed == '#') continue;

        commands = realloc(commands, sizeof(Command) * ++n);
        if (commands == NULL) {
            fprintf(stderr, "overlord: %s\n", strerror(errno));
            return 1;
        }

        commands[n-1].pid = 0;
        commands[n-1].line = malloc(strlen(trimmed)+1);
        if (commands[n-1].line == NULL) {
            fprintf(stderr, "overlord: %s\n", strerror(errno));
            return 2;
        }

        strcpy(commands[n-1].line, trimmed);
    }

    pthread_mutex_init(&mutex, NULL);

    // Register signal handlers
    sigemptyset(&block_mask);
    sigaddset(&block_mask, SIGINT);
    sigaddset(&block_mask, SIGTERM);
    sigaddset(&block_mask, SIGQUIT);
    sigaddset(&block_mask, SIGABRT);
    struct sigaction act;
    act.sa_handler = sig_handler;
    act.sa_mask = block_mask;
    act.sa_flags = 0;
    sigaction(SIGINT,  &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGABRT, &act, NULL);

    // Run commands
    for (int i = 0; i < n; ++i) {
        if (pthread_create(&commands[i].thread, NULL, watch_command, (void*)&commands[i]) != 0) {
            fprintf(stderr, "overlord: cannot create thread");
            pthread_mutex_lock(&mutex);
            send_signal(SIGKILL);
            stop = true;
            pthread_mutex_unlock(&mutex);
            return 3;
        }
    }

    // Wait all threads to finish then exit
    for (int i = 0; i < n; ++i)
        pthread_join(commands[i].thread, NULL);

    return 0;
}
