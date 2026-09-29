#!/usr/bin/env bash
# Builds blob.bin - the position-independent, import-free menu image.
#
# Requires:
#   * ps5-payload-dev SDK  ->  https://github.com/ps5-payload-dev/sdk
#   * clang + ld.lld with the x86_64-sie-ps5 target (LLVM 16+; tested on 22.1.4)
#
# The SDK ships bash wrappers that locate LLVM through `llvm-config`. If your
# LLVM has no llvm-config (some Windows/scoop builds), this script bypasses the
# wrappers and drives clang/ld.lld directly, which is all that is needed here.
set -euo pipefail

PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
[ -d "$PS5_PAYLOAD_SDK" ] || { echo "set PS5_PAYLOAD_SDK to your SDK path"; exit 1; }

CXX="${CXX:-clang++}"
LLD="${LLD:-ld.lld}"
OBJCOPY="${OBJCOPY:-llvm-objcopy}"
NM="${NM:-llvm-nm}"
OBJDUMP="${OBJDUMP:-llvm-objdump}"

# -fno-zero-initialized-in-bss keeps zeroed globals in .data so objcopy emits them.
CXXFLAGS=(
  --target=x86_64-sie-ps5
  -std=c++17 -O2
  -fno-exceptions -fno-rtti
  -ffreestanding -fno-builtin
  -fno-stack-protector -fno-plt
  -fvisibility=hidden
  -fno-asynchronous-unwind-tables
  -mno-red-zone
  -fno-zero-initialized-in-bss
  -Iinclude -Wall
)

"$CXX" "${CXXFLAGS[@]}" -c -o blob.o src/blob.cpp
"$LLD" -T blob.ld --no-dynamic-linker -o blob.elf blob.o
"$OBJCOPY" -O binary blob.elf blob.bin

# The blob must be fully position independent: any leftover relocation is a bug.
if "$OBJDUMP" -r blob.elf | grep -q R_X86_64; then
  echo "ERROR: blob.elf still has relocations - it will crash when injected." >&2
  "$OBJDUMP" -r blob.elf | grep R_X86_64 | head
  exit 1
fi

# Offsets the injector needs to patch/read.
"$NM" blob.elf | grep -E "hook_run|g_origRun|g_hits|g_menuOpen|g_dbg" \
  | awk '{print $3" 0x"$1}' > blob_syms.txt

echo "blob.bin  $(stat -c%s blob.bin) bytes, 0 relocations"
cat blob_syms.txt
