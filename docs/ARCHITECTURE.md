# Architecture and security notes

## Components

```text
Qt GUI
  ├─ VLESS parser and profile repository
  ├─ independent sing-box and Xray configuration builders
  ├─ verified GitHub Releases updater for the selected core
  ├─ asynchronous, unprivileged endpoint latency monitor
  ├─ config validator (runs as the desktop user)
  └─ systemd core manager
       ├─ Polkit once → memory-only helper → systemd-run → one selected core
       └─ journalctl follower (desktop user)
```

The GUI and core communicate only through a generated configuration and the systemd service
state. A small privileged helper lives only as a child of the authenticated GUI and communicates
over inherited stdin/stdout pipes; it creates no socket and exits on EOF or parent death.

While connected, the GUI performs a SOCKS5 CONNECT through the selected core's loopback-only mixed
inbound and waits for an HTTP response from Cloudflare's public IPv4 endpoint every five seconds.
Waiting for application data is important because a SOCKS server may acknowledge CONNECT before
the remote path has completed. Unlike a normal socket that may follow a physical-route exception,
this probe cannot report success without a full VLESS round trip, so it includes both VPN and exit
latency. It is asynchronous, has a three-second timeout, never crosses the privilege boundary,
works when ICMP echo is blocked, and sends no profile credentials.

## Privilege boundary

TUN creation and route/nftables changes require `CAP_NET_ADMIN`. LightTunnel launches its fixed
helper through a dedicated Polkit action. The helper derives the caller UID from `PKEXEC_UID`,
rejects caller-supplied unit names and UIDs, verifies ownership and permissions of the core and
`0600` configuration, and constructs the `systemd-run` invocation itself. The selected core runs
as the calling desktop UID, while systemd grants only the network capabilities needed for TUN
operation. The unit is constrained with:

- `NoNewPrivileges=true`
- a small capability bounding set
- matching ambient network capabilities (`CAP_NET_ADMIN`, `CAP_NET_RAW`, `CAP_NET_BIND_SERVICE`)
- `ProtectSystem=strict`
- `ProtectHome=read-only`
- `PrivateTmp=true`
- `KillMode=mixed`

The first start or stop in each GUI session triggers the standard Polkit dialog. Later operations
reuse the already-authorized helper in RAM; neither the password nor an authorization token is
stored by LightTunnel. The generated configuration is validated before this boundary is crossed.

## Routing defaults

- TUN addresses: `172.19.0.1/30` and `fdfe:dcba:9876::1/126`
- stack: `system`
- MTU: `1500`
- both cores hijack plaintext DNS port 53; AAAA receives an immediate empty response and upstream
  A lookups use IPv4-only DNS (applications using their own DoH are still constrained by the IPv6
  reject rule)
- when IPv4-only is enabled (the default), IPv6 is captured and rejected before routing,
  preventing physical-interface leaks; disabling the visible setting restores dual-stack routing
- private IPv4 destinations use the direct outbound
- DNS answers and VLESS endpoint resolution are restricted to IPv4
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

The updater requests the latest stable sing-box release or the latest official Xray rolling
release from the respective upstream GitHub repository. Xray publishes current builds as GitHub
pre-releases; following that channel is required for native Linux TUN routing fixes newer than
26.3.27. The updater selects the exact Linux archive for x86_64 or arm64 and requires the
`sha256:` digest exposed by the GitHub Releases API. Responses and downloads have strict size and
timeout limits. Redirects may not downgrade HTTPS.

The archive hash is checked before extraction. Only the expected `sing-box` or `xray` executable is
extracted; path traversal and symlinks are rejected, and the resulting binary must report the
expected version. Installation uses an atomic write into separate user-owned `0700` directories.
An existing running tunnel keeps its current executable; a newly installed core is used after the
next connection.
