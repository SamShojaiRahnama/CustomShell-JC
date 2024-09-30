# **Custom Shell with Job Control (CustomShell-JC)**

**CustomShell-JC** is a tiny shell program (`tsh`) written in C, which includes job control features. The shell supports both foreground and background processes, and handles various signals such as `SIGINT` (Ctrl-C) and `SIGTSTP` (Ctrl-Z). This project also includes several testing utilities and a reference implementation for comparison.

## **Project Overview**

The goal of this project is to implement a custom shell with job control and signal handling. It provides a simplified command-line interface that supports basic job management, along with a set of trace files and helper programs to test its behavior.

### **Features:**
- **Foreground and Background Job Control**: Users can run jobs in the foreground or send them to the background using commands like `fg` and `bg`.
- **Signal Handling**: The shell handles common signals such as `SIGINT` (Ctrl-C), `SIGTSTP` (Ctrl-Z), and `SIGCHLD` for child processes.
- **Builtin Commands**: Supports built-in commands such as `quit`, `jobs`, `bg`, and `fg`.
- **Testing Suite**: Includes trace files and a shell driver script to automate the testing of the shell's behavior.

## **Files Included**

### Core Files:
- **Makefile**: Compiles the shell program and runs the test cases.
- **README**: This file, explaining the project structure and usage.
- **tsh.c**: The shell program, which you will write and modify. This file includes the main functionality of the shell.
- **tshref**: A reference shell binary that provides a reference implementation of the shell functionality.

### Testing Files:
- **sdriver.pl**: A trace-driven shell driver that runs a series of automated tests on your shell.
- **trace\*.txt**: A set of 17 trace files that control the behavior of the shell driver and test different aspects of the shell’s functionality.
- **tshref.out**: Example output from the reference shell for all 17 trace files, which you can use to compare with your own shell’s output.

### Helper Programs:
These small C programs are used by the trace files to test specific behaviors of the shell:
- **myspin.c**: A program that takes an argument `<n>` and spins (i.e., uses CPU resources) for `<n>` seconds.
- **mysplit.c**: A program that forks a child process that spins for `<n>` seconds.
- **mystop.c**: A program that spins for `<n>` seconds and then sends a `SIGTSTP` signal to itself.
- **myint.c**: A program that spins for `<n>` seconds and then sends a `SIGINT` signal to itself.

## **How to Run**

### Prerequisites:
- A C compiler (e.g., `gcc`).
- A UNIX-based environment (Linux/macOS).
- The `make` utility.

## **License*
This project is open-source under the MIT License. See the LICENSE file for more information.
