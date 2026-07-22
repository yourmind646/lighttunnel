#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build"

mkdir -p "${build_dir}"
cd "${build_dir}"
qmake6 "${project_dir}/LightTunnel.pro"
make -j"$(nproc)"

printf 'Built: %s\n' "${build_dir}/lighttunnel"
