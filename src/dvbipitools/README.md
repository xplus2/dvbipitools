# dvbipitools

Multicall binary: every tool in this project, bundled into one executable, busybox-style.

- So, you want static binaries, because ... broadcast.
- So, you're limited in space on end-user devices, because ... broadcast.

I hear you. A static version of OpenSSL per tool is heavy. 

This is the solution. 

```sh
dvbipitools <tool> [args...]
dipi <short-name> [args...]    # this tool, renamed to "dipi"
<toolname> [args...]           # symlink/hardlink to <toolname>
```

> Fairness note: as of now, it's as static as glibc can be.

## 4 ways to approach this

The following options are not mutually exclusive.

### 1. ignore it
It's redundant if you are using the standalone tools.

### 2. use it as a dispatcher
Only keep/use this one, no "setup", always call it like this:
```sh
dvbipitools dipirec -i udp://239.1.1.1:5000 -o out.ts
```

### 3. rename or link it to "dipi" and add a space to your existing scripts or Systemd units
```sh
ln -s dvbipitools dipi
./dipi rec -i udp://239.1.1.1:5000 -o out.ts
```
A sym/hardlink of `dvbipitools` to `dipi` does the same thing.

### 4. only copy/keep this one and symlink or hardlink all tools
For a copy&paste friendly setup:
```sh
ln -s dvbipitools dipitvhead
ln -s dvbipitools dipiradiohead
ln -s dvbipitools dipimetrics
ln -s dvbipitools dipirist
ln -s dvbipitools dipifccret
ln -s dvbipitools dipisds
ln -s dvbipitools dipibcg
ln -s dvbipitools dipixmltv
ln -s dvbipitools dipirec
ln -s dvbipitools dipiscan
ln -s dvbipitools dipibim
ln -s dvbipitools dipicam378
ln -s dvbipitools dipidescramble


./dipirec -i udp://239.1.1.1:5000 -o out.ts
```
Renaming would work too, but who wants to do that?


## Applets

| full name        | short form  |
|------------------|-------------|
| `dipibcg`        | `bcg`       |
| `dipibim`        | `bim`       |
| `dipicam378`     | `cam378`    |
| `dipidescramble` | `descramble`|
| `dipifccret`     | `fccret`    |
| `dipimetrics`    | `metrics`   |
| `dipiradiohead`  | `radiohead` |
| `dipirec`        | `rec`       |
| `dipirist`       | `rist`      |
| `dipiscan`       | `scan`      |
| `dipisds`        | `sds`       |
| `dipitvhead`     | `tvhead`    |
| `dipixmltv`      | `xmltv`     |

`dipicam378`/`dipidescramble` need OpenSSL, `dipirist` needs librist, same as the
standalone builds. Each is only in the table above if their dependencies were found.

Run `dvbipitools` or `dipi` with no arguments for the full applet list.

## Once dispatched

A tool's own options, exit status, and behavior are unchanged from running it standalone.
See that tool's own README/`-h`. 
`dvbipitools` itself adds no options of its own.

## Exit status

| code | meaning |
|------|---------|
| `0` | success, or bare invocation with no arguments (applet list printed) |
| `2` | unrecognized applet name or subcommand |
| other | whatever the dispatched tool itself returns |

