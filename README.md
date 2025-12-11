# bshell

A functional Unix shell built from scratch in C. Used as a tool to learn process management, system calls, signals, and other OS internals.

See the [docs](DOCS.md) for architecture and implementation.

## Utilities

**Processes:** Command parsing and execution in a child process

**Built-in commands:** `cd`, `exit`

**Signal handling:** SIGINT (Ctrl-C) with reset and process cleanup

**I/O redirection:** `<`, `>`, `>>`, `2>` (stderr)

**Input features:** Dynamic directory prompt, command history, and tab completion

## Installation and Usage

### Option 1: Using DevContainer (Recommended)

This project includes the DevContainer configuration used for development.

**Prerequisites:**
- [Docker/Docker Desktop](https://www.docker.com/get-started)
- [Visual Studio Code](https://code.visualstudio.com/)
- [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

**Steps:**
1. Clone the repository:
```bash
   git clone https://github.com/braedenbucher/bshell.git
   cd bshell
```

2. Open the project in VS Code:
```bash
   code .
```

3. When prompted, click "Reopen in Container" (or press `F1` and select "Dev Containers: Reopen in Container")

4. Once the container is built and running, build and run the shell:
```bash
   make build
   ./bshell

   # Alternatively
   make run
```

### Option 2: Local Installation
**Prerequisites:**
- GCC compiler
- Make
- GNU Readline library (`libreadline-dev`)

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y build-essential libreadline-dev
```

**macOS:**
```bash
brew install readline
```

**Build and Run:**
```bash
git clone https://github.com/braedenbucher/bshell.git
cd bshell
make build
./bshell
```

To exit, type `exit`.

> [!WARNING]
> This shell handles lifecycle errors (allocation, forking, processes) and some
> native bash errors, but be cautious when running complex command setups.

## Makefile Targets

`make build` - Compile the shell

`make run` - Build and run the shell

`make clean` - Remove compiled binaries

## References
Development of this shell was guided by the workflow described in [Indradhanush Gupta’s blog series on writing a Unix shell](https://igupta.in/blog/writing-a-unix-shell-part-1/).
Core features of `bshell` were implemented independently and expanded beyond Gupta’s examples.

## Future Additions

- Piping
- Background Processes
- Job Control
- Aliases

## License

MIT License - see [LICENSE](LICENSE) for details

## Contributing

Contributions are welcome. Please open an issue to discuss proposed changes.
