#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build/tests"

mkdir -p "${build_dir}"
cd "${build_dir}"
qmake6 "${project_dir}/tests/tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./lighttunnel_tests -v1
