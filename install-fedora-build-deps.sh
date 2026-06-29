#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${repo_root}"

if [[ ! -r /etc/os-release ]]; then
  echo "Cannot detect Linux distribution: /etc/os-release is missing." >&2
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release

if [[ "${ID:-}" != "fedora" ]]; then
  echo "This helper is for Fedora. Detected: ${PRETTY_NAME:-unknown}." >&2
  echo "For other distros, use: ./get-dependencies_linux.sh <distro> build-dependencies" >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  sudo_cmd=()
else
  sudo_cmd=(sudo)
fi

packages=(
  gcc
  gcc-c++
  ninja-build
  nasm
  cmake
  git
  7zip
  gettext
  rubygem-asciidoctor
  SDL3-devel
  physfs-devel
  libpng-devel
  openal-soft-devel
  libvorbis-devel
  libogg-devel
  opus-devel
  libtheora-devel
  freetype-devel
  fribidi-devel
  harfbuzz-devel
  libcurl-devel
  libsodium-devel
  sqlite-devel
  protobuf-devel
  libzip-devel
  libzip-tools
  libjpeg-turbo-devel
  vulkan-devel
  glslc
)

echo "Installing Warzone 2100 Fedora build dependencies..."
"${sudo_cmd[@]}" dnf -y install "${packages[@]}"

echo "Initializing Warzone 2100 git submodules..."
git submodule update --init --recursive

echo "Checking pkg-config dependencies..."
pkg-config --modversion \
  sdl3 \
  physfs \
  libpng \
  openal \
  vorbis \
  ogg \
  opus \
  theora \
  freetype2 \
  fribidi \
  harfbuzz \
  libcurl \
  libsodium \
  sqlite3 \
  protobuf \
  libzip \
  libjpeg

echo
echo "Done. Next build command:"
echo "  cmake -S . -B /tmp/warzone2100-build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX:PATH=/tmp/warzone2100-install -GNinja"
echo "  cmake --build /tmp/warzone2100-build --target install"
