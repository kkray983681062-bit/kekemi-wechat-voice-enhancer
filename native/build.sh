#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
FLAGS='-std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation -ffunction-sections -fdata-sections -finput-charset=UTF-8'
OUT=${NATIVE_OUT:-build/Kekemi-WeChat-Voice-Enhancer.exe}
$CC $FLAGS tests_core.c core.c discovery.c compat.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -lbcrypt -o build/tests-core.exe
$CC $FLAGS tests_compat.c core.c discovery.c compat.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -lbcrypt -municode -o build/tests-compat.exe
$CC $FLAGS tests_clients.c clients.c bridge.c core.c discovery.c compat.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -lbcrypt -o build/tests-clients.exe
$CC $FLAGS tests_shim.c opus_shim.S -static-libgcc -o build/tests-shim.exe
$CC $FLAGS tests_codec_update.c core.c discovery.c compat.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -lbcrypt -lm -municode -o build/tests-codec-update.exe
$CC $FLAGS tests_payload_update.c core.c discovery.c compat.c opus_shim.S vendor/minhook/src/hook.c vendor/minhook/src/buffer.c vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -lbcrypt -lm -municode -o build/tests-payload-update.exe
$CC $FLAGS -shared payload.c core.c discovery.c compat.c opus_shim.S vendor/minhook/src/hook.c vendor/minhook/src/buffer.c vendor/minhook/src/trampoline.c vendor/minhook/src/hde/hde64.c -Wl,--gc-sections -static-libgcc -s -lbcrypt -o build/wx-native.dll
$WINDRES -I . app.rc -O coff -o build/app-res.o
$CC $FLAGS ui.c clients.c bridge.c core.c discovery.c compat.c vendor/minhook/src/hde/hde64.c build/app-res.o -municode -mwindows -Wl,--gc-sections -static-libgcc -s -lbcrypt -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -o "$OUT"
