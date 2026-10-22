# CSE231: Programming Assignment 3 — Processes, Threads, and Synchronization

**Name: Arpit Shukla , Yug Bansal**

**Roll Number: 2025105  , 2025579**

# Overview

This assignment explores three core operating-system concepts through C programs:

1. **Part A — Mini Shell:** A simple command-line shell that parses commands, runs external programs in child processes, and supports built-in `cd` and `exit` commands.
2. **Part B — Thread Counter:** A multithreaded program that demonstrates how race conditions affect a shared counter and how a mutex protects shared data.
3. **Part C — Print Server:** A producer-consumer system that uses a bounded job queue, a mutex, and condition variables to coordinate printer threads safely.

The project is written in C and built using the provided `Makefile`. Parts B and C use POSIX threads (`pthreads`).

# Project Structure

```text
pa3/
├── Makefile
├── answers.txt
├── A/
│   └── minishell.c
├── B/
│   └── thread_counter.c
└── C/
    ├── job_queue.h
    ├── job_queue.c
    └── print_server.c
```

- `Makefile` — Builds the three executables and removes generated binaries.
- `answers.txt` — Written answers to the conceptual questions for Parts A, B, C, and the bonus question.
- `A/minishell.c` — Implements the mini shell.
- `B/thread_counter.c` — Demonstrates unsafe and mutex-protected counter updates.
- `C/job_queue.h` — Defines the bounded queue, its synchronization objects, and its public functions.
- `C/job_queue.c` — Implements queue initialization, insertion, removal, shutdown, and cleanup.
- `C/print_server.c` — Runs printer worker threads that consume jobs from the queue.

# Prerequisites

A Linux environment with GCC, `make`, Python 3 (for the optional grader command), and POSIX threads is recommended. On Debian/Ubuntu, install the basic tools with:

```bash
sudo apt update
sudo apt install build-essential make python3
```

# Part A — Mini Shell

## Overview

The mini shell repeatedly displays the `msh>` prompt, reads a command, splits it into arguments, and either handles a built-in command or launches an external program. It demonstrates process creation, program execution, parent-child coordination, and working-directory management.

## Implementation

### Step 1 — Read and Parse the Command

The shell reads a line using `fgets()` and tokenizes it with `strtok()`. The resulting argument list is terminated with `NULL`, as required by `execvp()`.

The implementation defines limits for the input line and argument array:

```c
#define MAX_LINE 1024
#define MAX_ARGS 64
```

### Step 2 — Handle Built-in Commands

The shell handles `exit` and `cd` directly in the shell process.

- `exit` terminates the shell.
- `cd <directory>` calls `chdir()` to change the shell's working directory.

`cd` must run in the shell process itself. If it ran in a child process, only that child would change directories; the shell's working directory would remain unchanged.

### Step 3 — Create a Child Process

For an external command, `fork()` creates a child process.

- If `fork()` fails, the shell prints an error.
- In the child, `execvp()` replaces the child process image with the requested program.
- If `execvp()` fails, the child prints an error and exits using `_exit(127)`.
- In the parent, `waitpid()` waits for the child to finish.

The shell uses `waitpid()` so completed children are reaped rather than left as zombies. It also retries the wait when interrupted by a signal (`EINTR`).

## Supported Commands

```text
msh> pwd
msh> ls -l
msh> cd ..
msh> ls
msh> exit
```

The shell supports simple whitespace-separated commands and arguments. It does not implement shell features such as pipes, input/output redirection, quoting rules, wildcard expansion, or background execution.

## Build and Run

Build all assignment programs from the project root:

```bash
make
```

Run the shell:

```bash
./A/msh
```

Type `exit` or send EOF (Ctrl-D) to leave the shell.

# Part B — Thread Counter

## Overview

The thread counter creates multiple worker threads. Each worker increments the shared variable `total_jobs` for a specified number of iterations. The program supports two modes:

- **Unsafe mode:** Increments the shared counter without a mutex, intentionally demonstrating a data race.
- **Safe mode:** Locks a mutex around each increment to prevent concurrent updates from interfering with one another.

## Shared Counter

```c
static long total_jobs = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
```

The operation `total_jobs++` may look like a single operation in C, but conceptually it involves loading the current value, adding one, and storing the result. If two threads interleave these steps, one update can overwrite the other. This is called a **lost update**.

## Mutex Protection

In safe mode, each worker performs the increment inside a critical section:

```c
pthread_mutex_lock(&counter_mutex);
total_jobs++;
pthread_mutex_unlock(&counter_mutex);
```

Only one thread at a time can update the counter while holding the mutex. The program joins all created threads before printing the final result.

## Command-Line Usage

```bash
./B/thread_counter [unsafe|safe] [threads] [iterations]
```

If arguments are omitted, the defaults are:

```text
Mode:       unsafe
Threads:    4
Iterations: 100000 per thread
```

Examples:

```bash
./B/thread_counter unsafe
./B/thread_counter safe
./B/thread_counter unsafe 8 200000
./B/thread_counter safe 8 200000
```

The program accepts at most 64 threads and requires positive thread and iteration counts.

## Expected Result

The expected number of increments is:

```text
expected jobs = number of threads × iterations per thread
```

In safe mode, the actual count should equal the expected count. In unsafe mode, the actual count may be lower because of lost updates, but a particular run can still produce the expected value. A correct result in one unsafe run does not prove that the program is race-free.

# Part C — Producer-Consumer Print Server

## Overview

The print server models a system in which jobs are submitted to a bounded queue and several printer threads process them concurrently. It demonstrates producer-consumer synchronization, circular-buffer queue management, condition variables, and orderly shutdown.

The queue has a capacity of eight jobs, and the server creates four printer threads.

## Files

- `C/job_queue.h` — Declares `job_queue_t` and the queue functions.
- `C/job_queue.c` — Implements the thread-safe bounded queue.
- `C/print_server.c` — Creates printer workers, submits jobs, and coordinates shutdown.

## Queue Structure

The queue stores jobs in a fixed-size array and tracks its state with:

- `head` — Index of the next job to remove.
- `tail` — Index where the next job will be inserted.
- `count` — Number of jobs currently in the queue.
- `shutdown` — Indicates that no more jobs should be submitted.
- `mutex` — Protects the queue state and its invariants.
- `not_empty` — Signals that a consumer may be able to remove a job.
- `not_full` — Signals that a producer may be able to insert a job.

The queue uses circular indexing so the head and tail wrap around when they reach the end of the array.

## Queue Operations

### `queue_init()`

Initializes the queue state, mutex, and condition variables. If initialization fails partway through, already-initialized synchronization objects are cleaned up.

### `queue_push()`

1. Locks the queue mutex.
2. Waits while the queue is full and shutdown has not been requested.
3. Returns an error if shutdown has been requested.
4. Inserts the job and advances `tail` using circular indexing.
5. Increments `count`, signals `not_empty`, and unlocks the mutex.

### `queue_pop()`

1. Locks the queue mutex.
2. Waits while the queue is empty and shutdown has not been requested.
3. If shutdown is set and the queue is empty, unlocks and reports that no job remains.
4. Otherwise, removes the next job, advances `head`, decrements `count`, signals `not_full`, and unlocks the mutex.

The condition-variable waits use `while` loops because a thread must re-check the queue condition after waking. This handles spurious wakeups and cases where another thread changes the queue before the awakened thread reacquires the mutex.

### `queue_shutdown()`

Sets the shutdown flag while holding the mutex and broadcasts to both condition variables. Broadcasting wakes all waiting consumers and any producers blocked on a full queue, allowing them to observe shutdown and exit or return an error.

## Printer Threads

Each printer worker repeatedly calls `queue_pop()` to obtain a job. It simulates printing with a short `nanosleep()`, increments its own `jobs_completed` counter, and prints a message such as:

```text
PROCESSED job=1 printer=0
```

Each worker has its own completion counter, so workers do not need to contend on a shared counter for every completed job. The main thread can total the counters after joining the workers.

## Build and Run

Build the project:

```bash
make
```

Run the print server with the default of 50 jobs:

```bash
./C/print_server
```

Specify a positive number of jobs:

```bash
./C/print_server 100
```

The output includes submitted and processed job messages, followed by the server's summary.

## Shutdown Order

The required order for a producer-consumer system is important:

1. Finish submitting jobs and ensure all producer threads have terminated, if producers are running concurrently.
2. Call `queue_shutdown()` so waiting consumers wake up and learn that no more jobs will arrive.
3. Let consumers drain any jobs still in the queue and exit once the queue is empty.
4. Join all consumer threads.
5. Destroy the queue's condition variables and mutex.

Shutting down before all producers finish can cause remaining jobs to be rejected. In this implementation's core server, the main thread submits the jobs, calls shutdown after submission, and joins the printer threads.

# Build, Test, and Clean

## Build All Programs

From the directory containing the `Makefile`:

```bash
make
```

This creates:

- `A/msh`
- `B/thread_counter`
- `C/print_server`

The build uses GCC with C11, warnings enabled, warnings treated as errors, debug information, and the POSIX threads library where needed.

## Run the Optional Grader

The `Makefile` provides a `check` target that runs:

```bash
make check
```

This target expects a `grader.py` file in the project root. That file is **not present in the supplied ZIP**, so the target will not work unless the grader script is provided separately.

## Remove Generated Binaries

```bash
make clean
```

This removes the generated executables while preserving the source files and written answers.

# Important Operating-System Concepts

| Concept | Demonstrated in | Purpose |
|---|---|---|
| `fork()` | Part A | Creates a child process. |
| `execvp()` | Part A | Replaces the child process image with an external program. |
| `waitpid()` | Part A | Waits for and reaps a child process. |
| `chdir()` | Part A | Changes the shell process's working directory. |
| Race condition | Part B | Shows how unsynchronized shared updates can be lost. |
| Mutex | Parts B and C | Protects shared state inside critical sections. |
| Condition variable | Part C | Allows threads to sleep until queue state changes. |
| Bounded buffer | Part C | Limits how many jobs can wait in the queue. |
| Producer-consumer pattern | Part C | Coordinates job submission and processing. |
| `pthread_join()` | Parts B and C | Waits for worker threads to finish. |

# Overall Execution Flow

## Part A — Mini Shell

```text
Read command
    ↓
Parse command into arguments
    ↓
Built-in command? ── Yes ──> Run cd/exit in shell
    │
    No
    ↓
fork()
    ↓
Child: execvp()       Parent: waitpid()
    ↓                       ↓
Run external program   Reap child and show prompt again
```

## Part B — Thread Counter

```text
Start worker threads
    ↓
Each worker increments total_jobs
    ↓
Unsafe mode: no mutex / Safe mode: lock → increment → unlock
    ↓
Join all workers
    ↓
Compare expected count with actual count
```

## Part C — Print Server

```text
Submit jobs
    ↓
Bounded queue protected by mutex
    ↓
Printer workers wait on not_empty and pop jobs
    ↓
Workers process jobs and update local counters
    ↓
Request shutdown and wake waiting threads
    ↓
Join workers and summarize completed jobs
    ↓
Destroy queue synchronization objects
```

# Conclusion

This assignment demonstrates how operating systems manage processes, threads, and synchronization. Part A implements a small shell using `fork()`, `execvp()`, and `waitpid()`, while handling built-in commands in the correct process. Part B illustrates the nondeterministic effects of a data race and shows how a mutex protects a shared counter. Part C combines a bounded circular queue, a mutex, and condition variables to coordinate job submission and concurrent processing safely.

Together, the three parts provide practical experience with process control, thread coordination, critical sections, condition-variable waits, and safe shutdown of concurrent programs.

# AI Help Links

Add links to the AI conversations or other resources you used for this assignment, if required by your course's submission rules.
https://chatgpt.com/c/6ad9e549-9754-83ee-8c89-8e4087bd341a
