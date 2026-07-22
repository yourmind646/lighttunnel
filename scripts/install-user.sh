#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${project_dir}/build/lighttunnel"

if [[ ! -x "${binary}" ]]; then
    "${project_dir}/scripts/build.sh"
fi

install -Dm755 "${binary}" "${HOME}/.local/bin/lighttunnel"
install -Dm644 "${project_dir}/packaging/io.github.lighttunnel.LightTunnel.desktop" \
    "${HOME}/.local/share/applications/io.github.lighttunnel.LightTunnel.desktop"
install -Dm644 "${project_dir}/resources/icons/lighttunnel.svg" \
    "${HOME}/.local/share/icons/hicolor/scalable/apps/lighttunnel.svg"

command -v update-desktop-database >/dev/null && \
    update-desktop-database "${HOME}/.local/share/applications" || true

printf 'LightTunnel installed for %s.\n' "${USER}"
