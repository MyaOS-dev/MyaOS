# User Guide

## Shell Basics

- `msh` starts the shell.
- Commands are resolved via `/cmd/*.cmd` manifests and `/bin/*.elf`.

## Filesystem Commands

- `list [PATH]` list directory
- `show FILE` print file
- `save FILE TEXT` write file
- `newfile FILE` create file
- `mkfolder DIR` create directory
- `del PATH` remove file/dir
- `whereami` print current directory
- `attach diskN /mount` mount detected disk
- `attached` list mounted filesystems

## Process and System Commands

- `tasks` list processes
- `sched` scheduler info
- `pause TICKS` sleep current process
- `stop|restart|poweroff` power control
- `whoami` show current uid
- `login root|user|guest` switch current process user
- `update [INDEX]` update installed packages from repository index (`/repo/index.pkg` by default)
- `modctl [load|unload NAME]` list/load/unload kernel modules
- `hotplug SIZE_MIB [BLOCK_SIZE]` add RAM-backed block disk at runtime
- `kill PID [EXIT_CODE]` terminate process by pid
- `uname` print kernel name/arch
- `compat CMD [ARGS...]` run command through Unix compatibility mapper

## Package Management

- `pkg install /path/to/pkgfile.pkg` install local package (`MYAPKG1`)
- `pkg list` list installed packages
- `pkg info NAME` show metadata for installed package
- `pkg avail [INDEX]` list repository entries
- `pkg install-from NAME [INDEX]` install package by name from repository index
- `pkg upgrade [INDEX]` upgrade installed packages from repository index
- `pkg repo-init [INDEX]` create/initialize repository index
- `pkg repo-add INDEX NAME VERSION PACKAGE_PATH [ABI]` add/update repository entry

## IPC Commands

- `notify PID BITS`
- `notifypoll [MASK]`
- `notifywait [MASK] [TIMEOUT_TICKS]`
- `msgsend PID TEXT`
- `msgrecv`
- `pipecheck` basic in-process pipe syscall check
- `shmcheck` shared-memory IPC self-test
- `threadcheck` user-thread self-test

## Debug/Info Commands

- `meminfo` memory subsystem stats
- `disks` list block disks
- `devls` list registered devices
- `sockcheck` loopback socket self-test
- `netstat` show network counters/devices
- `netsend <src_port> <dst_port> <text>` send loopback packet
- `netrecv <port> [timeout_spins]` receive loopback packet
- `fbinfo` show framebuffer interface values
- `abi` print ABI version (`compile` and `/sys/abi_version`)
- `swapstat` show swap pool stats
- `swapcheck` explicit swap-out/swap-in self-test
- `dlcheck [LIB_PATH]` dynamic loader self-test on shared library (`/lib/libdemo.so`)
- `posixcheck` show POSIX-compatible API values
- `cpuburn [SPINS]` CPU-bound workload for scheduler testing
- `schedcheck` compare CPU allocation between low/high-priority workers
- `limitcheck` validate process VM resource-limit enforcement
- `notifycheck` validate notification wait/poll behavior
- `seccheck` run negative syscall pointer-validation checks
- `syscfg [KEY]` read `/sys` config values (`/sys/log` includes ring-buffer log)

## Unix Compatibility Aliases

- `ls`, `cat`, `pwd`, `mkdir`, `rm`, `touch`, `echo`
- `ps`, `id`, `sleep`, `sh`, `bash`, `reboot`, `halt`
