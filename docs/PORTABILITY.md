# Portability

The kernel now supports architecture profiles via `ARCH` in Make:

- `ARCH=x86_64` (default)
- `ARCH=portable` (stub profile)

Example:

```bash
make ARCH=x86_64 -j4
```

`portable` keeps architecture-specific interfaces isolated and provides a second compile-time profile while low-level boot/runtime remains x86_64-oriented at this stage.
