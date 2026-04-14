# User Guide

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for common failure scenarios and recovery commands.

## Shell Basics

- `msh` starts the shell.
- Commands are resolved via `/cmd/*.cmd` manifests and `/bin/*.elf`.

## Baseline CLI Contract (v1.x)

The following entrypoints are platform-critical and must keep working across v1.x:

- `/bin/sh`
- `/bin/ls`
- `/bin/cat`
- `/bin/pkg`

Validation is part of `scripts/test_baseline.sh`.

## Filesystem Commands

- `list [PATH]` list directory
- `find [PATH] [PATTERN]` recursive path search
- `df` show mounts and disk sizes
- `du [PATH]` recursive size summary
- `show FILE` print file
- `save FILE TEXT` write file
- `newfile FILE` create file
- `mkfolder DIR` create directory
- `del PATH` remove file/dir
- `nano FILE` full-screen text editor (`Ctrl+O` save, `Ctrl+X` exit)
- `whereami` print current directory
- `attach diskN /mount` mount detected disk; `ntfs` supports session-local read/write
- `attached` list mounted filesystems (`volatile` marks mounts where runtime writes are available but persistence to source disk is not guaranteed)
- `pathinfo [PATH]` explain filesystem role; for concrete paths also show mount source and owning package when known

## Process and System Commands

- `tasks` list processes
- `sched` scheduler info
- `pause TICKS` sleep current process
- `start NAME /bin/prog.elf [ARGS...]` start background service
- `enable NAME [/bin/prog.elf]` enable service autostart profile
- `disable NAME` disable service autostart profile
- `status [NAME]` service status
- `stop [NAME]` stop service by name or power off machine (no args)
- `restart|poweroff` power control
- `whoami` show current uid
- `login root|user|guest` switch current process user
- `chmod MODE PATH` update mode bits (octal)
- `chown UID PATH` change owner (root only)
- `update [INDEX]` update installed packages from repository index (`/repo/index.pkg` by default)
- `modctl [load|unload NAME]` list/load/unload kernel modules (executes module hooks)
- `hotplug SIZE_MIB [BLOCK_SIZE]` add RAM-backed block disk at runtime
- `kill PID [EXIT_CODE]` terminate process by pid
- `sudo CMD [ARGS...]` run command with temporary root uid
- `uname` print kernel name/arch
- `compat CMD [ARGS...]` legacy shim (deprecated; use direct command or `lxrun`)
- `lxrun /path/prog|prog [ARGS...]` run Linux ELF64 (x86_64) in compatibility mode (expanded syscall subset); if `prog` has no `/`, `lxrun` first tries current directory, then `/boot/assets`, `/assets`, `/`, `/boot/bin`, `/bin`
- `man TOPIC` show manual page/topic help
- `cc <args...>` compiler frontend wrapper (`/bin/tcc.elf` if installed)
- `dbg <proc|tree|mem|net|log|sched> [args...]` debugging command dispatcher

## Package Management

- `pkg install /path/to/pkgfile.pkg` install local package (`MYAPKG1`)
- `pkg install http[s]://host/path.pkg` download and install package from network URL
- `pkg remove NAME` remove package files and metadata
- `pkg search PATTERN [INDEX]` search installed/repository packages
- `pkg list` list installed packages
- `pkg info NAME` show metadata for installed package
- `pkg avail [INDEX]` list repository entries
- `pkg install-from NAME [INDEX]` install package by name from repository index
- `pkg upgrade [INDEX]` upgrade installed packages from repository index
- `pkg downgrade NAME VERSION [INDEX]` install an older repository version
- `pkg dry-run <install|install-from|upgrade|downgrade|remove> ...` show plan without applying
- `pkg repo-init [INDEX]` create/initialize repository index
- `pkg repo-add INDEX NAME VERSION PACKAGE_PATH_OR_URL [ABI]` add/update repository entry
- `https://` downloads in `pkg/curl` work natively without proxy; optional HTTP forward proxy is available via `curl --proxy`

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
- `netstat` show network counters/devices (`nic_state`, PCI BDF and PCI ID)
- `ip` show configured source and gateway IPv4
- `route` show default route
- `wifi [status|diag|set <ssid> [psk]|show-config|clear-config|firmware-status|firmware-install <file>|firmware-remove|connect]` Wi-Fi profile + AX210 firmware helper
- `netsend <src_port> <dst_port> <text>` send loopback packet
- `netrecv <port> [timeout_spins]` receive loopback packet
- `curl [-i] [-o FILE] [--timeout-polls N] [--proxy host[:port]] <http[s]://host[:port][/path]>` HTTP/HTTPS GET client (native TLS for `https`, proxy optional)
- `chooseres` GRUB-like resolution picker UI (saves preferred GOP mode for next boot)
- `ssh [--insecure-hostkey] [-l USER] [-p PORT] [-pw PASSWORD] <USER@dst_ip|dst_ip> [port]` interactive SSHv2 client over TCP (password auth, shell channel)
- `fbinfo` show framebuffer values and available UEFI GOP modes
- `abi` print ABI version (`compile` and `/sys/abi_version`)
- `swapstat` show swap pool stats
- `swapcheck` explicit swap-out/swap-in self-test
- `dlcheck [LIB_PATH]` dynamic linker self-test on shared library (`/lib/liblinkdemo.so` by default, also supports `/lib/libdemo.so`)
- `posixcheck` show POSIX-compatible API values
- `cpuburn [SPINS]` CPU-bound workload for scheduler testing
- `rescpu [TICKS]` sample process CPU usage
- `log [--since T] [--until T] [--contains TEXT] [--service NAME] [--export FILE]` view logs with filters and export
- `edit FILE` open/edit/save text file (`.w` save, `.q` quit)
- `backup SRC OUT` create backup package (`MYABACK1`)
- `restore BACKUP_FILE [TARGET_PREFIX]` restore files from backup bundle
- `fsck [PATH]` recursive consistency/readability scan
- `schedcheck` compare CPU allocation between low/high-priority workers
- `limitcheck` validate process VM resource-limit enforcement
- `notifycheck` validate notification wait/poll behavior
- `seccheck` run negative syscall pointer-validation checks
- `syscfg [KEY]` read `/sys` config values (`/sys/log` includes ring-buffer log)
- `svcboot` start enabled services listed in `/etc/services.enabled` (normally called by `/autorun.sh`)
- `tree` show process tree
- `which [PATTERN]` search command manifests and ELF command binaries

## Unix Compatibility Aliases (Legacy)

- `ls`, `cat`, `pwd`, `mkdir`, `rm`, `touch`, `echo`
- `ps`, `id`, `sleep`, `sh`, `bash`, `reboot`, `halt`
