#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>

// Constants
#define MAX_CWD_SIZE 1024
#define INIT_CMD_CAP 8
#define TRUNCATE 0
#define APPEND 1

// Structures
struct redirect_info {
    char *input_file;
    char *output_file;
    char *error_file;
    int output_mode;
};

// Global variables
static volatile sig_atomic_t jump_flag = 0;
static sigjmp_buf env;

// Function prototypes
void setup_redirects(char **command, struct redirect_info *redir);
void apply_redirects(struct redirect_info *redir);
void setup_sigaction_handler(void);
void sigint_handler();
char **inputToCommand(char *input);
void freeCommand(char **command);
int cd(char *path);

/**
 * Main shell lifecycle: Input, parse, execute, free.
 */
int main() {
    char *input = NULL;
    char **command;
    pid_t child_pid;
    int status;
    char cwd[MAX_CWD_SIZE];

    setup_sigaction_handler();

    while(1) {
        // Set jump point
        if (sigsetjmp(env, 1) == 42) {
            printf("\n");
        }
        jump_flag = 1;

        // Prepend cwd to prompt
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            fprintf(stderr, "Warning: Unable to determine current directory: %d\n", errno);
            strcpy(cwd, "???");  // Fallback if cwd error
        }
        size_t len = strlen(cwd);
        snprintf(cwd + len, sizeof(cwd) - len, "> ");
        input = readline(cwd);

        // Handle CTRL-D (EOF)
        if (input == NULL) {
            printf("\n");
            break;
        }

        // Handle `Enter` (empty)
        if (strlen(input) == 0) {
            free(input);
            continue;
        }

        add_history(input);

        command = inputToCommand(input);

        struct redirect_info redir;
        setup_redirects(command, &redir);

        // Skip if only whitespaces
        if (!command[0]) {
            free(input);
            freeCommand(command);
            continue;
        }

        // Built-in: exit
        if (strcmp(command[0], "exit") == 0) {
            free(input);
            freeCommand(command);
            break;
        }

        // Built-in: cd
        if (strcmp(command[0], "cd") == 0) {
            if (command[1] == NULL) {
                fprintf(stderr, "cd: missing operand\n");
                fprintf(stderr, "Usage: cd <directory>\n");
            } else if (cd(command[1]) < 0) {
                fprintf(stderr, "cd: cannot change directory to '%s': %s\n", command[1], strerror(errno));
            }
            free(input);
            freeCommand(command);
            continue;
        }

        child_pid = fork();

        if (child_pid < 0) {
            fprintf(stderr, "Error: Failed to create child process: %s\n", strerror(errno));
            free(input);
            freeCommand(command);
            continue;
        } else if (child_pid == 0) {
            // Child path
            apply_redirects(&redir);
            signal(SIGINT, SIG_DFL);
            execvp(command[0], command);
            fprintf(stderr, "Error: Command not found or failed to execute '%s': %s\n", command[0], strerror(errno));
            exit(1);
        } else {
            // Parent path
            waitpid(child_pid, &status, WUNTRACED);
        }

        free(input);
        freeCommand(command);
    }

    return 0;
}

/**
 * Scrapes a command for <, >, >>, 2> and their targets, storing
 * redirect info into a struct for child execution.
 *
 * @param command: Pointer to command array to scrape
 * @param redir: Pointer to redirect_info structure to populate
 */
void setup_redirects(char **command, struct redirect_info *redir) {
    // Initialize the struct
    redir->input_file = NULL;
    redir->output_file = NULL;
    redir->error_file = NULL;
    redir->output_mode = TRUNCATE;
    
    int write_idx = 0; // Where we write cleaned args
    
    // Parse through command array
    for (int i = 0; command[i] != NULL; i++) {
        if (strcmp(command[i], "<") == 0) {
            // Input redirection
            if (command[i + 1] != NULL) {
                redir->input_file = command[i + 1];
                i++; // Skip the filename
            }
        } else if (strcmp(command[i], ">") == 0) {
            // Output redirection (truncate)
            if (command[i + 1] != NULL) {
                redir->output_file = command[i + 1];
                redir->output_mode = TRUNCATE;
                i++;
            }
        } else if (strcmp(command[i], ">>") == 0) {
            // Output redirection (append)
            if (command[i + 1] != NULL) {
                redir->output_file = command[i + 1];
                redir->output_mode = APPEND;
                i++;
            }
        } else if (strcmp(command[i], "2>") == 0) {
            // Error redirection
            if (command[i + 1] != NULL) {
                redir->error_file = command[i + 1];
                i++;
            }
        } else {
            // Regular command argument - keep it
            command[write_idx++] = command[i];
        }
    }
    
    // Null-terminate at the new end
    command[write_idx] = NULL;
}


/**
 * Apply file redirections for stdin, stdout, and stderr.
 * 
 * @param redir: Pointer to redirect_info structure containing redirection parameters
 *
 * Note: This function exits the process on error, as it's intended to be
 * called after forking in a child process. File descriptors are closed after
 * duplication. Calls exit(1) when open/dup2 fails
 */
void apply_redirects(struct redirect_info *redir) {
    int fd;
    
    // Handle input redirection
    if (redir->input_file != NULL) {
        fd = open(redir->input_file, O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "Error: Failed to open input file '%s': %s\n",
                    redir->input_file, strerror(errno));
            exit(1);
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
            fprintf(stderr, "Error: Failed to redirect stdin from '%s': %s\n",
                    redir->input_file, strerror(errno));
            close(fd);
            exit(1);
        }
        close(fd);
    }
    
    // Handle output redirection
    if (redir->output_file != NULL) {
        if (redir->output_mode == APPEND) {
            fd = open(redir->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        } else {
            fd = open(redir->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        if (fd == -1) {
            fprintf(stderr, "Error: Failed to open output file '%s': %s\n",
                    redir->output_file, strerror(errno));
            exit(1);
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
            fprintf(stderr, "Error: Failed to redirect stdout to '%s': %s\n",
                    redir->output_file, strerror(errno));
            close(fd);
            exit(1);
        }
        close(fd);
    }
    
    // Handle error redirection
    if (redir->error_file != NULL) {
        fd = open(redir->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            fprintf(stderr, "Error: Failed to open error file '%s': %s\n",
                    redir->error_file, strerror(errno));
            exit(1);
        }
        if (dup2(fd, STDERR_FILENO) == -1) {
            fprintf(stderr, "Error: Failed to redirect stderr to '%s': %s\n",
                    redir->error_file, strerror(errno));
            close(fd);
            exit(1);
        }
        close(fd);
    }
}

/**
 * Sets up the sigaction struct, linking it to our signal handler,
 * and using it as the handler for SIGINT (CTRL-C).
 */
void setup_sigaction_handler() {
    struct sigaction s;
    s.sa_handler = sigint_handler;
    sigemptyset(&s.sa_mask);
    s.sa_flags = SA_RESTART;
    sigaction(SIGINT, &s, NULL);
}

/*
 * Custom signal handler for the SIGINT signal, jumping back
 * to the parent shell and resetting the prompt.
 */
void sigint_handler() {
    if (!jump_flag) {
        return;
    }
    siglongjmp(env, 42);
}

/**
 * Tokenizes input string into a dynamically-allocated array of command arguments.
 * 
 * @param input: The input string to tokenize
 * @return: A null terminated array of string pointers. Returns an array with just null if input is
 *          all whitespace.
 * 
 * Note: Calls exit(1) on allocation failure
 */
char **inputToCommand(char *input) {
    int capacity = INIT_CMD_CAP;
    int count = 0;
    char **command = malloc(capacity * sizeof(char *));
    
    if (command == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for command array: %s\n", strerror(errno));
        exit(1);
    }

    // Safety copy since strtok modifies the string
    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        fprintf(stderr, "Error: Memory allocation failed while copying input: %s\n", strerror(errno));
        free(command);
        exit(1);
    }

    char *token = strtok(input_copy, " ");
    
    while (token != NULL) {
        // Resize array if needed (leave room for NULL terminator)
        if (count >= capacity - 1) {
            capacity *= 2;
            char **temp = realloc(command, capacity * sizeof(char *));
            if (temp == NULL) {
                fprintf(stderr, "Error: Memory reallocation failed while expanding command array: %s\n", strerror(errno));
                freeCommand(command);
                free(input_copy);
                exit(1);
            }
            command = temp;
        }
        
        // Duplicate each token so it persists after input_copy is freed
        command[count] = strdup(token);
        if (command[count] == NULL) {
            fprintf(stderr, "Error: Memory allocation failed while copying token: %s\n", strerror(errno));
            freeCommand(command);
            free(input_copy);
            exit(1);
        }
        count++;
        token = strtok(NULL, " ");
    }
    
    command[count] = NULL;  // null-terminate the array for execvp
    free(input_copy);
    return command;
}

/**
 * Frees memory used by command array created by inputToCommand().
 * 
 * Iterates through array, freeing each individual string,
 * then frees the array itself. Safe to call with NULL pointer.
 * 
 * @param command: Null terminated array of strings to free
 */
void freeCommand(char **command) {
    if (command == NULL) return;
    
    for (int i = 0; command[i] != NULL; i++) {
        free(command[i]);
    }
    free(command);
}

/**
 * Built-in change directory function.
 * 
 * @param path: The directory path to change to (relative or absolute)
 * @return: 0 on success, -1 on failure (with errno set)
 */
int cd(char *path) {
    return chdir(path);
}