# overlord

overlord is a simple process manager written in C.

It runs each line in standard input as a command.
stdout of commands are combined in stdout of the overlord process.
If a command exits, overlord restarts it.
If you press Ctrl-C, SIGTERM will be sent to all running processes.
If you press Ctrl-C again, SIGKILL will be sent to remaining processes.

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
