# dipicam378

Smartcard simulator for integration tests. It uses the `cs378x` protocol, holds a device's RSA private key and 
answers ECM/EMM requests with a CW. Nothing more.

```
dipicam378 -k <keyfile> [options]
```

## Options

| flag | long form   | argument              | default             |
|------|-------------|-----------------------|---------------------|
| `-k` | `--key`     | `<path>`              | required            |
| `-s` | `--serial`  | `<id>`                | none = no filtering |
| `-p` | `--port`    | `<n>`                 | `27500`             |
| `-a` | `--auth`    | `[user:]<pass>`       | `dipicam378`        |
|      | `--caid`    | `<hex>`               | none = no filtering |
|      | `--algo`    | `cissa\|csa2`         | `cissa`             |
| `-v` | `--verbose` |                       |                     |
|      | `--color`   | `auto\|always\|never` | `auto`              |
| `-h` | `--help`    |                       |                     |

This tool is for debugging and validation only. A password is required because the
protocol's encryption relies on it. A username is optional, give one only if you
want this server to reject connections that don't send it.

## Parameters

### Device key (`-k`)

PEM-encoded RSA private key (`EK`) for a single device, as issued by the CAS on device creation.
Decryption of EMM-U/EMM-G/ECM all happens locally with this key.

### Device serial (`-s`)

Optional. Matched against EMM-U addressing. EMM-U for any other device on the
same carousel is skipped.

### Listen port (`-p`)

TCP port OSCam's `protocol = cs378x` reader dials into. Default `27500`.

### Auth (`-a`)

`[user:]password`. Password must match the reader config `password =` in
oscam.server, defaults to `dipicam378` if not given.

User, if given, must match the reader stanza's `user =` - a connection sending any other username
is rejected. Left unset: no username check, any connection accepted.

### CAID (`--caid`)

Optional. This device's own CAID. An ECM for any other CAID gets a CMD08 reply, so OSCam stops
re-requesting a CAID this device will never answer for. 

Left unset: no filtering, no CMD08.

### Algorithm (`--algo`)

`cissa` or `csa2` selects whether a decrypted ECM's control word is 16 or 8 bytes.
Nothing else.

### Live diagnostics (`-v`)

Connection accept/close and cs378x message types in/out are logged unconditionally.
`-v` adds finer detail like ECM/EMM request fields

## Example

```sh
dipicam378 -k device.key -s cardserial-01 -p 27500 -v -a someuser:somepassword
```
