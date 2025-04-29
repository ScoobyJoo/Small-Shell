#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CMD_LEN 2048
#define MAX_ARG 512

// Global variables
int foreground_only = 0;
int last_status = 0;
pid_t foregroundProcess = -1;

// Function for SIGTSTP handler
void handleSIGTSTP(int signo) {
    if (foreground_only) {
        write(STDOUT_FILENO, "Exiting foreground only mode\n", 30);
        foreground_only = 0;
    } else {
        write(STDOUT_FILENO, "Entering foreground only mode (& ignored)\n", 44);
        foreground_only = 1;
    }
}

// Function for SIGINT handler
void handleSIGINT(int signo) {
    if (foregroundProcess != -1) {
        kill(foregroundProcess, SIGINT);
    }
    write(STDOUT_FILENO, " CTRL + C ignored", 17);
    write(STDOUT_FILENO, "\n:", 2); // Print newline after Ctrl+C
}

// Execute external commands
void executeCommand(char *args[], int background, char *input_file, char *output_file) {
    pid_t spawnpid = fork();

    // Child Porcess
    if (spawnpid == 0) {
        // Reset signal handling for SIGINT and SIGTSTP
        struct sigaction default_action = {0};
        default_action.sa_handler = SIG_DFL;
        sigaction(SIGINT, &default_action, NULL);
        sigaction(SIGTSTP, &default_action, NULL);

        // Redirect input if needed
        if (input_file) {
            int in_fd = open(input_file, O_RDONLY);
            if (in_fd == -1) {
                perror("error");
                exit(1);
            }
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }

        // Redirect output if needed
        if (output_file) {
            int out_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd == -1) {
                perror("error");
                exit(1);
            }
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        // Background process input redirection
        if (background && foreground_only == 0) {
            int dev_null = open("/dev/null", O_WRONLY);
            if (dev_null == -1) {
                perror("error");
                exit(1);
            }
            dup2(dev_null, STDIN_FILENO);
            close(dev_null);
        }

        // Execute command
        if (execvp(args[0], args) == -1) {
            perror("error");
            exit(1);
        }
    // Parent Process
    } else if (spawnpid > 0) {
        if (background && foreground_only == 0) {
            printf("background pid is %d\n", spawnpid);
            fflush(stdout);
        } else {
            foregroundProcess = spawnpid;  // Track foreground process
            int child_status;
            waitpid(spawnpid, &child_status, 0);
            foregroundProcess = -1; // Reset after child process exits

            if (WIFEXITED(child_status)) {
                last_status = WEXITSTATUS(child_status);
            } else if (WIFSIGNALED(child_status)) {
                last_status = WTERMSIG(child_status);
                printf("terminated by signal %d\n", last_status);
                fflush(stdout);
            }
        }
    } else {
        perror("fork");
    }
}

// Function for the main loop of the program
void smallsh() {
    char input[MAX_CMD_LEN];
    char *args[MAX_ARG];
    char *input_file = NULL, *output_file = NULL;
    int background;

    // Set up SIGTSTP handler
    struct sigaction SIGTSTP_action = {0};
    SIGTSTP_action.sa_handler = handleSIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Set up SIGINT handler
    struct sigaction SIGINT_action = {0};
    SIGINT_action.sa_handler = handleSIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    while (1) {
        // Check for completed background processes
        int child_status;
        pid_t child_pid;
        while ((child_pid = waitpid(-1, &child_status, WNOHANG)) > 0) {
            printf("background pid %d is done: exit value %d\n", child_pid, WEXITSTATUS(child_status));
            fflush(stdout);
        }

        printf(": ");
        fflush(stdout);
        if (!fgets(input, MAX_CMD_LEN, stdin)) break;
        input[strcspn(input, "\n")] = '\0'; // Remove newline
        if (strlen(input) == 0 || input[0] == '#') continue;

        char *token;
        char *saveptr;
        int arg_count = 0;
        background = 0;
        input_file = output_file = NULL;

        token = strtok_r(input, " ", &saveptr);
        while (token) {
            if (strcmp(token, "&") == 0 && strtok_r(NULL, " ", &saveptr) == NULL) {
                background = 1;
            } else if (strcmp(token, "<") == 0) {
                input_file = strtok_r(NULL, " ", &saveptr);
            } else if (strcmp(token, ">") == 0) {
                output_file = strtok_r(NULL, " ", &saveptr);
            } else {
                args[arg_count++] = token;
            }
            token = strtok_r(NULL, " ", &saveptr);
        }
        args[arg_count] = NULL;

        if (args[0] == NULL) continue;

        // Built-in commands
        if (strcmp(args[0], "exit") == 0) {
            exit(0);
        } else if (strcmp(args[0], "cd") == 0) {
            if (args[1]) {
                if (chdir(args[1]) != 0) {
                    perror("cd");
                }
            } else {
                chdir(getenv("HOME"));
            }
        } else if (strcmp(args[0], "status") == 0) {
            printf("exit value %d\n", last_status);
            fflush(stdout);
        } else {
            executeCommand(args, background, input_file, output_file);
        }
    }
}

int main() {
    smallsh();
    return 0;
}
