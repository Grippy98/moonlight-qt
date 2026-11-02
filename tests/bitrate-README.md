# Network-aware bitrate policy tests

From the repository root:

```sh
bash tests/run-bitrate-tests.sh
```

Requires a C++17 compiler, `make`, qmake, and the Qt Core/Network development
libraries. Qt 6 is selected when `qmake6` is available. The runner builds in a
temporary directory and removes its output on exit.

These tests call the same `BitratePolicy` helpers used by the client. They cover
selection by the socket's source address with both Wi-Fi and Ethernet up,
docking transitions, IPv4/IPv6 subnet metadata, down/missing interfaces, VPN
heuristics, toggle behavior, preservation of explicit bitrate presets, and the
locked/unlocked bounds for automatically calculated bitrates.

The tests use interface snapshots and require no network changes, server,
display, elevated privileges, or controller. They do not exercise the live TCP
probe, QSettings persistence, CLI parsing, or streaming codec negotiation.
Verify those separately in a client build and a streaming session.
