# Operating Systems: Process Management, Multithreading, and Deadlocks

## 1. How an Operating System Manages Processes
A process is a program in execution. The operating system kernel manages processes through several core abstractions:
- **Process Control Block (PCB)**: A kernel data structure maintaining the process ID (PID), program counter, CPU registers, memory limits, open file descriptors, and scheduling priority.
- **Process States**: A process transitions through five primary lifecycle states:
  1. *New*: Process being created.
  2. *Ready*: In ready queue waiting for CPU allocation.
  3. *Running*: Instructions currently executing on a CPU core.
  4. *Waiting / Blocked*: Awaiting an I/O event or signal.
  5. *Terminated*: Execution finished; PCB reclaimed.
- **Context Switching**: The kernel saves the CPU register state of the running process into its PCB and restores the state of the next scheduled process.
- **CPU Scheduling**: Schedulers choose the next ready process using algorithms such as Round Robin (time quantum), Multi-Level Feedback Queues, Priority Scheduling, and Completely Fair Scheduler (CFS).

## 2. Multithreading and Safe Access to Shared Resources
Multithreading enables concurrent execution within a single address space. However, when multiple threads access shared mutable data without coordination, race conditions occur.

### Safe Concurrent Access:
- **Mutual Exclusion (Mutex)**: A binary lock that ensures only one thread executes a critical section at any given time.
- **Locks & Synchronization**: Explicit lock primitives (`acquire()` and `release()`) guarantee atomicity and memory visibility.
- **Semaphores**: Counting semaphores control access to a finite pool of resources.
- **Critical Section Invariant**: Code blocks modifying shared counters, linked lists, or database buffers must be protected to prevent corrupted state.

## 3. Deadlocks in Operating Systems
A deadlock occurs when a set of processes are permanently blocked because each process holds a resource and waits for another resource held by another process.

### The Four Coffman Conditions for Deadlock:
1. **Mutual Exclusion**: At least one resource must be held in a non-shareable mode.
2. **Hold and Wait**: A process holds at least one resource while waiting to acquire additional resources held by other processes.
3. **No Preemption**: Resources cannot be forcibly taken from a process; they must be released voluntarily.
4. **Circular Wait**: A closed chain of processes exists such that $P_0$ waits for a resource held by $P_1$, $P_1$ waits for $P_2$, ..., and $P_n$ waits for $P_0$.

### Deadlock Handling Strategies:
- **Deadlock Prevention**: Eliminate at least one of the four Coffman conditions (e.g. enforce global resource ordering to break circular wait).
- **Deadlock Avoidance**: The Banker's Algorithm dynamically inspects resource allocation state to ensure safe states.
- **Deadlock Detection and Recovery**: Construct a Resource Allocation Graph (RAG) and find cycles; terminate offending processes or preempt resources.
