#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

#define MAX_LINE 80         /* 80 chars per line, per command */
#define NORMAL_MODE 998
#define REDIRECT_INPUT O_RDONLY
#define REDIRECT_OUTPUT (O_WRONLY | O_CREAT | O_TRUNC)
#define EXECUTION_MODE (S_IRUSR | S_IWUSR)
#define PIPED_COMMAND 999
#define READ_END    0
#define WRITE_END    1

void init();

void inputArgs(char *buffer);

void parseArgs(char *line, char **args);

mode_t selectMode(char **args);

void processArgs(char **args, char **history);

void pipeCommand(char **args);

void executeArgs(char **args, mode_t mode);

void redirectData(char **args, int oFlags, mode_t mode, int stream);

void history_push(char **args, char **history);

void printArgs(char **args);

unsigned int countArgs(char **args);

int main(void) {
    char buffer[MAX_LINE];              /* Buffer for holding the input string MAX(80 CHAR) */
    char *args[MAX_LINE / 2 + 1];       /* Buffer for holding parsed arguments maximum 40 */
    char *history[MAX_LINE / 2 + 1];    /* History Buffer for holding prev arguments */
    bool run = true;
    init();

    while (run) {
        inputArgs(buffer);
        parseArgs(buffer, args);
        if (!countArgs(args)) {
            printf("No command was entered.\n");
        } else {
            processArgs(args, history);
        }
    }
}

void init() {
    printf("======================= SHELL =======================\n");
}

void inputArgs(char *buffer) {
    char *b = buffer;
    size_t bufferSize = MAX_LINE;
    printf("osh>");
    fflush(stdout);
    getline(&b, &bufferSize, stdin);
    // REMOVE new line
    size_t len = strlen(buffer);
    if (buffer[len - 1] == '\n')
        buffer[len - 1] = 0;
}

void parseArgs(char *line, char **args) {
    const char *delimiter = " \t";
    char *token;
    int i = 0;
    token = strtok(line, delimiter);
    while (token != NULL) {
        args[i] = token;
        i++;
        token = strtok(NULL, delimiter);
    }
    args[i] = NULL;
}

mode_t selectMode(char **args) {
    mode_t mode = NORMAL_MODE;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0)
            mode = REDIRECT_INPUT;
        else if (strcmp(args[i], ">") == 0)
            mode = REDIRECT_OUTPUT;
        else if (strcmp(args[i], "|") == 0)
            mode = PIPED_COMMAND;
    }
    return mode;
}

void truncateParameters(char **args, int i) {
    for (i = i; args[i] != NULL; i++)
        args[i] = NULL;
}


void processArgs(char **args, char **history) {
    if (strcmp(args[0], "!!") == 0) {
        if (!countArgs(history)) {
            printf("No history.\n");
        } else {
            printf("Executing previous command:\n");
            printArgs(history);
            args = history;
        }
    }
    mode_t mode = selectMode(args);
    if (!countArgs(args)) {
        printf("No command was entered.\n");
    }
    history_push(args, history);
    executeArgs(args, mode);
}

void pipeCommand(char **args) {
    int fd[2];
    int i = 0;
    for (i = 0; strcmp(args[i], "|") != 0; i++);
    if (pipe(fd) < 0) {
        fprintf(stderr, "Failed to open a pipe: %s\n", strerror(errno));
        exit(1);
    }
    pid_t pid = fork();

    if (pid < 0) {
        fprintf(stderr, "Failed to fork a process: %s\n", strerror(errno));
        return;
    } else if (pid == 0) { /* Child Process */
        truncateParameters(args, i);
        if (close(fd[READ_END]) < 0) {
            fprintf(stderr, "Failed to close a file: %s\n", strerror(errno));
            exit(1);
        }
        dup2(fd[WRITE_END], STDOUT_FILENO);

        if (execvp(args[0], (char **) args) < 0)
            fprintf(stderr, "Could not execute command.\n");
        return;
    } else { /* Parent Process */
        int status;
        waitpid(pid, &status, 0);
        args = &args[i + 1];
        if (close(fd[WRITE_END]) < 0) {
            fprintf(stderr, "Failed to close a file: %s\n", strerror(errno));
            exit(1);
        }
        dup2(fd[READ_END], STDIN_FILENO);
        if (execvp(args[0], args) < 0)
            fprintf(stderr, "Could not execute command.\n");
        if (close(fd[READ_END]) < 0) {
            fprintf(stderr, "Failed to close a file: %s\n", strerror(errno));
            exit(1);
        }
    }
}

void executeArgs(char **args, mode_t mode) {
    bool waitForChild = true;
    if (strcmp(args[countArgs(args) - 1], "&") == 0) {
        waitForChild = false;
        args[countArgs(args) - 1] = NULL;
    } else if (args[countArgs(args) - 1][strlen(args[countArgs(args) - 1]) - 1] == '&') {
        args[countArgs(args) - 1][strlen(args[countArgs(args) - 1]) - 1] = 0;
        waitForChild = false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Failed to fork a process: %s\n", strerror(errno));
        return;
    } else if (pid == 0) { /* Child Process */
        switch (mode) {
            case REDIRECT_INPUT:
                redirectData(args, REDIRECT_INPUT, EXECUTION_MODE, STDIN_FILENO);
                break;
            case REDIRECT_OUTPUT:
                redirectData(args, REDIRECT_OUTPUT, EXECUTION_MODE, STDOUT_FILENO);
                break;
            case PIPED_COMMAND:
                pipeCommand(args);
                break;
            default:
                if (execvp(args[0], args) < 0)
                    fprintf(stderr, "Could not execute command.\n");
        }
    } else { /* Parent Process */
        if (waitForChild) {
            int status;
            waitpid(pid, &status, 0);
        }
        return;
    }
}

void redirectData(char **args, int oFlags, mode_t mode, int stream) {
    int fd = open(args[countArgs(args) - 1], oFlags, mode);
    if (fd < 0) {
        fprintf(stderr, "Failed to open a file: %s\n", strerror(errno));
        exit(1);
    }
    dup2(fd, stream);
    truncateParameters(args, countArgs(args) - 2);
    if (execvp(args[0], args) < 0)
        fprintf(stderr, "Could not execute command.\n");
    if (close(fd) < 0) {
        fprintf(stderr, "Failed to close a file: %s\n", strerror(errno));
        exit(1);
    }
}

unsigned int countArgs(char **args) {
    int i = 0;
    while (args[i] != NULL)
        i++;
    return i;
}

void printArgs(char **args) {
    for (int i = 0; args[i] != NULL; i++) {
        printf("Argument %d: %s\n", i, args[i]);
    }
}


void history_push(char **args, char **history) {
    for (int i = 0; args[i] != NULL; i++) {
        if (history[i] != args[i]) {
            history[i] = (char *) malloc(strlen(args[i]) + 1);
            strcpy(history[i], args[i]);
            history[i + 1] = NULL;
        }
    }
}