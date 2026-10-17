# Project 1: Quash – A Simple UNIX Shell

**Course:** Operating Systems  
**Student:** [Your Name]  
**Language:** C

## 1. Project Overview

Quash is a simple UNIX command-line shell written in C. The goal of this project is to understand how operating systems handle command execution, process creation, signals, and interprocess communication.

The shell accepts user commands, executes built-in commands, and runs external programs using UNIX system calls.

### Features Implemented
- Shell prompt displaying the current working directory
- Built-in commands: `cd`, `pwd`, `echo`, `exit`, `env`, `setenv`
- External command execution using `fork()` and `execvp()`
- Background process execution using `&`
- Ctrl+C (`SIGINT`) signal handling
- Automatic termination of foreground processes exceeding 10 seconds
- Input redirection (`<`)
- Output redirection (`>`)
- Piping between commands (`|`)

All six required tasks are implemented, including all three optional I/O features for extra credit.

## 2. Implementation and Design

### Task 1: Shell Prompt and Built-in Commands

The shell uses `getcwd()` to display the current working directory as its prompt. User input is collected using `fgets()` and split into arguments by the `tokenize()` function.

The built-in commands are handled by the `builtin()` function:

- `cd` uses `chdir()` to change directories.
- `pwd` uses `getcwd()` to print the current directory.
- `echo` prints text and supports environment variable expansion.
- `env` displays environment variables using `environ` and `getenv()`.
- `setenv` creates or updates variables using `setenv()`.
- `exit` terminates the shell.

Environment variables beginning with `$` are expanded during tokenization.

### Task 2: Process Creation

When the user enters an external command, Quash uses `fork()` to create a child process.

The child executes the requested program using `execvp()`, while the parent uses `waitpid()` to wait for the foreground process to finish.

Errors during execution are reported using `perror()`.

### Task 3: Background Processes

Commands ending with `&` run in the background.

The shell creates a child process but does not wait for it to finish, allowing the user to enter more commands immediately.

Completed background processes are periodically collected using `waitpid()` with `WNOHANG` to prevent zombie processes.

### Task 4: Signal Handling

The shell handles Ctrl+C using `sigaction()` and a custom `SIGINT` handler.

Instead of terminating the shell, the interrupt is forwarded to the active foreground process group.

Child processes restore default signal handling before executing external programs.

### Task 5: Process Timeout

Foreground commands are limited to 10 seconds of execution.

The shell uses `clock_gettime()` to track elapsed time and `waitpid()` to check whether the process has finished.

If the process exceeds the limit, `kill()` sends `SIGKILL` to terminate the foreground process group.

Background processes are not affected by this timeout.

### Task 6: I/O Redirection and Piping

All three optional I/O features are implemented.

**Output redirection (`>`):** Uses `open()` and `dup2()` to write command output into a file.

```bash
ls > output.txt
```

**Input redirection (`<`):** Redirects file contents to a command's standard input.

```bash
cat < output.txt
```

**Piping (`|`):** Uses `pipe()`, `fork()`, and `dup2()` to connect the output of one process to the input of another.

```bash
ls | grep shell
```

Multiple commands can be connected through pipes.

## 3. Code Structure

The implementation is organized into the following functions:

| Function | Purpose |
|---|---|
| `main()` | Displays prompt and processes commands |
| `tokenize()` | Splits input and expands variables |
| `parse()` | Identifies arguments, pipes, and redirection |
| `builtin()` | Executes built-in commands |
| `redirect_io()` | Handles input/output redirection |
| `run_builtin_redirected()` | Handles redirection for built-in commands |
| `launch()` | Creates and manages child processes |
| `sigint_handler()` | Handles Ctrl+C |
| `free_tokens()` | Frees dynamically allocated tokens |

## 4. Design Choices and Challenges

The shell was designed using separate functions for parsing, command execution, and process management to make the code easier to maintain.

Built-in commands such as `cd` execute in the parent process because changes to the working directory must persist within the shell.

A `Stage` structure stores each command's arguments and redirection information, simplifying pipeline execution.

Process groups are used to manage signals and timeouts for multiple foreground processes.

One challenge was distinguishing foreground and background execution. This was handled using a background flag and nonblocking process checks.

Another challenge was managing file descriptors when implementing pipes and redirection. Unused descriptors are closed, and standard input/output descriptors are restored after redirected built-in commands.

## 5. Compilation and Testing

### Compile

```bash
gcc -Wall -Wextra -std=c11 -o shell shell.c
```

### Run

```bash
./shell
```

### Test Commands

```bash
pwd
cd ..
echo hello world
setenv greeting=hello
echo $greeting
env greeting
ls -l
sleep 5 &
sleep 15
echo hello > output.txt
cat < output.txt
ls | grep shell
exit
```

### Expected Results

- Built-in commands execute correctly.
- External programs run through child processes.
- Background commands return control immediately.
- Ctrl+C interrupts foreground processes without exiting the shell.
- Foreground processes exceeding 10 seconds are terminated.
- Input/output redirection and pipelines work correctly.

The implementation was compiled and tested locally. Final verification should be performed using the Codio **CHECK IT** autograder.

## 6. Limitations

Quash is a simplified educational shell and does not implement every feature of Bash.

Some advanced features, such as command history, job control commands (`fg`, `bg`), wildcard expansion, and command substitution, are not supported.

## 7. Conclusion

This project helped me understand the internal workings of a UNIX command shell and how operating systems manage processes.

By implementing Quash, I gained practical experience with `fork()`, `execvp()`, `waitpid()`, signals, file descriptors, redirection, and interprocess communication.

The final shell supports all required tasks and the optional I/O features while maintaining a modular implementation.
