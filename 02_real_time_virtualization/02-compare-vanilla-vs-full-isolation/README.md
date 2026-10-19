# Experiment 02. Comparing vanilla and CPU-isolated Linux for nested KVM VMs

This lab compares wake-up latency (we use ``cyclictest``) inside a nested KVM guest under CPU stress, using an OpenStack instance with a certain number of vCPUs. The task cyclictest runs is a small measurement loop: sleep, wake up, read the clock, record how late it woke up, and repeat. In this example, we use **8 vCPUs**, but adjust this example according to the vCPUs you assigned to your VM. This hands-on aims to demonstrate whether isolating selected CPUs in the OpenStack instance reduces interference from ordinary background workloads.

## 1. Execution levels and controlled comparison

- **L0:** the physical OpenStack compute host. Its scheduling and hardware placement are outside this lab's control.
- **L1:** the Ubuntu OpenStack VM, with CPUs `0–7`. QEMU and stress-ng run here.
- **L2:** the Debian guest started by QEMU, with one vCPU. cyclictest runs here.

Unless explicitly labeled **L2**, commands run in **L1**.

| Setting | Vanilla | Isolation configuration |
| --- | --- | --- |
| L2 QEMU vCPU thread | Pinned to L1 CPU 1 | Pinned to L1 CPU 1 |
| L2 Other QEMU threads | Allowed on L1 CPUs 0–7 | Allowed on L1 CPUs 0–7 |
| L1 Background stress | Eight unpinned CPU workers | Eight unpinned CPU workers |
| Isolated CPUs | None | L1 CPUs 1 |
| Additional boot options | None | `nohz_full=1 isolcpus=1 rcu_nocbs=1 irqaffinity=0,2-7` |
| Measurement | Same 60-second cyclictest command | Same 60-second cyclictest command |

Keep the instance flavor, kernel version, guest image, QEMU options, thread placement, and measurement duration unchanged. Do not apply real-time scheduling to QEMU's vCPU thread during this comparison.

## 2. Prerequisites and initial backup

The lab assumes nested KVM works and the existing lab image is available at `~/kvm-demo/debian-base.qcow2`. Complete the [environment setup](../setup/README.md) first.

```bash
cat /sys/devices/system/cpu/online
ls -l /dev/kvm
test -r /dev/kvm && test -w /dev/kvm && echo 'KVM device accessible'
test -f "$HOME/kvm-demo/debian-base.qcow2" && echo 'Guest image found'
sudo apt update
sudo apt install stress-ng sysstat
```

Expect CPUs `0-7`. Use the same QEMU version throughout. Before changing GRUB, save its current configuration once:

```bash
BACKUP_DIR="$HOME/grub-backup-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"
sudo cp -a /etc/default/grub /etc/default/grub.d "$BACKUP_DIR/"
echo "Backup: $BACKUP_DIR"
```

If isolation was already enabled, this backup captures that state; it is not necessarily a backup of the original vanilla configuration.

## 3. Select and verify a boot configuration

### A. Vanilla

Remove only this lab's override, if present:

```bash
sudo rm -f /etc/default/grub.d/99-lab-isolation.cfg
sudoedit /etc/default/grub
```

Remove previously added isolation options from `/etc/default/grub`, preserving unrelated existing settings. Do not edit or remove `50-cloudimg-settings.cfg`. Inspect assignments for leftover overrides:

```bash
grep -nE '^[[:space:]]*GRUB_CMDLINE_LINUX' \
  /etc/default/grub /etc/default/grub.d/*.cfg
sudo update-grub
sudo grep -E '^[[:space:]]*linux[[:space:]]' /boot/grub/grub.cfg
```

The normal boot entry must have no lab isolation options. Preserve cloud console options such as `console=tty1 console=ttyS0`. Then reboot:

```bash
sudo reboot
```

After reconnecting:

```bash
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
cat /sys/devices/system/cpu/nohz_full
```

Expect an empty isolated CPU list and typically `(null)` for `nohz_full`.

### B. Isolation configuration

We want to enable the following kernel options:

| Option | Effect within L1 |
| --- | --- |
| `isolcpus=1` | Excludes these CPUs from normal scheduler load balancing |
| `nohz_full=1` | Suppresses scheduling ticks when eligible; it does not eliminate every interrupt |
| `rcu_nocbs=1` | Offloads RCU callback processing from these CPUs |
| `irqaffinity=0,2-7` | Sets default IRQ affinity; drivers and irqbalance can affect actual placement |

These settings neither configure physical-host isolation nor turn the guest kernel into PREEMPT_RT.


Start from the cleaned configuration above. Ubuntu cloud images can override `/etc/default/grub` through `50-cloudimg-settings.cfg`, so use a later file that appends to the cloud settings:

```bash
sudo tee /etc/default/grub.d/99-lab-isolation.cfg >/dev/null <<'EOF'
GRUB_CMDLINE_LINUX_DEFAULT="${GRUB_CMDLINE_LINUX_DEFAULT} nohz_full=1 isolcpus=1 rcu_nocbs=1 irqaffinity=0,2-7"
EOF

sudo update-grub
sudo grep -E '^[[:space:]]*linux[[:space:]]' /boot/grub/grub.cfg
```

Confirm that normal entries contain the four options, without conflicting duplicate CPU ranges, and retain console settings. Recovery entries may omit them.

```bash
sudo reboot
```

After reconnecting:

```bash
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
cat /sys/devices/system/cpu/nohz_full
```

The last two should show `1`. If not, inspect kernel configuration and boot messages before measuring:

```bash
grep -E '^CONFIG_(NO_HZ_FULL|RCU_NOCB_CPU)=' /boot/config-$(uname -r)
sudo dmesg | grep -Ei 'nohz|rcu|isolat'
```


## 4. Launch L2 identically after each boot

Ensure port 2222 is free and an earlier copy of the lab VM is not running. In **L1 terminal A**:

```bash
taskset -c 0-7 qemu-system-x86_64 \
  -enable-kvm -cpu host -smp 1 -m 1G \
  -drive file="$HOME/kvm-demo/debian-base.qcow2",if=virtio,snapshot=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device virtio-net-pci,netdev=n0 \
  -nographic \
  -name isolation-demo,debug-threads=on
```

Initially **all QEMU threads** inherit `0-7`, including the vCPU thread. In **L1 terminal B**, find and move only the vCPU thread:

```bash
$ PID=$(pgrep -n -f 'qemu-system-x86_64.*isolation-demo')
$ ps -T -p "$PID" -o tid,psr,comm
TID PSR COMMAND
  11867   6 qemu-system-x86
  11868   4 qemu-system-x86
  11869   7 worker
  11871   5 CPU 0/KVM
  11873   5 kvm-nx-lpage-re
  11874   5 worker
  11877   5 worker
  11896   5 worker
  11897   6 worker
  11987   5 worker
  11988   5 worker
  11989   6 worker
  11990   6 worker
  11991   6 worker
  11992   4 worker
  11993   4 worker
  11994   7 worker
  11995   0 worker
```

```bash
$ VCPU_TID=$(ps -T -p "$PID" -o tid,comm | awk '/CPU 0\/KVM/ {print $1}')
$ printf 'QEMU PID=%s; vCPU TID=%s\n' "$PID" "$VCPU_TID"
QEMU PID=11867; vCPU TID=11871
```

Confirm that there is exactly one matching QEMU process and one `CPU 0/KVM` thread. If the TID is empty, inspect the names above rather than continuing.

```bash
$ sudo taskset -pc 1 "$VCPU_TID"
pid 11871's current affinity list: 0-7
pid 11871's new affinity list: 1

$ taskset -apc "$PID"
pid 11867's current affinity list: 0-7
pid 11868's current affinity list: 0-7
pid 11871's current affinity list: 1
pid 11873's current affinity list: 0-7

$ chrt -p "$VCPU_TID"
pid 11871's current scheduling policy: SCHED_OTHER
pid 11871's current scheduling priority: 0
```

Expect vCPU affinity `1`, other QEMU thread affinities `0-7`, and the vCPU scheduling policy `SCHED_OTHER`. Repeat this step after every QEMU restart: thread IDs change.

## 5. Prepare the measurement

From L1, connect to L2 using the lab credentials:

```bash
ssh -p 2222 root@127.0.0.1

## password is demo
```

**Inside L2**, if necessary:

```bash
apt-get update
apt-get install -y rt-tests
```

The measurement command is:

```bash
cyclictest -m -t1 -a0 -p80 -i1000 -D60s -q
```

It uses one measurement thread on **L2 CPU 0**, real-time priority 80, a 1,000 microsecond interval, and 60 seconds of measurement. `-m` locks memory and `-q` prints the final summary. L2 CPU 0 maps to the QEMU thread pinned to **L1 CPU 1**.

The measured quantity is wake-up latency, not job execution time or end-to-end application response time. The priority inside L2 does not change QEMU's scheduling priority in L1.

Because QEMU uses `snapshot=on`, guest filesystem changes are discarded at shutdown. Reinstall rt-tests on each launch if needed. Save measurement logs in L1, as below, to retain them.

## 6. Collect results for baseline and stress-induced experiments

For each boot configuration, take a no-stress baseline and measurements with stress induced (repeat measurements to assess variability; multiple runs alone do not establish statistical significance). Use the same quiet period after guest startup and finish package installation before collecting data. Avoid unrelated jobs during the experiment.

In **L1 terminal B**, set a label matching the boot configuration:

```bash
CONFIG=vanilla  # Change to isolated after booting the isolation configuration
mkdir -p "$HOME/isolation-results/$CONFIG"
uname -a > "$HOME/isolation-results/$CONFIG/kernel.txt"
cat /proc/cmdline > "$HOME/isolation-results/$CONFIG/cmdline.txt"
taskset -apc "$PID" > "$HOME/isolation-results/$CONFIG/qemu-affinity.txt"
```

Use SSH to execute cyclictest in L2 while saving output in L1. First, with no stress running:

```bash
set -o pipefail
ssh -p 2222 root@127.0.0.1 \
  'cyclictest -m -t1 -a0 -p80 -i1000 -D60s -q' 2>&1 \
  | tee "$HOME/isolation-results/$CONFIG/baseline.txt"
```

For measurements with stress-based experiments, open **L1 terminal C as a normal SSH session** and run without taskset:

```bash
stress-ng --cpu 8 --cpu-method int64 --timeout 90s
```

Promptly start the 60-second measurement in terminal B (within about 20 seconds, including authentication), so it remains entirely inside the stress interval:

```bash
RUN=1  # Use 2, 3, ... for subsequent runs
ssh -p 2222 root@127.0.0.1 \
  'cyclictest -m -t1 -a0 -p80 -i1000 -D60s -q' 2>&1 \
  | tee "$HOME/isolation-results/$CONFIG/stress-$RUN.txt"
```

If stress ends before ``cyclictest``, discard that run and repeat it. Wait for the previous stress command to finish before launching the next one; do not overlap stress runs. Collect at least three runs per configuration. If practical, repeat the vanilla boot afterward to check for changes in cloud-host load over time.

## 7. Verify actual workload placement

Do an initial verification run in each configuration. Avoid extra monitoring in timed runs, or keep it identical between configurations.

```bash
watch -n 0.5 'ps -eLo pid,tid,psr,comm | grep "[s]tress-ng"'
```

`PSR` is the CPU on which a thread most recently executed, not its allowed affinity mask. In the isolated setup, ordinarily launched stress workers should appear on `0,2,3,4,5,6,7`, not `1`. Inspect several refreshes; this is sampling, not proof of every scheduling event.

```bash
mpstat -P ALL 1
```

In the isolated configuration, expect the housekeeping CPUs to be busy, CPU 1 to execute the nested vCPU intermittently. CPU 1 need not reach 100% because cyclictest mostly sleeps. `%steal`, when reported, can indicate time denied by L0; a zero reading does not establish the absence of host interference.

If stress appears on isolated CPUs, inspect the launching shell's affinity and worker masks:

```bash
taskset -pc $$
for tid in $(pgrep -f '^stress-ng'); do
  taskset -pc "$tid"
done
```

Do not launch stress from a shell previously pinned to CPU 1. Isolation does not prohibit explicit placement on an isolated CPU. An allowed mask that includes isolated CPUs does not itself prove execution there: affinity permission and scheduler load balancing are different mechanisms.

For IRQ inspection, use `cat /proc/irq/default_smp_affinity` and compare `/proc/interrupts` over time. The default mask alone does not prove every IRQ follows it. Keep irqbalance and other services in the same state across runs and record any deviations.

## 8. Record and interpret the comparison

Use one row per measured run:

| Configuration | Load | Run | Average (µs) | Maximum (µs) | Samples C | Notes |
| --- | --- | --- | ---: | ---: | ---: | --- |
| Vanilla | None | 1 | | | | |
| Vanilla | 8 unpinned CPU workers | 1 | | | | |
| Vanilla | 8 unpinned CPU workers | 2 | | | | |
| Vanilla | 8 unpinned CPU workers | 3 | | | | |
| Isolated | None | 1 | | | | |
| Isolated | 8 unpinned CPU workers | 1 | | | | |
| Isolated | 8 unpinned CPU workers | 2 | | | | |
| Isolated | 8 unpinned CPU workers | 3 | | | | |

cyclictest reports latency in microseconds for these commands. `C` is the number of measurement cycles, not a deadline-miss count. `Act` is the latest latency sample, not a representative average. Compare both averages and maxima across repeated runs; maxima alone are sensitive to rare events. Percentiles require histogram or sample collection, not just the summary shown here.

### Example observations from the initial experiment

These are user-observed single 60-second runs with eight unpinned CPU workers in L1, not predicted results or a repeated-run validation:

| Configuration | Minimum (µs) | Average (µs) | Maximum (µs) | Samples |
| --- | ---: | ---: | ---: | ---: |
| Vanilla | 23 | 80 | 62,750 | 59,358 |
| Full isolation configuration | 24 | 154 | 16,743 | 59,883 |

The observed maximum decreased by approximately 73.3% (3.75 times lower), while the average increased from 80 to 154 µs. A defensible statement is:

Why could an isolation-based scenario have a higher average? With stress excluded from CPU 1, both L2 and the underlying L1 vCPU may become idle. If L1 blocks rather than continuing to poll, a subsequent wake-up can require L0 to schedule it again before L1 can resume L2. KVM’s halt-polling mechanism exists specifically to avoid some of this block-and-reschedule latency. However, stress can also delay L2’s vCPU in vanilla: keeping L1 active does not guarantee prompt dispatch of L2. That could produce a pattern of many short wake-ups plus occasional large contention delays.

Measurements in an OpenStack (nested) environment can be consistent with this hypothesis, but do not prove it. In particular, isolation does not necessarily make L1 block—other work and halt polling matter—and “L1 idle” does not necessarily mean the physical processor entered a deep sleep state. Do not infer a guaranteed latency bound or attribute the change solely to `isolcpus`. L0 can still deschedule L1, and different L1 vCPUs are not necessarily dedicated physical cores. Shared caches, memory bandwidth, interrupt activity, and QEMU emulator scheduling can still matter. The result may differ on another OpenStack host.

## 9. Optional follow-up experiments

**Isolate the effect of isolcpus:** add a third boot configuration with only `isolcpus=1`, keeping the same QEMU placement and unpinned stress. Compare vanilla, isolcpus-only, and the full bundle. This does not separately identify every parameter's contribution.

**Demonstrate deliberate contention:** under one fixed boot configuration, compare one stress worker pinned to CPU 1 with one pinned to CPU 4:

```bash
# Run separately; allow each command to finish before the next test.
taskset -c 1 stress-ng --cpu 1 --cpu-method int64 --timeout 90s
taskset -c 4 stress-ng --cpu 1 --cpu-method int64 --timeout 90s
```

This tests explicit shared-versus-separate CPU placement. Keep these results separate from the eight-worker vanilla-versus-isolation comparison.

## 10. Cleanup and return to vanilla

Shut down L2 cleanly using `poweroff` inside the guest. Let timed stress runs finish. Keep the L1 results directory.

To return to vanilla, remove the lab override, ensure no isolation options remain in other GRUB assignments, regenerate GRUB, inspect normal boot entries, and reboot as in section 3A. Verify `/proc/cmdline` afterward. Do not remove the cloud-image settings file or its console arguments.

## References

- [Linux kernel command-line parameters](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html)
- [Linux CPU isolation](https://www.kernel.org/doc/html/next/admin-guide/cpu-isolation.html)
- [rt-tests cyclictest manual source](https://kernel.googlesource.com/pub/scm/utils/rt-tests/rt-tests/+/refs/heads/main/src/cyclictest/cyclictest.8)
- [OpenStack nested KVM configuration](https://docs.openstack.org/nova/latest/admin/configuration/hypervisor-kvm.html)
