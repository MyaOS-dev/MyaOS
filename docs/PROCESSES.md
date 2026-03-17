# Processes

The scheduler now stores process metadata instead of only round-robin task callbacks.

## Tracked State

- PID
- PPID
- name
- current working directory
- background flag
- priority (`low` / `normal` / `high`)
- exit code
- run counters
- wake tick for sleeping processes
- optional CPU tick budget per process (enforced by scheduler)
- thread-group id for process/thread relationship

## States

- `running`
- `ready`
- `blocked`
- `sleeping`
- `zombie`

## Operations

- spawn kernel processes
- spawn ELF programs from the VFS into ring 3
- exec the current process image
- wait on child exit
- exit
- yield
- sleep until a timer deadline
- lightweight notify bits between processes (`notify`, `notify-poll`, `notify-wait` with timeout)
- one-slot message mailbox send/recv
- create user threads in current address space
- join user threads (`join` / non-blocking `join-poll`)
- named shared-memory segment read/write IPC
- weighted round-robin scheduling with priority budgets (`low=1`, `normal=2`, `high=4`)
- process resource limits (`cpu_limit_ticks`, `vm_limit_bytes`) via syscall API

## Current Limitation

- no POSIX-style signals; notifications are bitmask-based
- user threads are cooperative (no preemptive synchronization primitives yet)
