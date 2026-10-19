#!/usr/bin/env bash
# Build the whole Android port from a clean checkout: DXVK (d3d8/d3d9 -> Vulkan), the engine
# (libmain.so + SDL3/SDL3_image/OpenAL/GameSpy) and the jniLibs folder Gradle packages.
# Used by .github/workflows/android-ci.yml; also runs on any Linux x86_64 box with the tools below.
#
# Needs: ANDROID_NDK_HOME (NDK 27.2.12479018), VCPKG_ROOT (bootstrapped vcpkg at the engine's
#        builtin-baseline), cmake >= 3.25, ninja, meson, glslangValidator, pkg-config, git, curl.
#
# Usage: scripts/ci/build-android.sh [dxvk|engine|stage|all]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${OUT_DIR:-$ROOT/build/android-ci}"
JOBS="${JOBS:-$(nproc)}"
API=28
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to NDK 27.2.12479018}"
TC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"

# shellcheck source=versions.env
source "$ROOT/scripts/ci/versions.env"

DXVK_OUT="$OUT/dxvk"            # libdxvk_d3d8.so + libdxvk_d3d9.so end up here
ENGINE_BUILD="$OUT/engine"
JNILIBS="$ROOT/android/app/libs/arm64-v8a"

build_dxvk() {
  if [[ -f "$DXVK_OUT/libdxvk_d3d8.so" && -f "$DXVK_OUT/libdxvk_d3d9.so" ]]; then
    echo "DXVK already built in $DXVK_OUT"; return
  fi
  local src="$OUT/dxvk-src" sdl="$OUT/SDL3-$SDL3_VERSION" pc="$OUT/dxvk-pkgconfig"
  rm -rf "$src" && mkdir -p "$src" "$pc" "$DXVK_OUT"
  git -C "$src" init -q
  git -C "$src" fetch -q --depth 1 "$DXVK_REPO" "$DXVK_COMMIT"
  git -C "$src" checkout -q FETCH_HEAD
  # libdisplay-info lives on gitlab.freedesktop.org, which has outages; retry with backoff.
  local attempt
  for attempt in 1 2 3 4 5; do
    echo "$(date -u +%T) fetching DXVK submodules (attempt $attempt)"
    git -C "$src" submodule update --init --recursive && break
    (( attempt < 5 )) || { echo "DXVK submodules could not be fetched" >&2; exit 1; }
    sleep $(( 30 * attempt ))
  done
  git -C "$src" apply "$ROOT/scripts/ci/dxvk-android.patch"

  if [[ ! -d "$sdl/include" ]]; then
    curl -fsSL -o "$OUT/sdl3.tar.gz" \
      "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz"
    echo "$SDL3_SHA256  $OUT/sdl3.tar.gz" | sha256sum -c -
    tar -xzf "$OUT/sdl3.tar.gz" -C "$OUT"
  fi
  # meson looks the dependency up as 'SDL3', and pkg-config names are case-sensitive.
  cat > "$pc/SDL3.pc" <<EOF
Name: sdl3
Description: SDL3 headers for DXVK's SDL3 WSI (library is loaded at runtime)
Version: $SDL3_VERSION
Cflags: -I$sdl/include
Libs:
EOF

  cat > "$OUT/meson-android-arm64.txt" <<EOF
[binaries]
c = '$TC/bin/aarch64-linux-android$API-clang'
cpp = '$TC/bin/aarch64-linux-android$API-clang++'
ar = '$TC/bin/llvm-ar'
strip = '$TC/bin/llvm-strip'
pkg-config = 'pkg-config'

[properties]
pkg_config_libdir = ['$pc']

[built-in options]
c_args = ['-DVK_ENABLE_BETA_EXTENSIONS', '-DVK_USE_PLATFORM_ANDROID_KHR']
cpp_args = ['-DVK_ENABLE_BETA_EXTENSIONS', '-DVK_USE_PLATFORM_ANDROID_KHR']

[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
EOF

  meson setup "$OUT/dxvk-build" "$src" --cross-file "$OUT/meson-android-arm64.txt" \
    -Dbuildtype=release -Ddxvk_native_wsi=sdl3 \
    -Denable_dxgi=false -Denable_d3d10=false -Denable_d3d11=false
  ninja -C "$OUT/dxvk-build" -j"$JOBS"
  for lib in d3d8 d3d9; do
    "$TC/bin/llvm-strip" --strip-unneeded -o "$DXVK_OUT/libdxvk_$lib.so" \
      "$OUT/dxvk-build/src/$lib/libdxvk_$lib.so"
  done
  ls -l "$DXVK_OUT"
}

build_engine() {
  : "${VCPKG_ROOT:?set VCPKG_ROOT to a bootstrapped vcpkg checkout}"
  cmake -S "$ROOT/engine" -B "$ENGINE_BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DVCPKG_TARGET_TRIPLET=arm64-android \
    -DVCPKG_OVERLAY_TRIPLETS="$ROOT/scripts/ci/vcpkg-triplets" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-$API \
    -DCMAKE_BUILD_TYPE=Release \
    -DRTS_BUILD_ZEROHOUR=ON -DRTS_BUILD_GENERALS=OFF \
    -DRTS_BUILD_CORE_TOOLS=OFF -DRTS_BUILD_ZEROHOUR_TOOLS=OFF \
    -DRTS_BUILD_OPTION_SAGE_PATCH=OFF \
    -DSAGE_USE_DX8=OFF -DSAGE_USE_SDL3=ON -DSAGE_USE_OPENAL=ON -DSAGE_USE_MINIAUDIO=OFF \
    -DSAGE_USE_GLM=ON -DSAGE_UPDATE_CHECK=OFF \
    -DRTS_BUILD_OPTION_FFMPEG=ON -DRTS_CRASHDUMP_ENABLE=OFF
  # -k 0: report every compile error in one CI run instead of stopping at the first.
  cmake --build "$ENGINE_BUILD" --target z_generals -j"$JOBS" -- -k 0 2>&1 | tee "$OUT/engine-build.log"
}

# Copy libmain.so, every engine-built .so it needs, and DXVK into jniLibs, then check that each
# DT_NEEDED entry is either packaged or an Android system library.
stage() {
  local readelf="$TC/bin/llvm-readelf"
  local -a system_libs=(libc.so libm.so libdl.so liblog.so libandroid.so libvulkan.so libOpenSLES.so
    libEGL.so libGLESv1_CM.so libGLESv2.so libGLESv3.so libz.so libjnigraphics.so libmediandk.so
    libaaudio.so libamidi.so libnativewindow.so libsync.so libOpenMAXAL.so)
  rm -rf "$JNILIBS" && mkdir -p "$JNILIBS"

  local main
  main="$(find "$ENGINE_BUILD" -name libmain.so -type f | head -n1)"
  [[ -n "$main" ]] || { echo "libmain.so not found under $ENGINE_BUILD" >&2; exit 1; }
  cp "$main" "$JNILIBS/"
  cp "$DXVK_OUT/libdxvk_d3d8.so" "$DXVK_OUT/libdxvk_d3d9.so" "$JNILIBS/"

  # SDLActivity always loads libSDL3.so itself; the rest comes from the DT_NEEDED walk below.
  local -a wanted=(libSDL3.so)
  local missing=0 changed=1 found
  while (( changed )); do
    changed=0
    for lib in "$JNILIBS"/*.so; do
      while read -r need; do
        wanted+=("$need")
      done < <("$readelf" -d "$lib" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    done
    for need in "${wanted[@]}"; do
      [[ -f "$JNILIBS/$need" || " ${system_libs[*]} " == *" $need "* ]] && continue
      if [[ "$need" != lib*.so ]]; then
        # The APK can only carry lib*.so, so a versioned soname would never be found on device.
        echo "ERROR: something links against '$need', which cannot be packaged in an APK" >&2
        missing=1; continue
      fi
      found="$(find "$ENGINE_BUILD" -name "$need" \( -type f -o -type l \) | head -n1)"
      if [[ -n "$found" ]]; then
        cp -L "$found" "$JNILIBS/$need"; changed=1
      else
        echo "ERROR: $need is needed but was not built and is not an Android system library" >&2
        missing=1
      fi
    done
  done
  (( missing == 0 )) || exit 1

  # Keep an unstripped libmain.so for symbolizing crash logs; Gradle strips what it packages.
  mkdir -p "$OUT/symbols" && cp "$main" "$OUT/symbols/"
  ls -l "$JNILIBS"
  for lib in "$JNILIBS"/*.so; do
    echo "$(basename "$lib") needs: $("$readelf" -d "$lib" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
  done
}

mkdir -p "$OUT"
case "${1:-all}" in
  dxvk)   build_dxvk ;;
  engine) build_engine ;;
  stage)  stage ;;
  all)    build_dxvk; build_engine; stage ;;
  *) echo "usage: $0 [dxvk|engine|stage|all]" >&2; exit 2 ;;
esac
