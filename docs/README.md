# Overview

An overview of Chain Reactor is documented [here](../README.md)

# Reactions

A reaction is composed of a list of objectives, called `atoms`.

*Example (reaction.json):*

```
{
    "name": "name_of_reaction",
    "atoms": [
      "EXAMPLE-ATOM-1",
      "EXAMPLE-ATOM-2"
    ]
}
```

A `reaction` is composed into an ELF executable by using the `compose_reaction` script:

```
python3 compose_reaction atoms.json reaction.json <output_name_for_executable>
```

# Atoms

An `atom ` achieves an objective through one or many actions, called `quarks`.

All `atoms` are specified in a JSON data file and are executed in order.

*Example (atoms.json):*

```
[
    {
        "name": "EXAMPLE-ATOM-1",
        "<quark-type>": [QUARK-ARGS]
    },
    {
        "name": "EXAMPLE-ATOM-2",
        "<quark-type>": [QUARK-ARGS],
        "<quark-type>": [QUARK-ARGS]
    }
]
```

# Quarks

Each `atom` utilizes one or many actions, called `quarks`. Quarks specify the action to take and the subsequent arguments to use.

`Quarks` are executed in order. If a `quark` fails for any reason, subsequent `quarks` will not execute.

Available `quarks` are outlined below.

### execve, execveat

*Examples:*

```
{
    "name" : "NIX-WHOIS-TRANSFER",
    "execve" : [ "whois", "-h", "redcanary.com", "-p", "443", "iioo" ],
    "execveat" : [ "whois", "-h", "redcanary.com", "-p", "443", "iioo" ]
}
```
Details:
- The [execve](http://man7.org/linux/man-pages/man2/execve.2.html) and [execveat](http://man7.org/linux/man-pages/man2/execveat.2.html) `quarks` take the same arguments.
- The only difference between the two quarks is the underlying syscall that is used. Both will fork the reactor process and execute the list of CLI arguments provided.
- The first argument will be used as the executable path, and the entire list will be passed as its
arguments.
- The path environment variable will be included in the search for the
executable file.
- [stdin](http://man7.org/linux/man-pages/man3/stdin.3.html), [stdout](http://man7.org/linux/man-pages/man3/stdin.3.html), and [stderr](http://man7.org/linux/man-pages/man3/stdin.3.html) will all be redirected to [/dev/null](https://en.wikipedia.org/wiki/Null_device). Take this into account when executing things that rely on TTY's being present.
- The reactor process will wait for the new process to exit before continuing on to the next action.

Important note: `execveat` was added to the Linux kernel in v3.19. Kernel versions below this will fail with an `ENOSYS` error.

### fork-and-rename

Example:

```
{
    "name" : "NIX-WHOIS-TRANSFER-FAKE",
    "fork-and-rename" : [ "whois", "-h", "redcanary.com", "-p", "443", "iioo" ],
    "connect" : { "method": "socketcall", "protocol": "tcp4", "address": "redcanary.com", "port": 443 }
}
```
Details:
- `fork-and-rename` will [fork](http://man7.org/linux/man-pages/man2/fork.2.html) to create a new process, copy the Chain Reactor executable to a temporary directory, and execute it with the specified command line.
- The process name is user defined. The example above has the Chain Reactor process purporting to be the `whois` binary.
- Subsequent `quarks` after `fork-and-rename` will be executed in the new forked process.
- `fork-and-rename` can be nested to create child processes.

### connect

Example:

```
{
    "name" : "C2-BEACON",
    "fork-and-rename" : [ "crontab" ],
    "connect" : { "method": "socketcall", "protocol": "tcp4", "address": "google.com", "port": 443 }
}
```
Details:
- `connect` establishes a network connection and sends `512` bytes of random data.
- `connect` takes four arguments.
  - `method`: `socketcall` or `syscall` (more on this below)
  - `protocol`: `tcp4`, `tcp6`, `udp4`, or `udp6`
  - `address`: The target address. DNS, IPV4, and IPV6 addresses are supported.
  - `port`: The target port.

Details on `method`:
- If `socketcall` is defined, then the `socketcall` ABI is utilized.
- If `syscall`, then the following syscalls are utilized:
    - For TCP connections: [socket](http://man7.org/linux/man-pages/man2/socket.2.html), [connect](http://man7.org/linux/man-pages/man2/connect.2.html), [send](http://man7.org/linux/man-pages/man2/send.2.html)
    - For UDP connections: [socket](http://man7.org/linux/man-pages/man2/socket.2.html), [sendto](http://man7.org/linux/man-pages/man3/sendto.3p.html)

### listen

Example:

```
{
    "name" : "C2-BIND",
    "fork-and-rename" : [ "crontab" ],
    "listen" : { "method": "socketcall", "protocol": "udp4", "address": "0.0.0.0", "port": 443 }
}
```
Details:
- `listen` listens for a connection of the specified type.
- Chain Reactor will `fork` and do an implicit `connect` targeting the newly created socket to simulate
a connection.
- Listening for a network connection may require elevated privileges, so consider whether you want to run Chain Reactor as `root` or via `sudo`.

- `listen` takes four arguments.
  - `method`: `socketcall` or `syscall` (more on this below)
  - `protocol`: `tcp4`, `tcp6`, `udp4`, or `udp6`
  - `address`: The network interface that is used to listen on. `0.0.0.0`, `::/0`, `127.0.0.1`, and `::1/128` are supported.
  - `port`: The port to listen on.

Details on `method`:
- If `socketcall` is defined, then the `socketcall` ABI is utilized.
- If `syscall`, then the following syscalls are utilized:
    - For TCP connections: [socket](http://man7.org/linux/man-pages/man2/socket.2.html), [bind](http://man7.org/linux/man-pages/man2/bind.2.html), [listen](http://man7.org/linux/man-pages/man2/listen.2.html), [accept4](http://man7.org/linux/man-pages/man2/accept.2.html), [recv](http://man7.org/linux/man-pages/man2/recv.2.html)
    - For UDP connections: [socket](http://man7.org/linux/man-pages/man2/socket.2.html), [bind](http://man7.org/linux/man-pages/man2/bind.2.html), [recv](http://man7.org/linux/man-pages/man2/recv.2.html)

### copy

Example:

```
{
    "name" : "LINUX-SHM-DIR-EXECUTION",
    "copy" : [ "/proc/self/exe", "/dev/shm/chain_reactor" ],
    "execve" : [ "/dev/shm/chain_reactor", "exit" ],
    "remove" : [ "/dev/shm/chain_reactor" ]
}
```

Details:
- `copy` duplicates a file to another location, overwriting the destination if it
exists.
- The destination must be a file, or a name in a directory that does not exist.
- `copy` does not support duplicating directories, or copying into a directory and
appending the source's name.

### remove

Example:

```
{
    "name" : "LINUX-SHM-DIR-EXECUTION",
    "copy" : [ "/proc/self/exe", "/dev/shm/chain_reactor" ],
    "execve" : [ "/dev/shm/chain_reactor", "exit" ],
    "remove" : [ "/dev/shm/chain_reactor" ]
}
```
Details:
- `remove` is similar to `rm -rf`. It will delete files or directories.
- Any number of targets may be specified.
- No errors will be generated during this operation.
- Be careful when specifying paths, as this quark will indiscriminately delete everything specified in the list.

### chown, fchown, fchownat, lchown

Example:

```
{
    "name" : "CHOWN-EXISTING-FILE",
    "chown" : { "path" : "/tmp/cr.path.test", "user" : "1000", "group" : "nogroup" },
    "fchown" : { "path" : "/tmp/cr.descriptor.test", "user" : "1000", "group" : "nogroup"  },
    "fchownat" : { "path" : "/tmp/cr.at.test", "user" : "1000", "group" : "nogroup"  },
    "lchown" : { "path" : "/tmp/cr.link.test", "user" : "1000", "group" : "nogroup"  }
}
```

Details:
- Change the ownership of a file object
- `user` or `group` are both strings. If a named user or group does not exist,
  an attempt will be made to convert the string to a number. In the example above
  the `user` field will be translated into the `uid` of `1000`, while `group` will
  is set to `nogroup` a common default group entry, and will be translated to the
  correct value.
- It's common that changing ownership requires elevated privileges.

### chmod, fchmod, fchmodat

Example:

```
{
    "name" : "CHMOD-EXISTING-FILE",
    "chmod" : { "path" : "/tmp/cr.path.test", "mode" : "600" },
    "fchmod" : { "path" : "/tmp/cr.descriptor.test", "mode" : "060" },
    "fchmodat" : { "path" : "/tmp/cr.at.test", "mode" : "606"  }
}
```

Details:
- Change the file permissions of a target file.
- `mode` should be a string in [octal format](http://man7.org/linux/man-pages/man2/chmod.2.html).

### file-touch

```
{
    "name" : "TOUCH-TMP-NEW-FILE",
    "file-touch" : { "path" : "/tmp/cr.test" }
}
```

Details:
- Creates a file if it doesn't exist at the target `path`.
- Does not error if the file already exists.

### file-create

```
{
    "name" : "TOUCH-TMP-TRUNCATE-IF-EXISTS",
    "file-create" : { "path" : "/tmp/cr.test", data : "Hello World!\n", backup-and-revert : false  },
    "file-create" : { "path" : "/etc/passwd", data : "/etc/passwd", backup-and-revert : true }
}
```

Details:
- Creates a file, truncating if it exits.
- `data` can a string or a file path. If `data` is a string, all escape sequences
  will be turned into binary so `\n`, and `\x00` work correctly. When `data` is
  a file path, that file will be read during composition time, and baked into
  the chain reactor deliverable.
- `backup-and-revert` creates a backup of the target file specified by `path`.
  If the target file does not exist, this field has no effect.

### file-append

```
{
    "name" : "PERSIST_CRONTAB",
    "file-append" : { "path" : "/etc/crontab", data : "\n1 *	* * *	root   /var/www/malware-r-us/userkit\n", backup-and-revert : true  },
}
```

Details:
- Appends to an existing file, fails if the file does not exist.
- `data` can a string or a file path. If `data` is a string, all escape sequences
  will be turned into binary so `\n`, and `\x00` work correctly. When `data` is
  a file path, that file will be read during composition time, and baked into
  the chain reactor deliverable.
- `backup-and-revert` creates a backup of the target file specified by `path`.
  If the target file does not exist, this field has no effect.
