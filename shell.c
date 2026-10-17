/*
simple Shell 
Author : Bibesh Sedhain

*/

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_LINE 1024
#define MAX_ARGS 128
#define MAX_STAGES 32

extern char **environ;

static volatile sig_atomic_t foreground_group = 0;

/* Handle Ctrl+C */
static void sigint_handler(int sig) {
    (void)sig;

    if (foreground_group > 0)
        kill(-(pid_t)foreground_group, SIGINT);
    else
        write(STDOUT_FILENO, "\n", 1);
}

/* Split user input into tokens */
static int tokenize(const char *line, char **tokens) {
    int count = 0;
    const char *p = line;

    while (*p) {
        while (isspace((unsigned char)*p))
            p++;

        if (!*p)
            break;

        if (count >= MAX_ARGS - 1) {
            fprintf(stderr, "too many arguments\n");
            return -1;
        }

        char *word = calloc(MAX_LINE + 1, 1);

        if (!word) {
            perror("calloc");
            return -1;
        }

        size_t n = 0;

        if (strchr("|<>&", *p)) {
            word[n++] = *p++;
        } else {
            int quote = 0;

            while (*p) {
                char c = *p;

                if (!quote &&
                    (isspace((unsigned char)c) ||
                     strchr("|<>&", c)))
                    break;

                if (c == '\\' && p[1] && quote != '\'') {
                    p++;
                    c = *p++;
                    word[n++] = c;
                    continue;
                }

                if ((c == '\'' || c == '"') &&
                    (!quote || quote == c)) {
                    quote = quote ? 0 : c;
                    p++;
                    continue;
                }

                /* Expand environment variables */
                if (c == '$' && quote != '\'') {
                    p++;

                    char key[256];
                    size_t k = 0;

                    if (*p == '{') {
                        p++;

                        while (*p && *p != '}' &&
                               k < sizeof(key) - 1)
                            key[k++] = *p++;

                        if (*p == '}')
                            p++;
                    } else {
                        while ((isalnum((unsigned char)*p) ||
                                *p == '_') &&
                               k < sizeof(key) - 1)
                            key[k++] = *p++;
                    }

                    if (!k) {
                        word[n++] = '$';
                        continue;
                    }

                    key[k] = '\0';

                    const char *value = getenv(key);

                    if (value) {
                        size_t len = strlen(value);

                        if (n + len > MAX_LINE) {
                            fprintf(stderr,
                                    "expanded command too long\n");
                            free(word);
                            return -1;
                        }

                        memcpy(word + n, value, len);
                        n += len;
                    }

                    continue;
                }

                if (n >= MAX_LINE) {
                    fprintf(stderr, "token too long\n");
                    free(word);
                    return -1;
                }

                word[n++] = *p++;
            }

            if (quote) {
                fprintf(stderr, "unclosed quote\n");
                free(word);
                return -1;
            }
        }

        tokens[count++] = word;
    }

    tokens[count] = NULL;
    return count;
}

/* Free allocated tokens */
static void free_tokens(char **tokens, int count) {
    for (int i = 0; i < count; i++)
        free(tokens[i]);
}

/* Execute built-in shell commands */
static int builtin(char **args) {
    if (!args[0])
        return 0;

    /* cd */
    if (!strcmp(args[0], "cd")) {
        const char *dir = args[1] ? args[1] : getenv("HOME");

        if (!dir)
            dir = "/";

        if (chdir(dir) == -1)
            perror("cd");

        return 0;
    }

    /* pwd */
    if (!strcmp(args[0], "pwd")) {
        char *cwd = getcwd(NULL, 0);

        if (!cwd) {
            perror("pwd");
        } else {
            puts(cwd);
            free(cwd);
        }

        return 0;
    }

    /* echo */
    if (!strcmp(args[0], "echo")) {
        for (int i = 1; args[i]; i++) {
            printf("%s%s", i > 1 ? " " : "", args[i]);
        }

        putchar('\n');
        return 0;
    }

    /* env */
    if (!strcmp(args[0], "env")) {
        if (args[1]) {
            const char *value = getenv(args[1]);

            if (value)
                puts(value);
        } else {
            for (char **e = environ; *e; e++)
                puts(*e);
        }

        return 0;
    }

    /* setenv */
    if (!strcmp(args[0], "setenv")) {
        if (!args[1]) {
            fprintf(stderr,
                    "usage: setenv NAME=VALUE or setenv NAME VALUE\n");
            return 0;
        }

        char *equal = strchr(args[1], '=');

        if (equal) {
            *equal = '\0';

            if (setenv(args[1], equal + 1, 1))
                perror("setenv");

            *equal = '=';
        } else if (args[2]) {
            if (setenv(args[1], args[2], 1))
                perror("setenv");
        } else {
            fprintf(stderr,
                    "usage: setenv NAME=VALUE or setenv NAME VALUE\n");
        }

        return 0;
    }

    return -1;
}

/* Represent a command in a pipeline */
typedef struct {
    char *argv[MAX_ARGS];
    char *input;
    char *output;
} Stage;

/* Parse arguments, pipes, redirects and background flag */
static int parse(char **tokens, int count,
                 Stage *stages, int *background) {
    int stage = 0;
    int argc = 0;

    *background = 0;

    memset(stages, 0, sizeof(Stage) * MAX_STAGES);

    for (int i = 0; i < count; i++) {
        char *t = tokens[i];

        if (!strcmp(t, "&")) {
            if (i != count - 1) {
                fprintf(stderr, "& must be last\n");
                return -1;
            }

            *background = 1;
        }

        else if (!strcmp(t, "|")) {
            if (!argc || stage >= MAX_STAGES - 1) {
                fprintf(stderr, "invalid pipe\n");
                return -1;
            }

            stage++;
            argc = 0;
        }

        else if (!strcmp(t, "<") || !strcmp(t, ">")) {
            if (i + 1 >= count ||
                strchr("|<>&", tokens[i + 1][0])) {
                fprintf(stderr,
                        "missing redirection target\n");
                return -1;
            }

            if (*t == '<')
                stages[stage].input = tokens[++i];
            else
                stages[stage].output = tokens[++i];
        }

        else {
            if (argc >= MAX_ARGS - 1) {
                fprintf(stderr,
                        "too many stage arguments\n");
                return -1;
            }

            stages[stage].argv[argc++] = t;
        }
    }

    if (!argc) {
        fprintf(stderr, "missing command\n");
        return -1;
    }

    return stage + 1;
}

/* Handle input and output redirection */
static int redirect_io(Stage *s) {
    if (s->input) {
        int fd = open(s->input, O_RDONLY);

        if (fd == -1) {
            perror(s->input);
            return -1;
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    if (s->output) {
        int fd = open(s->output,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      0666);

        if (fd == -1) {
            perror(s->output);
            return -1;
        }

        if (dup2(fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}

/* Run built-in commands with redirected I/O */
static void run_builtin_redirected(Stage *s) {
    int in = dup(STDIN_FILENO);
    int out = dup(STDOUT_FILENO);

    if (in == -1 || out == -1) {
        perror("dup");
        goto restore;
    }

    fflush(stdout);

    if (redirect_io(s) == 0)
        builtin(s->argv);

    fflush(stdout);

restore:
    if (in != -1) {
        dup2(in, STDIN_FILENO);
        close(in);
    }

    if (out != -1) {
        dup2(out, STDOUT_FILENO);
        close(out);
    }
}

/* Launch external commands and pipelines */
static void launch(Stage *stages, int n, int background) {
    pid_t pids[MAX_STAGES];
    pid_t group = 0;

    int prev_read = -1;
    int launched = 0;

    for (int i = 0; i < n; i++) {
        int fds[2] = {-1, -1};

        if (i < n - 1 && pipe(fds) == -1) {
            perror("pipe");
            break;
        }

        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");

            if (fds[0] != -1)
                close(fds[0]);

            if (fds[1] != -1)
                close(fds[1]);

            break;
        }

        if (pid == 0) {
            /* Child process */

            signal(SIGINT, SIG_DFL);
            setpgid(0, group ? group : 0);

            if (prev_read != -1) {
                dup2(prev_read, STDIN_FILENO);
                close(prev_read);
            }

            if (fds[1] != -1) {
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
            }

            if (fds[0] != -1)
                close(fds[0]);

            /* Detach background input */
            if (background && i == 0 &&
                prev_read == -1 && !stages[i].input) {
                int nullfd = open("/dev/null", O_RDONLY);

                if (nullfd != -1) {
                    dup2(nullfd, STDIN_FILENO);
                    close(nullfd);
                }
            }

            if (redirect_io(&stages[i]))
                _exit(1);

            /* Built-in inside pipeline */
            if (builtin(stages[i].argv) != -1) {
                fflush(stdout);
                _exit(0);
            }

            /* Execute external command */
            execvp(stages[i].argv[0], stages[i].argv);

            perror("execvp() failed");
            _exit(127);
        }

        /* Parent process */
        if (!group)
            group = pid;

        setpgid(pid, group);

        pids[launched++] = pid;

        if (prev_read != -1)
            close(prev_read);

        if (fds[1] != -1)
            close(fds[1]);

        prev_read = fds[0];
    }

    if (prev_read != -1)
        close(prev_read);

    /* Return immediately for background commands */
    if (background) {
        if (launched)
            printf("[background pid %ld]\n", (long)group);

        return;
    }

    if (launched == 0)
        return;

    foreground_group = group;

    bool done[MAX_STAGES] = {false};
    int remaining = launched;

    struct timespec start, now;

    clock_gettime(CLOCK_MONOTONIC, &start);

    bool timed_out = false;

    /* Wait for foreground processes, with a 10-second limit */
    while (remaining > 0) {
        for (int i = 0; i < launched; i++) {
            if (done[i])
                continue;

            int status;

            pid_t result = waitpid(
                pids[i], &status, WNOHANG
            );

            if (result == pids[i] ||
                (result == -1 && errno == ECHILD)) {
                done[i] = true;
                remaining--;
            }

            else if (result == -1 && errno != EINTR) {
                perror("waitpid");
                done[i] = true;
                remaining--;
            }
        }

        if (!remaining)
            break;

        clock_gettime(CLOCK_MONOTONIC, &now);

        double elapsed =
            (now.tv_sec - start.tv_sec) +
            (now.tv_nsec - start.tv_nsec) / 1e9;

        if (elapsed >= 10.0 && !timed_out) {
            fprintf(stderr,
                    "foreground command exceeded "
                    "10 seconds; terminating\n");

            kill(-group, SIGKILL);
            timed_out = true;
        }

        struct timespec pause_time = {0, 20000000};
        nanosleep(&pause_time, NULL);
    }

    foreground_group = 0;
}

/* Main shell loop */
int main(void) {
    struct sigaction sa = {0};

    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL))
        perror("sigaction");

    char line[MAX_LINE];

    while (1) {
        /* Clean up completed background processes */
        while (waitpid(-1, NULL, WNOHANG) > 0) {}

        /* Display current working directory */
        char *cwd = getcwd(NULL, 0);

        printf("%s> ", cwd ? cwd : "?");

        free(cwd);
        fflush(stdout);

        /* Read user command */
        if (!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) {
                putchar('\n');
                break;
            }

            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }

            perror("fgets");
            break;
        }

        /* Reject overly long commands */
        if (!strchr(line, '\n') && !feof(stdin)) {
            int c;

            while ((c = getchar()) != '\n' && c != EOF) {}

            fprintf(stderr, "command line too long\n");
            continue;
        }

        /* Tokenize input */
        char *tokens[MAX_ARGS] = {0};

        int count = tokenize(line, tokens);

        if (count < 0) {
            for (int i = 0; i < MAX_ARGS; i++)
                free(tokens[i]);

            continue;
        }

        if (count == 0)
            continue;

        Stage stages[MAX_STAGES];
        int bg = 0;

        int n = parse(tokens, count, stages, &bg);

        if (n > 0) {
            /* Exit shell */
            if (n == 1 &&
                !strcmp(stages[0].argv[0], "exit")) {
                free_tokens(tokens, count);
                break;
            }

            /* Check built-in commands */
            bool is_builtin = n == 1 && !bg && (
                !strcmp(stages[0].argv[0], "cd") ||
                !strcmp(stages[0].argv[0], "setenv") ||
                !strcmp(stages[0].argv[0], "pwd") ||
                !strcmp(stages[0].argv[0], "echo") ||
                !strcmp(stages[0].argv[0], "env")
            );

            if (is_builtin) {
                if (stages[0].input || stages[0].output)
                    run_builtin_redirected(&stages[0]);
                else
                    builtin(stages[0].argv);
            } else {
                launch(stages, n, bg);
            }
        }

        free_tokens(tokens, count);
    }

    return 0;
}
