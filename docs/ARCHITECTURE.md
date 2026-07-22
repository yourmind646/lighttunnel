# Architecture and security notes

## Components

```text
Qt GUI
  ├─ VLESS parser and profile repository
  ├─ sing-box configuration builder
  ├─ verified GitHub Releases core updater
  ├─ config validator (`sing-box check` as the desktop user)
  └─ systemd core manager
       ├─ Polkit → systemd-run → sing-box (root, transient)
       └─ journalctl follower (desktop user)
```

The GUI and core communicate only through a generated configuration and the systemd service
state. There is no long-running custom privileged helper and no writable root-owned socket.

## Privilege boundary

TUN creation and route/nftables changes require `CAP_NET_ADMIN`. LightTunnel launches a transient
system unit through `pkexec systemd-run`. sing-box runs as the calling desktop UID, while systemd
grants only the network capabilities needed for TUN operation. The unit is constrained with:

- `NoNewPrivileges=true`
- a small capability bounding set
- matching ambient network capabilities (`CAP_NET_ADMIN`, `CAP_NET_RAW`, `CAP_NET_BIND_SERVICE`)
- `ProtectSystem=strict`
- `ProtectHome=read-only`
- `PrivateTmp=true`
- `KillMode=mixed`

Stopping the tunnel also goes through Polkit. The generated configuration is validated before this
boundary is crossed.

## Routing defaults

- TUN address: `172.19.0.1/30`
- stack: `system`
- MTU: `1500`
- DNS port 53 is hijacked before protocol sniffing
- private IP destinations use the direct outbound
- all remaining traffic uses the selected VLESS outbound
- both `proxy` and `direct` have an explicit `bind_interface`
- QUIC is allowed unless the user explicitly blocks UDP/443

Explicit interface binding is intentional: it prevents sing-box's own direct/DNS connections from
being routed back into the TUN. When moving between Wi-Fi, Ethernet or another VPN, leave the field
on “Automatic” or select the new physical interface.

## Secret handling

Profile JSON and runtime config are created with `0600`. Atomic `QSaveFile` writes prevent partial
configuration after a crash. URLs and UUIDs are never included in logs or error telemetry. The app
does not send analytics.

Future hardening work should move UUID/keys into the freedesktop Secret Service API with KWallet as
the KDE backend.

## Core updates

The updater requests only the latest stable release from the official SagerNet GitHub repository.
It selects the exact `linux-amd64` or `linux-arm64` archive and requires the `sha256:` digest exposed
by the GitHub Releases API. Responses and downloads have strict size and timeout limits. Redirects
may not downgrade HTTPS.

The archive hash is checked before extraction. Only one validated `*/sing-box` member is extracted,
path traversal and symlinks are rejected, and the resulting binary must report the expected version
through `sing-box version`. Installation uses an atomic write into a user-owned `0700` directory.
An existing running tunnel keeps its current executable; a newly installed core is used after the
next connection.
