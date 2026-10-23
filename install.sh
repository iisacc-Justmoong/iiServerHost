#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: INSTALL_PREFIX=... QT_PREFIX_PATH=... CMAKE_PREFIX_PATH=... ./install.sh" >&2
    exit 2
fi

source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$source_dir/build"
install_prefix="${INSTALL_PREFIX:-$HOME/.local/SDK/iiWebServer}"
compatibility="${IIWEBSERVER_BUILD_COMPATIBILITY:-ON}"
qt_prefix="${QT_PREFIX_PATH:-}"
if [[ -z "$qt_prefix" && -d "/Volumes/Storage/Qt/6.8.3/macos" ]]; then
    qt_prefix="/Volumes/Storage/Qt/6.8.3/macos"
fi
prefix_path="${CMAKE_PREFIX_PATH:-}"
network_prefix="${IINETWORK_PREFIX:-$source_dir/../iiNetwork/build/package}"
if [[ -d "$network_prefix/lib/cmake/iiNetwork" ]]; then
    prefix_path="$network_prefix${prefix_path:+;$prefix_path}"
fi
if [[ -n "$qt_prefix" ]]; then
    prefix_path="$qt_prefix${prefix_path:+;$prefix_path}"
fi

cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DIIWEBSERVER_BUILD_COMPATIBILITY="$compatibility" \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DCMAKE_PREFIX_PATH="$prefix_path"
cmake --build "$build_dir" --config Release --parallel
ctest --test-dir "$build_dir" -C Release --output-on-failure
cmake --install "$build_dir" --config Release

consumer_build_dir="$build_dir/http-consumer/build"
cmake -S "$source_dir/tests/http-consumer" -B "$consumer_build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$install_prefix${prefix_path:+;$prefix_path}" \
    -DiiWebServer_DIR="$install_prefix/lib/cmake/iiWebServer" \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiFileProvider=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiFileProviderNative=ON
cmake --build "$consumer_build_dir" --config Release --parallel
ctest --test-dir "$consumer_build_dir" -C Release --output-on-failure

case "$compatibility" in
    [Oo][Ff][Ff]|[Ff][Aa][Ll][Ss][Ee]|0|[Nn][Oo]) exit 0 ;;
esac
consumer_build_dir="$build_dir/consumer/build"
cmake -S "$source_dir/tests/consumer" -B "$consumer_build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$install_prefix${prefix_path:+;$prefix_path}" \
    -DiiWebServer_DIR="$install_prefix/lib/cmake/iiWebServer"
cmake --build "$consumer_build_dir" --config Release --parallel
ctest --test-dir "$consumer_build_dir" -C Release --output-on-failure
