#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main (int argc, char **argv) {
    int rc;
    int n = 0;
    FILE **fds;

    // Read lines
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, stdin)) != -1) {
        // TODO Trim whitespace
        // TODO Ignore empty lines

        // Ignore comments
        if (*line == '#') continue;

        // Run command
        FILE *fd;
        fd = popen(line, "r");
        if (fd == NULL) {
            printf("overlord: %s\n", strerror(errno));
            return 1;
        }

        fds = realloc(fds, sizeof(FILE*) * ++n);
        if (fds == NULL) {
            printf("overlord: %s\n", strerror(errno));
            return 2;
        }

        fds[n-1] = fd;
    }
    if (errno != 0) {
        printf("overlord: %s\n", strerror(errno));
        return 6;
    }

    // Read output of commands
    fd_set set;
    while (n > 0) {
        FD_ZERO (&set);
        for (int i = 0; i < n; ++i) {
            FD_SET (fileno(fds[i]), &set);
        }

        rc = select(FD_SETSIZE, &set, NULL, NULL, NULL);
        if (rc == -1) {
            printf("overlord: %s\n", strerror(errno));
            return 3;
        }

        for (int i = 0; i < n && FD_ISSET (fileno(fds[i]), &set); ++i) {
            linelen = getline(&line, &linecap, fds[i]);
            if (linelen == -1) {
                if (errno != 0) {
                    printf("overlord: %s\n", strerror(errno));
                    return 4;
                }

                // EOF
                rc = pclose(fds[i]);
                if (rc == -1) {
                    printf("overlord: %s\n", strerror(errno));
                    return 5;
                }

                fds[i] = fds[--n];
                break;
            }

            fwrite(line, linelen, 1, stdout);
        }
    }

    return 0;
}
