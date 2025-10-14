#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <errno.h>

#define MAX_CWD_SIZE 1024
#define INIT_CMD_CAP 8

char **inputToCommand(char *);
void freeCommand(char **);
int cd(char *);

/**
 * Main shell lifecycle: Input, parse, execute, free.
 * 
 * Returns: 0 on successful exit
 */
int main() {
    char *input = NULL;
    char **command;

    pid_t child_pid;
    int status;

    char cwd[MAX_CWD_SIZE];
    
    while(1) {

        // Prepend cwd to prompt
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            fprintf(stderr, "Warning: Unable to determine current directory: %d\n", errno);
            strcpy(cwd, "???");  // fallback if cwd error
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

        // Only whitespaces
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
            // Usually out-of-memory issue
            fprintf(stderr, "Error: Failed to create child process: %s\n", strerror(errno));
            free(input);
            freeCommand(command);
            continue;
        } else if (child_pid == 0) {
            // Child path
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
 * Tokenizes input string into a dynamically-allocated array of command arguments.
 * 
 * @param input: The input string to tokenize (not modified by this function)
 * @return: A null terminated array of string pointers. Returns an array with just null if input is
 *          all whitespace. Caller is responsible for freeing with freeCommand().
 * 
 * Note: Calls exit(1) on allocation failure
 */
char **inputToCommand(char *input) {
    int capacity = INIT_CMD_CAP;
    int count = 0;
    char **command = malloc(capacity * sizeof(char *));
    
    if (command == NULL) {
        fprintf(stderr, "Alloc error: Memory allocation failed for command array: %s\n", strerror(errno));
        exit(1);
    }

    // Safety copy since strtok modifies the string
    char *input_copy = strdup(input);
    if (input_copy == NULL) {
        fprintf(stderr, "Alloc error: Memory allocation failed while copying input: %s\n", strerror(errno));
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
                fprintf(stderr, "Alloc error: Memory reallocation failed while expanding command array: %s\n", strerror(errno));
                freeCommand(command);
                free(input_copy);
                exit(1);
            }
            command = temp;
        }
        
        // Duplicate each token so it persists after input_copy is freed
        command[count] = strdup(token);
        if (command[count] == NULL) {
            fprintf(stderr, "Alloc error: Memory allocation failed while copying token: %s\n", strerror(errno));
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
 * @param command: null terminated array of strings to free
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