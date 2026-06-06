# PortFw

A small TCP port forwarder written in C++. It listens on a local address and
port, then forwards traffic in both directions to a remote host and port.

## Requirements

- Linux
- `g++` with C++11 support
- pthread

## Build

```bash
chmod +x build.sh
./build.sh
```

The executable is created with the name `main`. To choose a different name:

```bash
./build.sh portfw
```

## Usage

```bash
./main <local_address> <local_port> <remote_address> <remote_port>
```

Example: forward connections received on local port `8080` to
`192.168.1.10:80`.

```bash
./main 0.0.0.0 8080 192.168.1.10 80
```

The program keeps listening until it is stopped, for example with `Ctrl+C`.
