# 🛡️ LightTunnel

LightTunnel is a small native VLESS VPN client for Linux. The interface is written in modern
C++20 and Qt 6; the network engine is the official `sing-box` executable installed on the
system. The project is independent and is not affiliated with the sing-box maintainers.

## ✨ Current feature set

- Import one or several `vless://` links, including plain or Base64 subscription URLs.
- VLESS over TCP, WebSocket, gRPC and HTTPUpgrade.
- TLS and REALITY, including uTLS fingerprint, Vision flow and XUDP.
- System-wide TUN with automatic routing and DNS interception.
- Safe explicit binding of both proxy and direct connections to the physical interface.
- KDE/GNOME tray, connect/disconnect actions and start minimized.
- Freedesktop autostart.
- Live `journalctl` logs inside the application.
- sing-box version display and verified automatic updates from official GitHub Releases.
- Configuration validation via `sing-box check` before requesting privileges.
- Profile and runtime files are written with mode `0600`.

LightTunnel deliberately does **not** run its GUI as root. It asks Polkit to create a hardened,
transient systemd service for the sing-box process. The service disappears after it stops.

## 📦 Requirements

- Linux with systemd and Polkit
- Qt 6 (`Core`, `Gui`, `Widgets`, `Network`, and `Test` for tests)
- qmake 6
- GCC or Clang with C++20 support
- `libarchive`/`bsdtar` for installing sing-box updates

An existing sing-box installation is optional. LightTunnel can use `/usr/bin/sing-box`, a core
shipped with v2rayN, or a manually selected executable. On a clean system it downloads the latest
stable official sing-box release on first launch, verifies the SHA-256 published by GitHub, checks
the reported version, and stores the executable in the private application data directory.

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
runs as root; Polkit asks for permission only when the protected TUN service is started. sing-box
does not need to be installed separately because LightTunnel downloads and verifies it on first
launch.

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

On a clean Arch/GNOME laptop this single command is sufficient; sing-box is obtained securely by
LightTunnel on first launch. GNOME may need the AppIndicator/KStatusNotifier extension for the tray
icon, while the main window works without it.

Run tests:

```bash
cd ~/Desktop/MyProjects/LightTunnel
./scripts/test.sh
```

Install only for the current user:

```bash
./scripts/install-user.sh
```

This installs the binary, desktop entry and icon below `~/.local`; it does not install or modify
sing-box.

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
2. Open **Settings** and verify the detected sing-box and network interface.
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

The generated sing-box configuration lives under the application data directory and is recreated
before a connection. Both files are owner-readable only. Secrets are never printed to the UI log.

Managed cores are stored below `~/.local/share/LightTunnel/LightTunnel/core` with mode `0700`.
Update checks run at most once per 24 hours unless started manually. Downloads use HTTPS, are size
limited, and are activated only after their GitHub Release SHA-256 and embedded version both match.

For a threat model and design details, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## ⚠️ Known limitations

- VLESS XHTTP is not enabled yet because its client schema is still evolving between cores.
- Subscription import is currently one-shot; periodic background refresh and QR scanning are planned.
- GNOME may require an AppIndicator/status notifier shell extension for full tray interaction.
- Profiles are protected by filesystem permissions, not by KWallet/Secret Service encryption yet.
- The application currently targets systemd-based distributions.

## 📄 Licensing

LightTunnel source code is MIT licensed. sing-box is a separate executable distributed under
GPL-3.0-or-later; consult its own repository and license when redistributing it.
