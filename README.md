# overlord

overlord is a simple process manager written in C.

- overlord runs each command from `stdin` and prints their output to `stdout`.
- If a command exits, overlord restarts it.
- If you press Ctrl-C, overlord sends `SIGTERM` to running processes.
- If you press Ctrl-C again, overlord sends `SIGKILL` to running processes.
- overlord exits after all processes have exit.

## Usage

```bash
$ cat commands
echo foo; sleep 1
echo bar; sleep 1
# this is a comment
$ cat commands | overlord
foo
bar
bar
foo
foo
bar
^C
$
```
