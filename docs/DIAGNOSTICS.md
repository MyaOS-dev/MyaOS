# Diagnostics

Last updated: 2026-04-10

This page focuses on practical error triage and repeatable diagnostics flow.

## Fast Triage Checklist

1. Confirm ABI level:
   - `abi`
2. Confirm command resolution:
   - `which <cmd>`
   - `show /cmd/<cmd>.cmd`
3. Confirm binary and permissions:
   - `list /bin`
   - `chmod 755 /bin/<prog>.elf`
4. Inspect recent logs:
   - `log --since 0`
   - `log --contains "<keyword>"`

## Subsystem Checks

- Process/scheduler:
  - `tasks`
  - `sched`
  - `rescpu`
- Memory/swap:
  - `meminfo`
  - `swapstat`
  - `swapcheck`
- Security and pointer validation:
  - `seccheck`
- Linux compatibility path:
  - `lxrun /boot/assets/busybox true`
  - `posixcheck`
  - `dlcheck`
- Network:
  - `netstat`
  - `ip`
  - `route`
  - `ping <ip>`

## Package/Repo Diagnostics

- Local package integrity and ABI:
  - `pkg info <name>`
  - `pkg avail [index]`
  - `pkg dry-run install-from <name> [index]`
- Upgrade plan:
  - `pkg dry-run upgrade [index]`

## Legacy Warning Surface

`compat` is deprecated and considered legacy migration-only. Prefer:

- native command names through `/cmd` manifests
- `lxrun` for Linux binaries

## Regression Entry Points

- `./scripts/test_smoke.sh`
- `./scripts/test_runtime.sh`
- `./scripts/test_regression.sh`
- `./scripts/test_stress.sh`
