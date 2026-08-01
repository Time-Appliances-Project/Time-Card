#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    printf 'Run this installer as root (for example, sudo %s).\n' "$0" >&2
    exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "${script_dir}/.." && pwd)"
build_root="${BUILD_ROOT:-${source_dir}/build-timecard-system}"

PREFIX=/usr/local BUILD_ROOT="${build_root}" BUILD_TESTS=OFF BUILD_UTILS=ON \
    bash "${script_dir}/build-timecard.sh"

install -d -m 0755 /etc /var/lib/oscillatord
if [[ ! -e /etc/oscillatord.conf ]]; then
    install -m 0640 \
        "${source_dir}/example_configurations/oscillatord_timecard.conf" \
        /etc/oscillatord.conf
    printf 'Created /etc/oscillatord.conf; review it before starting the service.\n'
else
    printf 'Kept existing /etc/oscillatord.conf unchanged.\n'
fi

systemctl daemon-reload
printf 'Enable after reviewing configuration: systemctl enable --now oscillatord.service\n'
