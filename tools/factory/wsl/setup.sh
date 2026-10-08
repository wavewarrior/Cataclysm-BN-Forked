#!/usr/bin/env bash
# One-time setup of the WSL (Ubuntu 24.04) lint lane. Run from the repo root:
#   wsl -d Ubuntu -u root -e bash /mnt/c/WORK/GIT_REPOS/<checkout>/tools/factory/wsl/setup.sh
# Mirrors .github/workflows/clang-tidy.yml (LLVM 22, cata-* plugin) so a local gate result and the
# CI `tidy` job agree. Idempotent. Never builds under /mnt/c: the clone lives on ext4.
set -euo pipefail

LLVM_VERSION=22
LANE_HOME="${BN_TIDY_HOME:-$HOME/bn-tidy}"
SRC_REPO_WIN="${1:-/mnt/c/WORK/GIT_REPOS/Cataclysm-BN-Forked}"
SUDO=""
[ "$(id -u)" = 0 ] || SUDO="sudo"

export DEBIAN_FRONTEND=noninteractive
$SUDO apt-get update
$SUDO apt-get install -y wget gnupg lsb-release ca-certificates software-properties-common

# apt.llvm.org: clang/clang-tidy/clang-format/llvm-dev 22.
codename="$(lsb_release -cs)"
if [ ! -f /etc/apt/sources.list.d/llvm-$LLVM_VERSION.list ]; then
    wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | $SUDO tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
    echo "deb http://apt.llvm.org/$codename/ llvm-toolchain-$codename-$LLVM_VERSION main" |
        $SUDO tee /etc/apt/sources.list.d/llvm-$LLVM_VERSION.list >/dev/null
    $SUDO apt-get update
fi

# Package lists copied from .github/workflows/clang-tidy.yml (install dependencies, SDL3-devel).
$SUDO apt-get install -y \
    "clang-$LLVM_VERSION" "clang-tidy-$LLVM_VERSION" "clang-format-$LLVM_VERSION" \
    "llvm-$LLVM_VERSION-dev" "libclang-$LLVM_VERSION-dev" "libclang-rt-$LLVM_VERSION-dev" \
    gettext mold jq ccache ninja-build cmake astyle git curl zip unzip tar \
    sqlite3 libsqlite3-dev zlib1g-dev libvulkan-dev mesa-vulkan-drivers xvfb \
    autoconf autoconf-archive automake libtool libltdl-dev pkg-config python3-pip \
    libasound2-dev libpulse-dev libaudio-dev libjack-dev libsndio-dev libx11-dev \
    libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
    libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev libdbus-1-dev \
    libibus-1.0-dev libudev-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev \
    libfreetype-dev libharfbuzz-dev \
    libtiff-dev libavif-dev libwebp-dev libjpeg-dev libjxl-dev \
    libflac-dev libvorbis-dev libxmp-dev libmpg123-dev libwavpack-dev

# build-clang-tidy-plugin.sh hardcodes /usr/bin/clang and calls an unversioned llvm-config.
for tool in clang clang++ clang-tidy clang-format llvm-config; do
    $SUDO update-alternatives --install "/usr/bin/$tool" "$tool" "/usr/bin/$tool-$LLVM_VERSION" 100
    $SUDO update-alternatives --set "$tool" "/usr/bin/$tool-$LLVM_VERSION"
done

mkdir -p "$LANE_HOME"
git config --global --add safe.directory '*'
if [ ! -d "$LANE_HOME/src/.git" ]; then
    git clone "$SRC_REPO_WIN" "$LANE_HOME/src"
fi

# shadercross (vcpkg) is required by the game's CMake configure.
cd "$LANE_HOME/src"
shader_out="$(bash build-scripts/install-shadercross-vcpkg.sh "$LANE_HOME/vcpkg-shadercross" | tee /dev/stderr | grep '^shadercross=' | tail -n1)"
shadercross_exe="${shader_out#shadercross=}"
cat >"$LANE_HOME/env.sh" <<EOF
export SHADERCROSS="$shadercross_exe"
export PATH="$(dirname "$shadercross_exe"):\$HOME/.local/bin:\$PATH"
EOF

# lit 23 rejects the plugin's lit.cfg (execute_external); 18.x runs it. FileCheck is for check_clang_tidy.py.
pip install --break-system-packages 'lit==18.1.8' || true
$SUDO ln -sf "/usr/lib/llvm-$LLVM_VERSION/bin/FileCheck" /usr/local/bin/FileCheck

echo "--- versions ---"
clang-tidy --version | head -n 2
clang-format --version
astyle --version || true
llvm-config --version
tidy_major="$(clang-tidy --version | sed -n 's/.*version \([0-9]*\)\..*/\1/p' | head -n1)"
format_major="$(clang-format --version | sed -n 's/.*version \([0-9]*\)\..*/\1/p' | head -n1)"
if [ "$tidy_major" != "$LLVM_VERSION" ] || [ "$format_major" != "$LLVM_VERSION" ]; then
    echo "setup.sh: clang-tidy ($tidy_major) or clang-format ($format_major) is not $LLVM_VERSION" >&2
    exit 1
fi
echo "setup.sh: ok ($LANE_HOME)"
