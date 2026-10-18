#!/bin/bash
# Builds Hydro Thunder in WSL after setup.sh. usage: build.sh win|linux [jobs]
set -euo pipefail
TARGET="$1"; JOBS="${2:-14}"
HT=/root/ht
APP="$HT/src/app"
B="$HT/build-$TARGET"

# FidelityFX's shader build calls "wine FidelityFX_SC.exe ..."; run the Linux port.
mkdir -p "$HT/toolbin"
cp "$HT/src/ffx-linux-build/toolbin/glslangValidator" "$HT/toolbin/"
cat > "$HT/toolbin/wine" <<EOS
#!/bin/sh
shift
ASAN_OPTIONS=detect_leaks=0 exec $HT/src/ffx-linux-build/ffxsc/FidelityFX_SC_asan "\$@"
EOS
chmod +x "$HT/toolbin/"* "$HT/src/ffx-linux-build/ffxsc/"FidelityFX_SC_*
export PATH="$HT/toolbin:$PATH"
export HT_LLVM_MINGW_ROOT="$HT/llvm-mingw"
LM="$HT/llvm-mingw/bin"

COMMON=(-G Ninja "$APP" -DCMAKE_BUILD_TYPE=Release -DHT_CPU_ARCH=x86-64-v3 -DHT_CPU_TUNE=znver4
  -DREXGLUE_USE_VULKAN=ON -DREXGLUE_USE_D3D12=OFF -DREXGLUE_ENABLE_FIDELITYFX=ON
  -DREXGLUE_BUILD_TESTS=OFF -DREXSDK_DIR="$HT/sdk" -DHT_GENERATED_DIR="$APP/generated-updated")
if [ "$TARGET" = win ]; then
  # FidelityFX's VK backend runs find_package(Vulkan); a cross build has no
  # Vulkan SDK, so point it at the SDK's headers and a vulkan-1.dll import lib.
  VKINC="$HT/sdk/thirdparty/vulkan-headers/include"
  VKLIB="$HT/vulkan-mingw/libvulkan-1.a"
  if [ ! -f "$VKLIB" ]; then
    mkdir -p "$HT/vulkan-mingw"
    { echo "LIBRARY vulkan-1.dll"; echo "EXPORTS";
      grep -ohE 'VKAPI_CALL vk[A-Za-z0-9]+\(' "$VKINC/vulkan/vulkan_core.h" "$VKINC/vulkan/vulkan_win32.h" \
        | sed -E 's/VKAPI_CALL (vk[A-Za-z0-9]+)\(/\1/' | sort -u; } > "$HT/vulkan-mingw/vulkan-1.def"
    "$LM/llvm-dlltool" -m i386:x86-64 -d "$HT/vulkan-mingw/vulkan-1.def" -l "$VKLIB"
  fi
  # Windows sources include headers with mixed case (Windows.h, ObjBase.h); the
  # MinGW headers are lower case and Linux file names are case sensitive.
  MGINC="$HT/llvm-mingw/x86_64-w64-mingw32/include"
  grep -rhoE '#\s*include\s*<[A-Za-z0-9_]+\.[hH]>' "$HT/sdk/src" "$HT/sdk/include" "$APP/src" 2>/dev/null \
    | sed -E 's/.*<([^>]+)>/\1/' | sort -u | while read -r hdr; do
      low=$(echo "$hdr" | tr 'A-Z' 'a-z')
      if [ "$hdr" != "$low" ] && [ ! -e "$MGINC/$hdr" ] && [ -e "$MGINC/$low" ]; then ln -s "$low" "$MGINC/$hdr"; fi
    done
  EXTRA=(-DCMAKE_TOOLCHAIN_FILE="$HT/src/windows-toolchain.cmake"
    -DVulkan_INCLUDE_DIR="$VKINC" -DVulkan_LIBRARY="$VKLIB"
    -DVulkan_GLSLANG_VALIDATOR_EXECUTABLE="$HT/toolbin/glslangValidator")
else
  EXTRA=(-DCMAKE_C_COMPILER="$LM/clang" -DCMAKE_CXX_COMPILER="$LM/clang++"
    -DCMAKE_C_COMPILER_TARGET=x86_64-pc-linux-gnu -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-linux-gnu
    -DCMAKE_LINKER_TYPE=LLD -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld
    -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_WAYLAND_LIBDECOR=OFF -DSDL_KMSDRM=OFF -DSDL_DBUS=OFF
    -DSDL_IBUS=OFF -DSDL_HIDAPI_LIBUSB=OFF)
fi

mkdir -p "$B"; cd "$B"
# FidelityFX writes its static libs to <source>/bin; keep one per target so the
# Windows and Linux builds do not link each other's objects.
mkdir -p "$B/ffxbin"
if [ ! -L "$APP/bin" ]; then rm -rf "$APP/bin"; fi
ln -sfn "$B/ffxbin" "$APP/bin"
echo "== configure $TARGET $(date +%T)"
cmake "${COMMON[@]}" "${EXTRA[@]}" > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
FFX=$(find "$B" -maxdepth 3 -type d -name fidelityfx-src | head -1)
echo "== fidelityfx: $FFX"
if ! grep -q HT_FFX_ALIGN_PTR "$FFX/sdk/src/backends/vk/ffx_vk.cpp"; then
  (cd "$FFX" && git apply --include='sdk/src/backends/vk/ffx_vk.cpp' "$HT/src/fidelityfx-mingw-alignment.patch")
fi
grep -c HT_FFX_ALIGN_PTR "$FFX/sdk/src/backends/vk/ffx_vk.cpp"
# The cross build invokes binary_store/FidelityFX_SC.exe directly: make it the Linux port.
SC="$FFX/sdk/tools/binary_store/FidelityFX_SC.exe"
if ! head -c2 "$SC" | grep -q '#!'; then
  printf '#!/bin/sh\nASAN_OPTIONS=detect_leaks=0 exec %s "$@"\n' \
    "$HT/src/ffx-linux-build/ffxsc/FidelityFX_SC_asan" > "$SC"
fi
chmod +x "$SC"
echo "== build $(date +%T)"
ninja -j"$JOBS" > build.log 2>&1 || { grep -E 'error|FAILED' build.log | head -40; tail -20 build.log; exit 1; }
echo "== done $(date +%T)"
find "$B" "$HT/sdk/out" -newer configure.log -type f \( -name 'hydrothunder*' -o -name '*.dll' -o -name '*.so' -o -name 'ht-import*' \) \
  -not -path '*/CMakeFiles/*' -printf '%TT %10s %p\n' | sort | tail -20
