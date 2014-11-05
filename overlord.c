#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
    char *line;
    int  pid;
    FILE *fd;
} Command;

int     n           = 0;
Command *commands   = NULL;
char    *line       = NULL;
size_t  linecap     = 0;
ssize_t linelen     = 0;

int run_command(Command *cmd) {
    FILE *fd;
    int filedes[2], pid;

    if (pipe(filedes) == -1) return -1;

    switch (pid = fork()) {
    case -1:
        return -1;
    case 0: // Child
        dup2(filedes[1], STDOUT_FILENO);
        close(filedes[1]);
        close(filedes[0]);
        execl("/bin/sh", "sh", "-c", cmd->line, NULL);
        _exit(127);
    }

    // Parent
    fd = fdopen(filedes[0], "r");
    close(filedes[1]);
    cmd->pid = pid;
    cmd->fd = fd;
    return 0;
}

int main() {
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

    // Read output of commands
    fd_set set;
    while (n > 0) {
        FD_ZERO (&set);
        for (int i = 0; i < n; ++i) {
            FD_SET (fileno(commands[i].fd), &set);
        }

        // Wait for IO
        int rc = select(FD_SETSIZE, &set, NULL, NULL, NULL);
        if (rc == -1) {
            printf("overlord: %s\n", strerror(errno));
            return 5;
        }

        for (int i = 0; i < n && FD_ISSET (fileno(commands[i].fd), &set); ++i) {
            linelen = getline(&line, &linecap, commands[i].fd);
            if (linelen != -1) {
                // Write output
                fwrite(line, linelen, 1, stdout);
                continue;
            }

            // EOF, check for error
            if (errno != 0) {
                printf("overlord: %s\n", strerror(errno));
                return 6;
            }

            // Close old stream
            if (fclose(commands[i].fd) == -1) {
                printf("overlord: %s\n", strerror(errno));
                return 7;
            }

            // Restart command
            if (run_command(&commands[i]) == -1) {
                printf("overlord: %s\n", strerror(errno));
                return 8;
            }
        }
    }

    return 0;
}
