# overlord

overlord is a simple process manager written in C.

It runs each command in stdin and prints their output to stdout. 
If a command exits, overlord restarts it. 
If you press Ctrl-C, overlord sends a SIGTERM to all running processes. 
If you press Ctrl-C again, overlord sends a SIGKILL to the remaining processes.
It exits when all processes have exited.

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
