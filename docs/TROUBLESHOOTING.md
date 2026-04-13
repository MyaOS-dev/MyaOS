# Troubleshooting

This page lists practical checks for the most common MyaOS issues.
For a compact diagnostics playbook, also see `docs/DIAGNOSTICS.md`.

## Boot Stops Early

Symptoms:
- kernel panic (`PANIC vector=...`)
- no shell prompt after boot

Checks:
1. Boot with debug output enabled and inspect `/sys/log`:
   - `log --since 0`
2. In EFI boot menu press `P` to try previous kernel image (`/kernel.prev.elf`).
3. If loader prints `BOOT FAILURE`, follow suggested actions on screen.
4. Verify files on boot volume:
   - `list /boot`
   - `list /boot/bin`
   - `list /boot/cmd`
5. Validate ABI compatibility:
   - `abi`

## Command Not Found

Symptoms:
- `unknown command`
- manifest exists but program does not start

Checks:
1. Confirm manifest exists:
   - `list /cmd`
   - `show /cmd/<name>.cmd`
2. Confirm executable exists:
   - `list /bin`
3. Confirm execute permissions:
   - `chmod 755 /bin/<name>.elf`
4. Avoid deprecated migration shim:
   - `compat` is legacy; prefer direct command name or `lxrun`.

## Program Fails To Start

Symptoms:
- `failed to launch`
- immediate non-zero exit

Checks:
1. Run directly:
   - `load /bin/<prog>.elf`
2. Check logs:
   - `log --contains "<prog>"`
3. Verify file ownership and mode:
   - `chown <uid> /bin/<prog>.elf`
   - `chmod 755 /bin/<prog>.elf`

## Service Problems

Symptoms:
- service is not running after reboot
- `status` reports stopped

Checks:
1. Inspect service state:
   - `status [name]`
2. Inspect enabled profile:
   - `show /etc/services.enabled`
3. Enable/disable autostart:
   - `enable <name> [/bin/prog.elf]`
   - `disable <name>`
4. Start enabled services manually:
   - `svcboot`
5. Check service logs:
   - `log --service <name>`

## Network Not Working

Symptoms:
- no IP/gateway
- ping fails

Checks:
1. Show NIC/network state:
   - `netstat`
   - `ip`
   - `route`
2. Check link/driver info:
   - `devls`
3. Verify ICMP path:
   - `ping <ip>`
4. Inspect network logs:
   - `log --contains "net"`

## Filesystem Issues

Symptoms:
- cannot read/write directories
- mount points look missing

Checks:
1. List mounts:
   - `df`
2. Verify target path:
   - `whereami`
   - `list <path>`
3. Check disk visibility:
   - `disks`
4. Re-attach disk if required:
   - `attach diskN /mount`

## Performance Degradation

Symptoms:
- shell feels slow
- commands start slowly

Checks:
1. Scheduler/process view:
   - `tasks`
   - `sched`
2. CPU pressure:
   - `rescpu`
3. Memory/swap pressure:
   - `meminfo`
   - `swapstat`

## If Nothing Helps

Collect:
1. `log --since 0 --export /ram/diag.log`
2. `tasks`
3. `netstat`
4. `df`

Then attach these outputs to the bug report.
