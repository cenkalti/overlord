#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

typedef struct {
    char *line;
    FILE *fd;
} Command;

int     n           = 0;
Command *commands   = NULL;
char    *line       = NULL;
size_t  linecap     = 0;
ssize_t linelen     = 0;

int main () {
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
        commands[i].fd = popen(commands[i].line, "r");
        if (commands[i].fd == NULL) return 4;
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
            rc = pclose(commands[i].fd);
            if (rc == -1) return 7;

            // Restart command
            commands[i].fd = popen(commands[i].line, "r");
            if (commands[i].fd == NULL) return 8;
        }
    }

    return 0;
}
