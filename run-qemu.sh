#!/usr/bin/env bash
#
# run-qemu.sh - Boot the Prism kernel ISO in QEMU, "literally in your terminal".
#
# What you get:
#   * Kernel serial output is mirrored to THIS terminal (via -serial stdio).
#   * A QEMU window opens showing the VGA console where you can TYPE on your
#     real keyboard (PS/2) and see characters echoed by the kernel.
#
# Controls:
#   * Click the QEMU window to capture keyboard/mouse.
#   * Ctrl+Alt releases mouse/keyboard focus back to your desktop.
#   * Ctrl+Alt+G releases keyboard grab.
#   * Type 's' + Enter in the OS, or just close the window, to stop.
#
# Usage:
#   ./run-qemu.sh            # build first if needed, then boot
#   ./run-qemu.sh --no-build # skip building, just boot the existing ISO

set -euo pipefail

cd "$(dirname "$0")"

ISO="build/prism.iso"

# Optional: rebuild unless explicitly skipped.
if [ "${1:-}" != "--no-build" ]; then
  echo "[run-qemu] building ISO..."
  make iso >/dev/null
fi

if [ ! -f "$ISO" ]; then
  echo "[run-qemu] ERROR: $ISO not found. Run 'make iso' first." >&2
  exit 1
fi

# Use hardware acceleration when available, else pure TCG emulation.
if [ -e /dev/kvm ] && command -v qemu-system-x86_64 >/dev/null; then
  ACCEL="-enable-kvm"
else
  ACCEL="-accel tcg"
fi

echo "[run-qemu] launching $ISO ..."
echo "[run-qemu] (click the window to type; Ctrl+Alt releases focus; close window to quit)"
echo

exec qemu-system-x86_64 \
  $ACCEL \
  -m 256M \
  -cdrom "$ISO" \
  -serial stdio \
  -display gtk \
  -device VGA \
  -device PS2Keyboard \
  -rtc base=localtime \
  -no-reboot
