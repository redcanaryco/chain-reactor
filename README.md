<p><img src="assets/logo.png" width="225px" /></p>

# Chain Reactor

Chain Reactor is an open source framework for composing executables that
simulate adversary behaviors and techniques on Linux endpoints. Teams can
compose executables that move through the different stages documented in MITRE’s
[ATT&CK](https://attack.mitre.org/) framework, from running malicious code, to
establishing persistence, to escalating privileges, and more.

Chain Reactor allows you to compose Executable and Linkable Format
([ELF](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)) binaries
that perform sequences of actions like process creation, network connections and
more, through the simple configuration of a JSON file. Chain Reactor’s rich
configuration settings let you control the specific
[system calls](http://man7.org/linux/man-pages/man2/syscalls.2.html) used to
perform these actions, giving you greater granularity when testing security
controls.

<p><img src="assets/video.gif" width="500px" /></p>

# How does it work?

Chain Reactor is responsible for running a `reaction`, which is composed of a
list of objectives, called `atoms`. Each `atom` can contain one or many actions,
called `quarks`. Quarks specify the action to take and the subsequent arguments
to use.

While this might sound complex at first, this structure helps with pre-stage
setup, multi-stage objectives, and post-stage cleanup.

# Getting Started

Chain Reactor requires `python3`.

Install dependencies:

Debian:
```
sudo apt install musl-tools
```

RPM:
```
sudo yum install musl-tools
```

*Note: If your repository system doesn't contain musl-tools, you can build it from source:*

```
git clone git://git.musl-libc.org/musl
cd musl && ./configure && sudo make install
```

Build Chain Reactor:
```
make
```

# An illustrative example

Let’s start with a basic chain reaction:

`reaction.json`

```
{
    "name": "simple_reaction",
    "atoms": [
        "HIDDEN-PROCESS-EXEC"
    ]
}
```

`atoms.json`

```
{
    "name" : "HIDDEN-PROCESS-EXEC",
    "execve" : [ "mkdir”, “-p”, “/tmp/.hidden” ],
    "copy" : [ “/proc/self/exe", "/tmp/.hidden/.chain_reactor_hidden" ],
    "execveat" : [ "/tmp/.hidden/.chain_reactor_hidden", "exit" ],
    "remove" : [ "/tmp/.hidden" ]
}
```

To build the ELF executable, we run the following:

`python3 compose_reaction atoms.json reaction.json <output_name_for_executable>`

The details:
- The chain reaction `simple_reaction` is composed of one objective (atom) called `HIDDEN-PROCESS-EXEC`.
- This atom is composed of four actions (quarks).
- The first quark utilizes the [execve](http://man7.org/linux/man-pages/man2/execve.2.html) system call to create a hidden directory.
- The second quark utilizes a built-in function to copy the current running chain reactor process (`/proc/self/exe`) to the newly created hidden directory as a hidden file.
- The third quark utilizes a different system call, [execveat](http://man7.org/linux/man-pages/man2/execveat.2.html), to execute the hidden chain reactor binary. The `exit` argument instructs the newly created chain reactor process to exit without performing additional operations.
- The fourth quark deletes the hidden directory and hidden file.

Here are some questions this chain reaction can help you answer:
- *Visibility*: Does my endpoint security product collect telemetry for all four quarks? Does it handle one, many, or all system calls that can be used to execute a binary?
- *Detection*: Does my endpoint security product alert me to the execution of a hidden binary in a hidden directory?



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
