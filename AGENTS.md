# AGENTS.md

## Cursor Cloud specific instructions

This repo is the **QUIC / WebTransport Interop Test Runner** — a Python 3 orchestration
tool (entry point `run.py`) that spins up Docker containers (a network simulator plus a
server and client implementation), runs QUIC/WebTransport interop tests, captures pcaps,
and analyzes them with `tshark`. There is no long-running service; the "app" is a CLI run.

### Environment already provided by the VM snapshot / update script
- System packages (already installed in the snapshot): `python3.12-venv`, `docker-ce` +
  `docker-compose-plugin`, `fuse-overlayfs`, `iptables` (legacy), `tshark` 4.6.6 (from the
  `ppa:wireshark-dev/stable` PPA — the README requires ≥ 4.5.0; Ubuntu's default 4.2.2 is
  too old for correct QUIC dissection), `shellcheck`, `tcpdump`, `kmod`.
- The Python venv lives at `.venv/` (created by the update script). Activate with
  `. .venv/bin/activate` before running any Python command.

### Startup steps required on a fresh VM (NOT persisted in the snapshot)
Running processes and runtime kernel/sysctl state do not survive into a fresh VM, so do
these each session **before running interop tests**:

1. Start the Docker daemon (there is no systemd in this container):
   `sudo dockerd > /tmp/dockerd.log 2>&1 &` — then wait a few seconds and confirm with
   `docker info`. The daemon is configured (`/etc/docker/daemon.json`) to use the
   `fuse-overlayfs` storage driver with `containerd-snapshotter` disabled (required for
   Docker 29 + fuse-overlayfs in this nested environment). If you get a socket permission
   error, run `sudo chmod 666 /var/run/docker.sock` (the `ubuntu` user is already in the
   `docker` group, which persists in the snapshot).
2. **Critical for the interop tests to pass:** disable bridge netfilter so the network
   simulator actually receives client/server traffic:
   `sudo sysctl -w net.bridge.bridge-nf-call-iptables=0 net.bridge.bridge-nf-call-ip6tables=0`
   Without this, Docker's `FORWARD` chain (policy DROP) silently drops the QUIC UDP packets
   crossing the bridge between the client/server containers and the `sim` container. The
   symptom is a client that only logs `timeout: no recent network activity` and a red
   `✕(H)` handshake result while pcaps contain only ARP/ICMPv6/TCP and no QUIC UDP.

### Linting (mirrors the `check` GitHub Actions job in `.github/workflows/check.yml`)
Because the dev venv lives at `.venv/`, exclude it from `flake8` (CI has no venv):
- `flake8 --exclude=.venv .`
- `black --check --diff --exclude '/\.venv/' .`  (Black already ignores `.venv` by default)
- `python implementations.py -p quic` and `python implementations.py -p webtransport`
- `shellcheck certs.sh`

### Running the interop runner (see `README.md`, `quic.md`, `webtransport.md`)
Set `CRON=true` for non-interactive runs. Test images are pulled from Docker Hub on first
use. Examples that are verified to pass in this environment:
- QUIC: `CRON=true python run.py -s quic-go -c quic-go -t handshake`
- QUIC multi: `CRON=true python run.py -s quic-go -c quic-go -t handshake,transfer,retry,http3`
- WebTransport: `CRON=true python run.py -p webtransport -s webtransport-go -c webtransport-go -t handshake`

Notes:
- A result matrix cell is green `✓` (succeeded), grey `?` (unsupported), red `✕` (failed).
- Logs land in the `--log-dir` directory: `<server>_<client>/<testcase>/` with
  `output.txt`, `server/`, `client/`, and `sim/` (pcaps). `logs*/` and `*.json` are gitignored.
- Kernel modules cannot be loaded here (`modprobe ip6table_filter` fails), but the ip6
  filter table is built into the kernel, so IPv6 in the compose networks works anyway.
