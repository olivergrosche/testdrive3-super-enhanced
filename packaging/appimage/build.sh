#!/bin/bash
# Builds the AppImage. Runs as root inside an ubuntu:20.04 container (glibc 2.31, so the image runs on every distro of
# the last years); packaging/appimage/README.md has the podman line.
#
# /work/src       this repository
# /work/install   optional: Game/ (original game files: a PRIVATE image, never publish it), settings.ini (the
#                 launcher's) and testdrive3-enhanced.args. Empty or missing = the public image without game files.
# /work/out       the AppImage is written here
# /work/cache     downloads and the built libraries, kept between runs
#
# SDL 3 and wxWidgets are linked statically. What they need from the system they load at run time (SDL: X11, Wayland,
# audio, GL, Vulkan) or link dynamically (launcher: GTK 3); these are not bundled, they must match the host's driver
# stack and desktop anyway. Nothing but glibc is needed by the game itself.
set -euo pipefail

SDL_VER=3.4.10      # the same SDL as the Windows build
WX_VER=3.2.8
LIBDECOR_VER=0.2.3
VERSION=${VERSION:-0.2.0-og10}
NAME=TestDrive3-Super-Enhanced-$VERSION-x86_64.AppImage

SRC=/work/src
INSTALL=/work/install
OUT=/work/out
CACHE=/work/cache
PREFIX=$CACHE/prefix
PKG=$SRC/packaging/appimage
JOBS=$(nproc)
mkdir -p "$OUT" "$CACHE/dl" "$PREFIX"
export PATH=$PREFIX/bin:/usr/local/bin:$PATH
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PREFIX/lib/x86_64-linux-gnu/pkgconfig

get() { [ -s "$CACHE/dl/$2" ] || curl -fsSL --retry 3 -o "$CACHE/dl/$2" "$1"; }

echo "=== packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends \
    ca-certificates curl xz-utils build-essential pkg-config python3-pip ninja-build squashfs-tools file binutils \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev \
    libudev-dev libdbus-1-dev libasound2-dev libpulse-dev libgtk-3-dev libcairo2-dev libpango1.0-dev >/dev/null
pip3 install -q 'cmake>=3.24,<4' meson

# Only so that SDL can use the host's libdecor (window title bars on GNOME/Wayland): it is loaded at run time, if there.
if [ ! -e "$CACHE/libdecor.done" ]; then
    echo "=== libdecor $LIBDECOR_VER"
    get "https://gitlab.freedesktop.org/libdecor/libdecor/-/archive/$LIBDECOR_VER/libdecor-$LIBDECOR_VER.tar.gz" libdecor.tar.gz
    rm -rf /tmp/libdecor && mkdir /tmp/libdecor
    tar -xf "$CACHE/dl/libdecor.tar.gz" -C /tmp/libdecor --strip-components=1
    meson setup /tmp/libdecor/build /tmp/libdecor --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddemo=false -Ddbus=disabled
    ninja -C /tmp/libdecor/build install
    touch "$CACHE/libdecor.done"
fi

if [ ! -e "$CACHE/sdl.done" ]; then
    echo "=== SDL $SDL_VER"
    get "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL3-$SDL_VER.tar.gz" sdl.tar.gz
    rm -rf /tmp/sdl && mkdir /tmp/sdl
    tar -xf "$CACHE/dl/sdl.tar.gz" -C /tmp/sdl --strip-components=1
    cmake -S /tmp/sdl -B /tmp/sdl/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_DEPS_SHARED=ON -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_WAYLAND_LIBDECOR=ON -DSDL_KMSDRM=OFF \
        -DSDL_ALSA=ON -DSDL_PULSEAUDIO=ON -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_CAMERA=OFF -DSDL_LIBUDEV=ON -DSDL_DBUS=ON \
        -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_VULKAN=ON
    cmake --build /tmp/sdl/build -j "$JOBS"
    cmake --install /tmp/sdl/build
    touch "$CACHE/sdl.done"
fi

if [ ! -e "$CACHE/wx.done" ]; then
    echo "=== wxWidgets $WX_VER"
    get "https://github.com/wxWidgets/wxWidgets/releases/download/v$WX_VER/wxWidgets-$WX_VER.tar.bz2" wx.tar.bz2
    rm -rf /tmp/wx && mkdir /tmp/wx
    tar -xf "$CACHE/dl/wx.tar.bz2" -C /tmp/wx --strip-components=1
    (cd /tmp/wx && ./configure --prefix="$PREFIX" --disable-shared --with-gtk=3 --enable-unicode \
        --with-libpng=builtin --with-libjpeg=builtin --with-zlib=builtin --with-regex=builtin --with-expat=builtin \
        --without-libtiff --without-liblzma --without-opengl --without-libnotify --without-sdl --without-libmspack \
        --without-gtkprint --disable-webview --disable-mediactrl --disable-webrequest --disable-sound \
        --disable-uiactionsim --disable-secretstore --disable-detect_sm --disable-stc --disable-ribbon \
        --disable-richtext --disable-propgrid --disable-aui --disable-html --disable-xrc --disable-sys-libs >/dev/null)
    make -C /tmp/wx -j "$JOBS" >/dev/null
    make -C /tmp/wx install >/dev/null
    touch "$CACHE/wx.done"
fi

echo "=== game and launcher"
rm -rf /tmp/td3
cmake -S "$SRC" -B /tmp/td3 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" -DTD3E_LAUNCHER=ON \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++ -Wl,--as-needed"
cmake --build /tmp/td3 -j "$JOBS"

echo "=== image"
APP=/tmp/AppDir
rm -rf "$APP"
DOC=$APP/usr/share/doc/testdrive3-super-enhanced
SHARE=$APP/usr/share/testdrive3-super-enhanced
mkdir -p "$APP/usr/bin" "$DOC" "$SHARE"

install -m755 /tmp/td3/testdrive3-enhanced "/tmp/td3/Test Drive III Enhanced" "$APP/usr/bin/"
strip "$APP/usr/bin/"*

sed 's/\r$//' "$PKG/AppRun" > "$APP/AppRun"
chmod 755 "$APP/AppRun"
sed 's/\r$//' "$PKG/testdrive3-super-enhanced.desktop" > "$APP/testdrive3-super-enhanced.desktop"
install -m644 "$PKG/testdrive3-super-enhanced.png" "$APP/"
ln -s testdrive3-super-enhanced.png "$APP/.DirIcon"

if [ -d "$INSTALL/Game" ]; then
    cp -r "$INSTALL/Game" "$SHARE/Game"
    echo "NOTE: this image holds game files: private, never publish it"
fi
# The launcher's settings of this installation, its folders left to the launcher; without them, its defaults.
if [ -e "$INSTALL/settings.ini" ]; then
    sed -e 's/\r$//' -e 's|^GameFolder=.*|GameFolder=@GAMEDIR@|' -e 's|^Program=.*|Program=|' -e '/^\[Windows\]/,$d' \
        "$INSTALL/settings.ini" > "$SHARE/settings.ini.in"
else
    printf '[Game]\nGameFolder=@GAMEDIR@\n' > "$SHARE/settings.ini.in"
fi
# The same settings for --play and for a system without GTK 3.
if [ -e "$INSTALL/testdrive3-enhanced.args" ]; then
    sed 's/\r$//' "$INSTALL/testdrive3-enhanced.args" > "$SHARE/testdrive3-enhanced.args"
else
    echo "--fullscreen" > "$SHARE/testdrive3-enhanced.args"
fi

for f in "$SRC"/*.md "$SRC/LICENSE" "$PKG/README-LINUX.md" "$PKG/THIRD-PARTY-LINUX.txt"; do
    cp "$f" "$DOC/"
done
chmod -R u=rwX,go=rX "$APP"

get https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 runtime-x86_64
rm -f /tmp/app.squashfs
mksquashfs "$APP" /tmp/app.squashfs -root-owned -noappend -comp zstd -Xcompression-level 19 -b 256K -no-progress >/dev/null
cat "$CACHE/dl/runtime-x86_64" /tmp/app.squashfs > "$OUT/$NAME"
chmod 755 "$OUT/$NAME"

echo "=== result"
for b in "$APP/usr/bin/"*; do
    echo "--- $b"
    file -b "$b"
    readelf -d "$b" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/  needs \1/p'
    echo "  newest glibc symbol: $(objdump -T "$b" | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)"
done
if [ -d "$SHARE/Game" ]; then
    "$APP/usr/bin/testdrive3-enhanced" --game-dir "$SHARE/Game" --check
else
    echo "public image: no game files inside"
    [ -z "$(find "$APP" -iname 'tdiii.exe' -o -iname 'td3.exe' -o -iname 'playdisk.dat')" ] || { echo "game files found in a public image"; exit 1; }
fi
sha256sum "$CACHE/dl/runtime-x86_64" "$OUT/$NAME"
ls -la "$OUT"
