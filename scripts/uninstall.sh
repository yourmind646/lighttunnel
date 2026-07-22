#!/usr/bin/env bash
set -euo pipefail

purge_user_data=false

usage() {
    cat <<'EOF'
Usage: ./scripts/uninstall.sh [--purge-user-data]

Removes the pacman package, or files installed by install-user.sh.
Profiles and settings are kept unless --purge-user-data is specified.
EOF
}

case "${1:-}" in
    "") ;;
    --purge-user-data) purge_user_data=true ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

if command -v pacman >/dev/null && pacman -Qq lighttunnel >/dev/null 2>&1; then
    sudo pacman -Rns -- lighttunnel
else
    user_bin="${HOME}/.local/bin/lighttunnel"
    user_desktop="${HOME}/.local/share/applications/io.github.lighttunnel.LightTunnel.desktop"
    user_icon="${HOME}/.local/share/icons/hicolor/scalable/apps/lighttunnel.svg"

    rm -f -- "${user_bin}" "${user_desktop}" "${user_icon}"
    command -v update-desktop-database >/dev/null && \
        update-desktop-database "${HOME}/.local/share/applications" || true
    printf 'Removed the per-user LightTunnel installation.\n'
fi

autostart_file="${XDG_CONFIG_HOME:-${HOME}/.config}/autostart/io.github.lighttunnel.LightTunnel.desktop"
rm -f -- "${autostart_file}"

config_dir="${XDG_CONFIG_HOME:-${HOME}/.config}/LightTunnel"
data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/LightTunnel"

if [[ "${purge_user_data}" == true ]]; then
    for path in "${config_dir}" "${data_dir}"; do
        if [[ -n "${path}" && "${path}" != / && "${path}" == */LightTunnel ]]; then
            rm -rf -- "${path}"
        else
            printf 'Refusing to remove unsafe path: %s\n' "${path}" >&2
            exit 1
        fi
    done
    printf 'Removed LightTunnel profiles, settings and runtime data.\n'
else
    printf 'User data was kept in:\n  %s\n  %s\n' "${config_dir}" "${data_dir}"
fi
