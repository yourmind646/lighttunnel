# Architecture and security notes

## Components

```text
Qt GUI
  ├─ VLESS parser and profile repository
  ├─ independent sing-box and Xray configuration builders
  ├─ verified GitHub Releases updater for the selected core
  ├─ config validator (runs as the desktop user)
  └─ systemd core manager
       ├─ Polkit → systemd-run → exactly one selected core (transient)
       └─ journalctl follower (desktop user)
```

The GUI and core communicate only through a generated configuration and the systemd service
state. There is no long-running custom privileged helper and no writable root-owned socket.

## Privilege boundary

TUN creation and route/nftables changes require `CAP_NET_ADMIN`. LightTunnel launches a transient
system unit through `pkexec systemd-run`. The selected core runs as the calling desktop UID, while systemd
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

- TUN address: `172.19.0.1/30`; Xray also uses `fdfe:dcba:9876::1/126`
- stack: `system`
- MTU: `1500`
- sing-box hijacks DNS port 53; Xray passes packets through its native TUN
- private IP destinations use the direct outbound
- all remaining traffic uses the selected VLESS outbound
- outbound traffic is bound to the selected physical interface to prevent a route loop
- QUIC is allowed unless the user explicitly blocks UDP/443

Xray uses its native Linux TUN inbound with `autoSystemRoutingTable` and
`autoOutboundsInterface`; sing-box uses `auto_route`, `auto_redirect`, and explicit outbound
binding. There is no local SOCKS bridge and no sing-box → Xray chain. When moving between Wi-Fi,
Ethernet or another VPN, leave the field on “Automatic” or select the physical interface.

Public destinations are never routed directly by client-side country rules. Xray sniffing restores
HTTP/TLS/QUIC domain destinations before the VLESS outbound, so inbound routing configured in 3x-ui
(for example, `.ru` direct and everything else through Germany) remains authoritative on the
server. Only private/reserved networks bypass the VLESS server locally.

## Secret handling

Profile JSON and runtime config are created with `0600`. Atomic `QSaveFile` writes prevent partial
configuration after a crash. URLs and UUIDs are never included in logs or error telemetry. The app
does not send analytics.

Future hardening work should move UUID/keys into the freedesktop Secret Service API with KWallet as
the KDE backend.

## Core updates

The updater requests only the latest stable release from the official SagerNet or XTLS GitHub
repository, according to the selected core. It selects the exact Linux archive for x86_64 or arm64
and requires the `sha256:` digest exposed by the GitHub Releases API. Responses and downloads have
strict size and timeout limits. Redirects may not downgrade HTTPS.

The archive hash is checked before extraction. Only the expected `sing-box` or `xray` executable is
extracted; path traversal and symlinks are rejected, and the resulting binary must report the
expected version. Installation uses an atomic write into separate user-owned `0700` directories.
An existing running tunnel keeps its current executable; a newly installed core is used after the
next connection.
