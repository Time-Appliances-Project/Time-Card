#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd -- "${script_dir}/.." && pwd)"
build_root="${BUILD_ROOT:-${source_dir}/build-timecard}"
prefix="${PREFIX:-${build_root}/prefix}"
build_type="${BUILD_TYPE:-Release}"
with_tests="${BUILD_TESTS:-ON}"
with_utils="${BUILD_UTILS:-ON}"
jobs="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '2')}"

ubloxcfg_repo="https://github.com/Orolia2s/ubloxcfg.git"
ubloxcfg_commit="499048ba8b3b9d0ca5d752c56f14c48117ebbb8e"
minipod_repo="https://github.com/Orolia2s/disciplining-minipod.git"
minipod_commit="2b17a3b7d2a945b2e7b8dc7dfa913bc9c9c3d25e"

for tool in cmake git pkg-config; do
    command -v "${tool}" >/dev/null || {
        printf 'Missing required tool: %s\n' "${tool}" >&2
        exit 2
    }
done

mkdir -p "${build_root}/src" "${prefix}"

checkout_pinned() {
    local repository="$1" destination="$2" commit="$3"
    if [[ ! -d "${destination}/.git" ]]; then
        git clone --filter=blob:none "${repository}" "${destination}"
    fi
    git -C "${destination}" fetch --depth 1 origin "${commit}"
    git -C "${destination}" checkout --detach "${commit}"
}

checkout_pinned "${ubloxcfg_repo}" "${build_root}/src/ubloxcfg" "${ubloxcfg_commit}"
cmake -S "${build_root}/src/ubloxcfg" -B "${build_root}/ubloxcfg" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_INSTALL_PREFIX="${prefix}"
cmake --build "${build_root}/ubloxcfg" --parallel "${jobs}"
cmake --install "${build_root}/ubloxcfg"

checkout_pinned "${minipod_repo}" "${build_root}/src/disciplining-minipod" "${minipod_commit}"
cmake -S "${build_root}/src/disciplining-minipod" -B "${build_root}/disciplining-minipod" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_INSTALL_PREFIX="${prefix}"
cmake --build "${build_root}/disciplining-minipod" --parallel "${jobs}"
cmake --install "${build_root}/disciplining-minipod"

mapfile -t pkg_paths < <(find "${prefix}" -type d -name pkgconfig -print | sort)
if [[ "${#pkg_paths[@]}" -eq 0 ]]; then
    printf 'No pkg-config metadata was installed under %s\n' "${prefix}" >&2
    exit 3
fi
export PKG_CONFIG_PATH="$(IFS=:; printf '%s' "${pkg_paths[*]}")${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"

cmake -S "${source_dir}" -B "${build_root}/oscillatord" \
    -DCMAKE_BUILD_TYPE="${build_type}" \
    -DCMAKE_INSTALL_PREFIX="${prefix}" \
    -DCMAKE_BUILD_RPATH="${prefix}/lib;${prefix}/lib64;${prefix}/lib/x86_64-linux-gnu" \
    -DBUILD_TESTS="${with_tests}" \
    -DBUILD_UTILS="${with_utils}"
cmake --build "${build_root}/oscillatord" --parallel "${jobs}"

if [[ "${with_tests}" == "ON" ]]; then
    ctest --test-dir "${build_root}/oscillatord" --output-on-failure
fi

cmake --install "${build_root}/oscillatord"
printf 'oscillatord and its pinned dependencies are installed under %s\n' "${prefix}"
