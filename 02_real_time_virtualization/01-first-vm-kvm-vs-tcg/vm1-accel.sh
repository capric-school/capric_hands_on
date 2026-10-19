#!/usr/bin/env bash
# vm1-tcg.sh — Boot the demo VM using pure QEMU emulation (TCG).
#
# This forces QEMU to use dynamic binary translation instead of hardware
# virtualization. Every guest instruction is translated into host
# instructions in user-space.

set -euo pipefail

IMAGE="${IMAGE:-$HOME/kvm-demo/debian-base.qcow2}"

if [[ ! -f "$IMAGE" ]]; then
  echo "ERROR: image not found at $IMAGE"
  echo "Run setup/build-image.sh first, or set IMAGE=... in the environment."
  exit 1
fi

echo "Booting with KVM acceleration. This will be slow..."

qemu-system-x86_64 \
  -accel kvm -cpu qemu64 -smp 1 -m 1G \
  -drive file="$IMAGE",if=virtio,snapshot=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2223-:22 \
  -device virtio-net-pci,netdev=n0 \
  -nographic \
  -name accel-demo,debug-threads=on
