# Domotics Project

Operating Systems Laboratory Project 2026-2.

## Requirements

- Ubuntu 24.04
- `gcc`
- `make`
- `bash`

## Build

```bash
make build
```

## Run

```bash
make run
```

Optional arguments can be passed with:

```bash
make run ARGS="..."
```

## Clean

```bash
make clean
```

This removes compiled files and restores the runtime environment, including IPC resources.

## Manual interaction

Open a second terminal and run:

```bash
./scripts/manual_interaction.sh <id> <command> [parameters]
```

This command must contact the target device directly, bypassing the controller.

## Tests

```bash
make test
```

## Interactive shell commands

The controller shell supports at least:

- `list`
- `add <device>`
- `del <id>`
- `link <id1> to <id2>`
- `switch <id> <label> <pos>`
- `info <id>`

## Project layout

- `include/`: public headers
- `src/`: C source files
- `scripts/`: helper scripts and demo scenarios
- `tests/`: Bash tests
- `runtime/`: FIFOs, registry, logs, and pid files
- `bin/`: generated executables
- `build/`: object files