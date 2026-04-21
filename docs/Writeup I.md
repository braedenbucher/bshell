# Bare bones

A **shell** is essentially an interface that allows users to interact with a kernel by translating user commands into system calls that the kernel can understand. The shell provides a way to execute programs and utilities through a command-line interface.

The lifecycle of a shell look like this:
1. Startup the shell
2. Wait for user input
3. Parse user input
4. Execute the command and return the result
5. Go back to `2`.
# Fundamental

**Processes** are how we keep the shell running while executing programs. The shell is the parent process. This is the `main` thread of our program which is waiting for user input. However, we cannot execute the command in the `main` thread itself, because of the following reasons:

1. Execuing the command halts input and control. We can't update the CLI or accept input if it is processing something.
2. An erroneous command will mess with or terminate the shell process.

Commands should have their *own* process blocks. This is known as **isolation** and is a mechanic used in fault tolerance.


## Fork
To handle this, we use the system call `fork()`. `fork()` creates a complete copy of the current process (stack, heap, program, counter, global vars, file descriptors). This process is called the **child process**. Since processes have a unique ID for the kernel to identify them with, the child will have a different PID than the parent.

A second quirky trait is that this means the value returned form `fork()` is different for the parent and child=.
- In the parent, `fork()` returns the PID of the child.
- In the child, `fork()` returns 0.
You can get the PID of the current process with `getpid()`.

*Here's what the setup looks like:*
```
             parent [pid 1234]          child [pid 1235]       
                ┌────────┐                 ┌────────┐          
                │ main() │                 │ main() │          
                └───┬────┘                 └───┬────┘          
         ┌─ ─ ─ ─ ─ └─────────┐     ┌──────────┘ ─ ─ ─ ─┐      
         │child_pid = fork(); ├────►│child_pid = fork();│      
         └─ ─ ─ ─ ─ ┌─────────┘     └──────────┐ ─ ─ ─ ─┘      
                    │                          │               
            child_pid == 1235          child_pid == 0          
            getpid() == 1234           getpid() == 1235        
                    │                          │               
                    ▼                          ▼               
```


## Sleep
If we want to halt our program, say to wait for a process to finish, we can use the `sleep()` system call:

>
> `sleep – suspend execution for an interval of time`
>

Take the following code and it's output, where we sleep before printing on the parent:

```c
int main() {
	pid_t child_pid = fork();
	
	if (child_pid == 0) {
		printf("Child Process here!");
	} else {
		sleep(1);
		printf("Parent Process here!");
	}
	
	return 0;
}

// ===== TERMINAL =====
// $ ./main
// Child Process here!
// Parent Process here!
// $
```

This makes sense, because the child entirely executes and terminates well before our sleep call finishes.

If we put sleep before printing on the child, unexpected behavior happens:

```c
int main() {
	pid_t child_pid = fork();
	
	if (child_pid == 0) {
		sleep(1);
		printf("Child Process here!");
	} else {
		printf("Parent Process here!");
	}
	
	return 0;
}

// ===== TERMINAL =====
// $ ./main
// Parent Process here!
// $ Child Process here!
```

This is because the **parent finished first**, and so the contents of the child, who slept before printing, were dumped into `stdout`.

Using `sleep()` to control your process execution flow is not smart, though:

- You can't guarantee the child process will finish in that time, meaning the parent starts up again before the child is finished.
- BUT ALSO you can't guarantee the child process won't finish faster, meaning the parent just sits there doing nothing after the child is finished.


## Wait
A better approach here would be using the `wait` system call instead (or one of the variants). We will use the `waitpid` system call. This system call halts the current process until a given process (by ID) finishes.

```c
waitpid(pid, &status, options)
```

1. `pid`: Process ID of the process you are waiting for
2. `&status`:  An address which is populated depending on *how* the process was terminated.
3. `options`: Options flag, to customize the behaviour of `waitpid`
	1. `0` = block until child terminates *(normal wait)*
	2. `WNOHANG` = return immediately if child hasn't terminated *(more like a check-in)*
	3. `WUNTRACED` = block until child finishes or is stopped *(CTRL-Z)*
	4. `WCONTINUED` = block until child finishes or is resumed
	   These all can be combined with `|` *(bitwise OR)*

>[!IMPORTANT]
>
> `waitpid()` itself returns:
> The PID of the terminated process
> 1. `0` if you used `WNOHANG` and the child is still running
> 2. `-1` if an error occurred
> 
> `&status` is populated with:
> How the process terminated (exit code, signal, etc.)

If we take our faulty printer from above:

```c
int main() {
	pid_t child_pid;
	pid_t wait_result;
	int status;
	
	child_pid = fork();
	
	if (child_pid == 0) {
		sleep(1);
		printf("Child Process here!");
	} else {
		wait_result = waitpid(child_pid, &status, WUNTRACED);
		printf("Parent Process here!");
	}
	
	return 0;
}

// ===== TERMINAL =====
// $ ./main
// Child Process here!
// Parent Process here!
// $
```

It now pauses, and as soon as the child waits, prints, and finishes, the parent is right there to finish the wraup.


## Exec
All exec functions do the same thing: they replace your current process image with a new program. If successful, they **never return**. If they fail, they return -1 and set errno.

The letters after "exec" tell you how it works:

**l** = **list** arguments passed as separate parameters: `arg0, arg1, arg2, ..., NULL`
**v** = **vector** argument passed as an array: `char *argv[]`
*arg0 and argv[0] are the command.*
**p** = **path** search with your PATH environment variable for the executable
**e** = **environment** lets you pass a custom environment array

With this, we get a table for usage:

| command | arguments    | path to prog | environment |
| ------- | ------------ | ------------ | ----------- |
| execv   | array        | needs path   | current     |
| execve  | array        | needs path   | custom      |
| execvp  | array        | use $PATH    | current     |
| execvpe | array        | use $PATH    | custom      |
| execl   | indiv. param | needs path   | current     |
| execle  | indiv. param | needs path   | custom      |
| execlp  | indiv. param | use $PATH    | current     |
| execlpe | indiv. param | use $PATH    | custom      |

`execvp()`is perfect for shells because:
- It searches PATH *(so users can type "ls" not "/bin/ls")*
- It takes an argv array *(which you probably already built from parsing input)*
- You usually want to inherit the shell's environment

>[!WARNING]
>
>Both `l` and `p` variations require the last parameter fo the command to be `NULL`.

Typical shell pattern:

```c
pid_t pid = fork();
if (pid == 0) {
    // child
    execvp(argv[0], argv);
    perror("execvp failed");  // only runs if exec fails
    exit(1);
} else {
    // parent
    waitpid(pid, &status, 0);
}
```

After `fork()`, the child gets a copy of your code, so you call exec() to **replace** that copy with the actual program the user wants to run. The parent waits for the child to finish.

# Input

We are taking an input from `stdin` and executing our command with `execvp`. Since execvp just needs a:
- **Command** *(argv\[0])*
- **Argument list** *(argv)*

We have to break our string up into an array of strings. We can set up two pointers in our main for the input and command.

To read in input, we could use `scanf` or `fget` but `readline` has a bunch of built in handlers for arrow keys and tabs and such, so we'll be using that.

>[!NOTE]
>
> Importing readline - 
> In our `.devcontainer` we are going to add the following:
> ```json
> "features": { "ghcr.io/devcontainers/features/common-utils:2": {} },
> "postCreateCommand": "sudo apt-get update && sudo apt-get install -y libreadline-dev"
> ```
> 
> And in our `Makefile` we are going to add a `LIBS` and update our `build:` rule:
> ```python
> LIBS = -lreadline
> 
> build:
>     $(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(EXEC)
> 							^^^^^
> ```
> 
> Lastly, we are going to import with `#include <readline/readline.h>`.

Our input needs to be tokenized, so we'll set up a function which uses `strtok`  to break it up:

```c
char **inputToCommand(char *input) {
	int capacity = INIT_CMD_CAP;
	int count = 0;
	char **command = malloc(capacity * sizeof(char *));
	char *input_copy = strdup(input);
	char *token = strtok(input_copy, " ");
	
	while (token != NULL) {
		// Resize if needed (leave room for NULL terminator)
		if (count >= capacity - 1) {
			capacity *= 2;
			char **temp = realloc(command, capacity * sizeof(char *));
			if (temp == NULL) {
				perror("realloc failed");
				freeCommand(command);
				free(input_copy);
				exit(1);
			}
			command = temp;
		}
		//Alocate token to command
		command[count] = strdup(token);
		count++;
		token = strtok(NULL, " ");
	}
	command[count] = NULL;  // NULL-terminate the array
	free(input_copy);
	return command;
}
```
*We could do this much easier by just capping the array with `*command[1024]`, but I found it good practice for array management. This code does assume perfect input and parsing, we will handle errors later.*

Now, back in main, we just take our input:
```c
int main() {
	char *input = NULL;
	char **command;
	
	while(1) {
		input = readline(cwd);
		command = inputToCommand(input);
		
		child_pid = fork();
		
		if (child_pid == 0) {
			execvp(command[0], command);
			fprintf(stderr, "Execvp failed: %s\n", command[0]);
			exit(1);
		} else {
			waitpid(child_pid, &status, WUNTRACED);
		}
	}
	free(input);
	freeCommand(command);
	return 0;
}
```

# Error Handling

We have to cover our bases when it comes to invalid inputs, process failures, and memory safety, so here's a run through of all the places it's best to run a check:

## Input Handling Errors

1. `getcwd()` failure - Unable to determine current working director, can occur if directory was deleted or permission denied:
```c
if (getcwd(cwd, sizeof(cwd)) == NULL) {
	fprintf(stderr, "Warning: Unable to determine current directory: %d\n", errno);
	strcpy(cwd, "???");
}
```

2. `readline()` returns NULL - EOF/CTRL-D pressed
```c
if (input == NULL) {
	printf("\n");
	break;
}
```

3. Empty input - User presses Enter with no command
```c
if (strlen(input) == 0) {
	free(input);
	continue;
}
```

4. Input contains only spaces - Detected after tokenization when `command[0]` is NULL
```c
if (!command[0]) {
	free(input);
	freeCommand(command);
	continue;
}
```


## Memory Allocation Errors

1. `malloc()` failure in `inputToCommand()` - Initial command array allocation fails, typically occurs when system is OOM
```c
if (command == NULL) {
	fprintf(stderr, "Alloc error: Memory allocation failed for command array: %s\n", strerror(errno));
	exit(1);
}
```

2. `strdup()` failure on input copy - Cannot duplicate input string, generic OOM
```c
if (input_copy == NULL) {
	fprintf(stderr, "Alloc error: Memory allocation failed while copying input: %s\n", strerror(errno));
	free(command);
	exit(1);
}
```

3. `realloc()` failure - Cannot expand command array when cap exceeded, generic OOM
```c
if (temp == NULL) {
	fprintf(stderr, "Alloc error: Memory reallocation failed while expanding command array: %s\n", strerror(errno));
	freeCommand(command);
	free(input_copy);
	exit(1);
}
```

4. `strdup()` failure on token - Cannot duplicate individual token, generic OOM
```c
if (command[count] == NULL) {
	fprintf(stderr, "Alloc error: Memory allocation failed while copying token: %s\n", strerror(errno));
	freeCommand(command);
	free(input_copy);
	exit(1);
}
```


## Process Creation Errors

1. `fork()` failure - Cannot create child process, usually out of memory or process limit reached
```c
if (child_pid < 0) {
	fprintf(stderr, "Error: Failed to create child process: %s\n", strerror(errno));
	free(input);
	freeCommand(command);
	continue;
}
```

2. `execvp()` failure - Command not found or cannot execute, occurs when command doesn't exist in PATH or lacks execute permissions
```c
execvp(command[0], command); 
// execvp replaces process, exits if successful, below shoudn't execute
fprintf(stderr, "Error: Command not found or failed to execute '%s': %s\n", command[0], strerror(errno));
exit(1);
```

# Built-in Commands

## Change Directory
If you try to execute a command like `cd` in the shell, you get an error. The reason is that it's not actually a system program like `ls` or `pwd`, and actually depends on the process it's run from. If you think about our execution flow, this makes sense:

1. User sends `cd <dir>` to parent processs (shell).
2. Process is forked to child.
3. `cd <dir>` is executed in child.
4. Child is exited.

We have to write our own, which isn't so bad, as C has `chdir` buit-in. We just have to make a method which returns the status return of `chdir`. 

```c
int cd(char *path) {
	return chdir(path);
}
```

This means, however, we can't send `cd` off to a child, so we have to run the check **before** forking the parent process. 

```c
int main() {
// get input

	if (strcmp(command[0], "cd") == 0) {
		cd(command[1])
		free(input);
		freeCommand(command);
		continue;
	}
	
// fork and execute
}
```
### Cd Errors

1. `cd` with no argument - Missing directory operand
```c
if (command[1] == NULL) {
	fprintf(stderr, "cd: missing operand\n");
	fprintf(stderr, "Usage: cd <directory>\n");
}
```

2. `cd()` failure - `chdir()` system call fails, can occur with invalid paths or insufficient permissions, we'll replace the call for consistency
```c
} else if (cd(command[1]) < 0) {
	fprintf(stderr, "cd: cannot change directory to '%s': %s\n", command[1], strerror(errno));
}
```


## Exit
We can type in `exit` and successfully exit the shell, but that leaves unfreed memory. We can put a quick check in the same location as `cd`, after input but before fork, to gracefully exit the shell:
```c
int main() {
// get input

	if (strcmp(command[0], "exit") == 0) {
		free(input);
		freeCommand(command);
		continue;
	}
	
// fork and execute
}
```

# Signal Handlers

## Signals
A signal is a **notification** that interrupts a process. They can come from anywhere, and when it arrives the process stops its execution, and runs a special function called a  **signal handler**, and then the process typically resumes. The signal can interrupt between instructions or even mid-instruction, and they run "asynchronously".

>[!IMPORTANT]
>
> Process handling - 
> When a signal arrives, the process stops execution, not the program as a whole (if we're talking about a multi-process setup). Each process has its own execution context. When a signal is delivered to a specific process, only _that_ process gets interrupted and runs the signal handler.
>
>With our setup, running a parent and child process, each process recieves the same signal, and if both have handlers set up, use their own signal handler.

>[!NOTE]
> 
> "Asynchronously" - 
>Imagine your program is executing line 50, and suddenly a signal arrives. The CPU stops executing line 50 (even mid-instruction), switches to running the signal handler code, then returns to finish line 50. It's all in the same thread, but the handler can interrupt at unpredictable moments.


## Making Signal handlers
We can call a function `signal()` which can 'catch' signals and customize their behavior, or call a custom sighandler. For us, a basic example is catching `CTRL-C`, which throws the signal `SIGINT`.

```c
signal(SIGINT, sigint_handler);  // Use custom handler
signal(SIGINT, SIG_IGN);         // Ignore the signal
signal(SIGINT, SIG_DFL);         // Reset to default behavior
```

The challenge is that if we declare these in our parent, since fork copies **all** information to the child, the child will use the same handler and do the exact same thing:

```
			      ┌───────┐           ┌───────┐     
			      │ Shell │           │ Child │
			      └───────┘           └───┬───┘
			          │                   │
			signal(SIGINT,IGN) ─> signal(SIGINT,IGN)
			          │     ▲             │      ▲
			       fork();──┼──────────fork();   │
			          │     │             │      │
			SIGINT────┼─────┴─────────────┼──────┘
			          │                   │      ▲
			                                   bad!
```

To solve this, we declare the ==signal handler for the parent outside of the loop==. Then, in the `fork()` block the child executes in, change the signal behavior for `SIGINT`. Since the child process dies with the execvp call, the parent process keeps it's handler.

```c
int main() {
	
	signal(SIGINT, SIG_IGN);
	
	child_pid = fork();
	
	if (child_pid == 0) {
		signal(SIGINT, SIG_DFL);
		execvp(command[0], command);
	}
}
```


## Signal Masks
Each process has a **signal mask**, a filter of signals which is maintained by the kernel. Signals on that list get blocked rather than delivered immediately. Instead of a queue, it's stored in a binary flag *(this signal is pending/not pending)*. The primary reason is to prevent signals from interrupting their own handlers:

1. When a sighandler runs, that signal is automatically added to the mask.
2. If that signal arrives, it gets blocked, and marked as 'pending' by the kernel
3. If it arrives again, it's still marked as pending, so nothing changes.
4. When the handler is finished, it gets unblocked and delivered

*If multiple signals arrive, that behavior is undefined, the kernel decides depending on its implementation. Some signals do queue, but they are more advanced than is necessary here.*


## Jumps
If we want our shell to restart when we hit `CTRL-C`, we need to use **jumps**. Jumps allow you to set points you can return to, even from outside the scope. 

We have to declare a global variable `env` to keep track of the environment (`setjmp()` initializes this).

```c
jmp_buf env;

setjmp(env); // Mark this spot
longjmp(env, 35); // Jump back to that spot
```

>[!TIP]
>
> Tracking jumps - 
>The number is completely arbitrary. `setjmp()` returns `0` the first time it's called (when setting up the jump point). When you call `longjmp()`, the value you passed in gets returned through `sigjmp()`, distinguishing "first time here" vs. "jumped back here". Just put any non-zero value into `longjmp()`.
> ```c
> if (setjmp(env) == 35) { // returns 0 first time, doesn't trigger
>     printf("Came from longjmp!\n");
> }
> 
> // Later, in signal handler:
> longjmp(env, 35);  // Jump back, but setjmp returns 42 this time
> ```

If we were to jump *from inside* the signal handler, however, the signal would stay blocked because it never returned normally.

We solve this with a variant: `sigsetjmp()` and `siglongjmp()`. These have macros which handle the signal masks during jumps. For our usage, we only need to save the signal mask, which is `1`. We also have a different environment variable to use.

```c
sigjmp_buf env;

sigsetjmp(env, 1);        // The '1' means "save signal mask"
siglongjmp(env, 35);      // Jump back AND restore signal mask
```

>[!WARNING]
>
> Safety check - 
> If `Ctrl-C` is pressed after the handler is registered but *before* the jump point is set, the handler runs and tries to call `siglongjmp(env, 42)` but `env`(which contains our signal setup and jump points) hasn't been initialized by `sigsetjmp()`. This would cause a crash because you're jumping to garbage data. We mitigate this by making a flag which prevents the handler from attempting the jump until the jump point has been properly established.
> 
> ```c
> static volatile sig_atomic_t jump_flag = 0;
> 
> void sigint_handler(int signo) {
> 	if (!jump_flag) {
> 		return;
> 	}
> 	siglongjmp(env, 35);
> }
> 
> int main() {
> 	sigsetjmp(env, 1);
> 	jump_flag = 1; // NOW it's safe to jump
> }
> ```
> 
> - **`volatile`**: Compiler must always read fresh value
> *This prevents the compiler from thinking it's a constant and skipping checks, since it can be modified by a handler between instructions or mid-instruction. Must always be read from memory.*
> 
> - **`sig_atomic_t`**: Read/write operations are atomic
> *Reading or writing it happens in one indivisible operation, even if a signal interrupts.*


## Sigaction
`signal()` actually isn't the most reliable way to handle signals. `sigaction()` was created to properly handle signals more consistently.

We need to fill out the `sigaction` struct and it's fields to describe how we want to handle a signal.

```c
struct sigaction signal_struct;
```

First, our **custom signal handler function**:

```c
void sigint_handler(int signo) {}

signal_struct.sa_handler = sigint_handler;
```

Then, our **masks**, any additional signals to block while our handler is running. Since we don't need to block any others(the signal we are handling is auto-blocked), we use `sigemptyset()`. Since the field `sa_mask` is expecting a set of signals, we create an empty one and use the address of the field to link it to the struct.

```c
sigemptyset(&s.sa_mask);

// Example: Also block SIGTERM while handling SIGINT
sigaddset(&s.sa_mask, SIGTERM);
```

Next, any **flags** that the handler takes into account. These are just predefined behaviors, and in our case we want `SA_RESTART`.

This handles system calls that get interrupted by signals. Normally the signal handler would run and finish, but this leaves our system call returning an error and we have to restart the operation ourselves. `SA_RESTART` restarts any system calls that were interrupted by the signal and resumes after the handler finishes

```c
s.sa_flags = SA_RESTART;
```

Lastly, we tie it up with the `sigaction()` function, which tells the kernel to use the following confguration for signal interrupts:
```c
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
```

- **`SIGINT`**: Which signal we're configuring *(`Ctrl-C`)*
- **`&s`**: The address of our struct with the new configuration
- **`NULL`**: Where to save the old configuration *(we don't care, so pass NULL)*


## Implementation
Here's a code block recap of everything we added:

```c
#include <signal.h>
#include <setjmp.h>
static volatile sig_atomic_t jump_flag = 0;
static sigjmp_buf env;

void setup_sigaction_handler();
void sigint_handler();

int main() {
	// vars
	setup_sigaction_handler();
	
	while(1) {
		if (sigsetjmp(env, 1) == 35) {
			printf("\n");
		}
		jump_flag = 1;
		
		// code
		} else if (child_pid == 0) {
			signal(SIGINT, SIG_DFL);
			execvp(command[0], command);
			// rest of child block
		}
		// code
	}
}

void setup_sigaction_handler() {
	struct sigaction s;
	s.sa_handler = sigint_handler;
	sigemptyset(&s.sa_mask);
	s.sa_flags = SA_RESTART;
	sigaction(SIGINT, &s, NULL);
}

void sigint_handler() {
	if (!jump_flag) {
		return;
	}
	siglongjmp(env, 35);
}
```

*We don't need a `sigaction()` call inside the child block, because we aren't calling our custom function, just resetting the default behavior with `signal()` instead.*

# Redirection

Bash supports various types of I/O **redirections**, which allow manipulation of file descriptors (stdin, stdout, stderr, and others). While there are only three standard file descriptors (0 for stdin, 1 for stdout, and 2 for stderr), there are many more.

Core Redirection Operators:
- `>`: Redirects standard output (file descriptor 1) to a file, overwriting its contents.
- `>>`: Redirects standard output to a file, appending to its contents.
- `<`: Redirects standard input (file descriptor 0) from a file.
- `2>`: Redirects standard error (file descriptor 2) to a file, overwriting its contents.
- `2>>`: Redirects standard error to a file, appending to its contents.


# Fun little things

## Readline history
Readline supports using the arrow keys to scroll between history. We can  save an input to the history with `add_history()` after running our input checks but before parsing the command.

```c
int main() {
// input & input error handling
	add_history(input);
	
	command = inputToCommand(input);
// fork and execute
}
```

## Directory prompt
Many shells put the working directory in their prompt. We can do this, but to keep the readline impementation we can't just prepend the working directory to the readline prompt.

Since `getcwd()` fetches the working directory BUT returns a status, we'll write to a `cwd` string but run the check instantly, using a fallback prompt if there is an issue.

We'll catch the string length because we have to account for the `"> "` in the prompt, then plug it into `snprintf()` and subsequently into the readline. We use `snprintf()` because it takes in the number of bytes to write to the destination  (including null term), and return excess if there was truncation, but that's not entirely necessary for us. 

```c
char cwd[MAX_CWD_SIZE];

while(1) {
	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		fprintf(stderr, "Warning: Unable to determine current directory: %d\n", errno);
		strcpy(cwd, "???");  // fallback if cwd error
	}
	size_t len = strlen(cwd);
	snprintf(cwd + len, sizeof(cwd) - len, "> ");
	input = readline(cwd);
}
```
