# overlord

overlord is a simple process manager written in C.

It runs each line in standard input as a command.
stdout of commands are combined in stdout of the overlord process.
If a command exits, overlord restarts it.
If you press Ctrl-C a SIGTERM will be sent to all running processes.
If you press Ctrl-C again a SIGKILL will be sent to the process group.

## Example

```bash
$ cat testfile
echo foo; sleep 1
echo bar; sleep 1
# this is a comment
$ cat testfile | overlord
foo
bar
bar
foo
foo
bar
^Coverlord: received signal: Interrupt: 2
overlord: sending SIGTERM to PID: 16409
overlord: sending SIGTERM to PID: 16411
$
```
