# 🛡️ LightTunnel

LightTunnel is a small native VLESS VPN client for Linux. The interface is written in modern
C++20 and Qt 6. You can select either official `sing-box` or official `Xray-core` as the network
engine. Only the selected core runs; LightTunnel never chains the two backends. The project is
independent and is not affiliated with either upstream project.

## ✨ Current feature set

- Import one or several `vless://` links, including plain or Base64 subscription URLs.
- VLESS over TCP, WebSocket, gRPC and HTTPUpgrade with both cores; XHTTP with Xray.
- TLS and REALITY, including uTLS fingerprint, Vision flow and XUDP.
- System-wide TUN with automatic routing and DNS interception.
- Safe explicit binding of both proxy and direct connections to the physical interface.
- KDE/GNOME tray, connect/disconnect actions and start minimized.
- Live end-to-end TCP latency through the VPN tunnel, refreshed every five seconds.
- Freedesktop autostart.
- Live `journalctl` logs inside the application.
- sing-box/Xray version display and verified automatic updates from official GitHub Releases.
- Separate private managed directories for both cores; v2rayN is only a migration fallback.
- Configuration validation by the selected core before requesting privileges.
- One Polkit authorization per GUI session; an in-memory helper handles later connect/disconnect
  operations and disappears when LightTunnel exits. The password is never stored or seen by the GUI.
- Profile and runtime files are written with mode `0600`.

LightTunnel deliberately does **not** run its GUI as root. It asks Polkit once per application
session to start a narrowly scoped in-memory helper, which creates or stops a hardened transient
systemd service for the selected core. The helper accepts only validated LightTunnel operations
and exits with the GUI; the core service disappears after it stops.

## 📦 Requirements

- Linux with systemd and Polkit
- Qt 6 (`Core`, `Gui`, `Widgets`, `Network`, and `Test` for tests)
- qmake 6
- GCC or Clang with C++20 support
- `libarchive`/`bsdtar` for installing core updates

An existing core installation is optional. On a clean system LightTunnel downloads the latest
stable sing-box release or latest official Xray rolling release, verifies the SHA-256 published by
GitHub, checks the reported version, and stores it below its own private application data
directory. System executables and old v2rayN cores can be detected for migration, but an automatic
update moves normal operation to LightTunnel's own managed copy.

## 🛠️ Build and run

```bash
cd ~/Desktop/MyProjects/LightTunnel
./scripts/build.sh
./build/lighttunnel
```

### 🌀 Debian and Ubuntu

On a current Debian- or Ubuntu-based distribution with systemd, install the build and runtime
dependencies, then install LightTunnel for the current user:

```bash
sudo apt update
sudo apt install build-essential git libarchive-tools libgl1-mesa-dev \
    policykit-1 qt6-base-dev qt6-base-dev-tools

git clone https://github.com/yourmind646/lighttunnel.git
cd lighttunnel
./scripts/build.sh
./scripts/install-user.sh
```

Start **LightTunnel** from the application menu or run `~/.local/bin/lighttunnel`. The GUI never
runs as root; Polkit asks for permission only when the protected TUN service is started. Neither
core needs to be installed separately because LightTunnel downloads and verifies the selected one.

To remove the per-user installation while keeping profiles and settings:

```bash
cd lighttunnel
./scripts/uninstall.sh
```

On GNOME, install or enable an AppIndicator/KStatusNotifier extension if the tray icon is not
shown. The main window and VPN connection work without that extension.

### 🐧 Arch Linux and Manjaro package

Build and install a pacman-managed package:

```bash
cd ~/Desktop/MyProjects/LightTunnel/packaging/arch
makepkg -si
```

`makepkg` builds as the desktop user and asks for administrator privileges only when pacman needs
to install dependencies or the finished package. Installed files are tracked by pacman and work in
both KDE and GNOME sessions.

On a clean Arch/GNOME laptop this single command is sufficient; the selected core is obtained
securely by LightTunnel on first launch. GNOME may need the AppIndicator/KStatusNotifier extension
for the tray icon, while the main window works without it.

Run tests:

```bash
cd ~/Desktop/MyProjects/LightTunnel
./scripts/test.sh
```

Install only for the current user:

```bash
./scripts/install-user.sh
```

This installs the binary, desktop entry and icon below `~/.local`; cores remain user-managed by
LightTunnel.

## 🧹 Uninstall

Remove a package installed with `makepkg -si`:

```bash
sudo pacman -Rns lighttunnel
```

The repository also includes a wrapper that handles both pacman and the per-user installation:

```bash
./scripts/uninstall.sh
```

Profiles and settings are retained by default. To delete them as well:

```bash
./scripts/uninstall.sh --purge-user-data
```

## 🚀 First connection

1. Open **Profiles…** and paste a `vless://` link.
2. Open **Settings**, choose `sing-box` or `Xray`, and leave the output interface on Automatic
   unless you need to bind a specific physical interface.
3. Press **Connect** and approve the standard Polkit prompt.
4. Use the tray menu to hide, reconnect or quit.

QUIC is allowed by default. Enable “Block QUIC” only when an upstream does not support UDP; doing
so may add browser fallback latency.

Stop other full-device VPN/TUN clients before connecting. Stacking applications that both change
the default route is distribution-dependent and is intentionally not automated.

## 🔐 Data and security

Profiles contain credentials. They live in:

```text
~/.config/LightTunnel/LightTunnel/profiles.json
```

The generated core configuration lives under the application data directory and is recreated
before a connection. Both files are owner-readable only. Secrets are never printed to the UI log.

Managed cores are stored below `~/.local/share/LightTunnel/LightTunnel/cores/{sing-box,xray}` with
mode `0700`.
Update checks run at most once per 24 hours unless started manually. Downloads use HTTPS, are size
limited, and are activated only after their GitHub Release SHA-256 and embedded version both match.

For a threat model and design details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## ⚠️ Known limitations

- XHTTP profiles require selecting Xray; LightTunnel reports a clear error if one is started with
  sing-box.
- Native Xray TUN auto-routing requires Xray 26.5.9 or newer. LightTunnel follows the official
  Xray rolling channel because GitHub's “latest stable” endpoint remains on the broken 26.3.27.
- Both backends capture IPv6 locally and reject it inside the tunnel to prevent direct leaks. DNS
  answers and connections to the selected VLESS endpoint are forced to IPv4; proxied payload is
  therefore limited to IPv4 TCP/UDP.
- Subscription import is currently one-shot; periodic background refresh and QR scanning are planned.
- GNOME may require an AppIndicator/status notifier shell extension for full tray interaction.
- Profiles are protected by filesystem permissions, not by KWallet/Secret Service encryption yet.
- The application currently targets systemd-based distributions.

## 📄 Licensing

LightTunnel source code is MIT licensed. sing-box and Xray are separate upstream executables with
their own licenses; consult the respective upstream repository before redistributing either core.
