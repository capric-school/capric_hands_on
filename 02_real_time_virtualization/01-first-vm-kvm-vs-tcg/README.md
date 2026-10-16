# Experiment 01. KVM versus TCG: comparing execution time

## Goal

Compare the elapsed time required to execute the same fixed CPU workload in a guest using **KVM hardware acceleration** and **QEMU TCG software emulation**.

Perform this experiment **before the CPU affinity and isolation experiments**. Use a vanilla host kernel command line, no CPU pinning, and no background stress. Run the two guests sequentially.

## What this experiment demonstrates

With KVM, guest instructions execute directly on the processor using hardware virtualization. With TCG, QEMU translates guest code into host instructions and caches translated blocks for reuse.

Both modes may consume approximately one full CPU while running a single-threaded workload. The useful comparison is **time to complete the same work**, not merely instantaneous CPU utilization.

This lab measures a Python integer workload. It is not a comprehensive benchmark of virtualization, networking, storage, or real-time behavior.

## Environment and prerequisites

In the OpenStack setup:

- **L0:** the physical OpenStack compute host.
- **L1:** your Ubuntu OpenStack instance, where QEMU runs.
- **L2:** the Debian guest, where the benchmark runs.

On bare metal, the same commands apply: run L1-labeled commands on the Linux machine hosting QEMU.

Requirements:

- QEMU installed and KVM usable by your account.
- On OpenStack, nested virtualization enabled.
- The lab guest image at `~/kvm-demo/debian-base.qcow2`.
- Python 3 inside the guest, or Internet access to install it.
- No other lab VM listening on L1 TCP port 2222.

Inside **L1**, check:

```bash
cat /proc/cmdline
cat /sys/devices/system/cpu/isolated
test -r /dev/kvm && test -w /dev/kvm && echo 'KVM device accessible'
test -f "$HOME/kvm-demo/debian-base.qcow2" && echo 'Guest image found'
qemu-system-x86_64 --version
```

The command line should have no lab isolation options (`isolcpus`, `nohz_full`, `rcu_nocbs`, or custom `irqaffinity`), and the isolated CPU list should be empty. Finish any previous stress runs before starting. Do not use `taskset` or `chrt` in this experiment.

## Controlled settings

| Setting | KVM run | TCG run |
| --- | --- | --- |
| Accelerator | `kvm` | `tcg` |
| CPU model | `qemu64` | `qemu64` |
| Guest vCPUs | 1 | 1 |
| Guest memory | 1 GiB | 1 GiB |
| Disk image and disk interface | Same image, virtio | Same image, virtio |
| Network device | virtio | virtio |
| CPU affinity | Unpinned | Unpinned |
| Background stress | None | None |
| Workload | Same Python script | Same Python script |

`qemu64` is an explicit common CPU model suitable for this basic workload. It reduces CPU-feature differences compared with using `host` for KVM and `max` for TCG. It is not a claim that every exposed CPU detail or implementation is identical.

## Step 1 — Boot with KVM

Run in **L1 terminal A**:

```bash
qemu-system-x86_64 \
  -accel kvm -cpu qemu64 -smp 1 -m 1G \
  -drive file="$HOME/kvm-demo/debian-base.qcow2",if=virtio,snapshot=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device virtio-net-pci,netdev=n0 \
  -nographic \
  -name accel-demo,debug-threads=on
```

Log in at the guest console as `root` with password `demo`, unless you changed the lab credentials. Alternatively, connect from another L1 terminal:

```bash
ssh -p 2222 root@127.0.0.1
```

Wait for startup activity to settle before measuring. If KVM initialization fails, fix KVM availability or permissions; do not silently fall back to TCG for this run.

## Step 2 — Prepare the fixed workload inside L2

Run **inside L2**:

```bash
command -v python3 || {
  apt-get update
  apt-get install -y python3
}

python3 --version
uname -r
```

Use the same Python version in both modes. Complete package installation before timing.

Create the benchmark:

```bash
cat > /tmp/cpu-bench.py <<'PY'
import statistics
import time

N = 100_000

def workload():
    value = 1
    for i in range(N):
        value = (value * 1664525 + i + 1013904223) & 0xffffffff
    return value

print(f"Iterations per run: {N}", flush=True)
print("Warm-up...", flush=True)
workload()

times = []
for run in range(1, 6):
    start = time.perf_counter()
    checksum = workload()
    elapsed = time.perf_counter() - start
    times.append(elapsed)
    print(
        f"run={run} elapsed_s={elapsed:.6f} checksum={checksum}",
        flush=True,
    )

print(f"median_s={statistics.median(times):.6f}", flush=True)
PY
```

The benchmark executes a fixed number of integer operations, with values bounded to 32 bits, through the Python interpreter. It performs one untimed warm-up and five timed runs. Warm-up reduces initial execution effects, including initial TCG translation in the later run, but does not remove all runtime variability.

Each `elapsed_s` is elapsed time measured inside the guest, including delays while its execution is interrupted or descheduled. It excludes boot, installation, the warm-up, and printing the result.

## Step 3 — Measure KVM and retain the output

You can run the benchmark directly **inside L2**:

```bash
python3 /tmp/cpu-bench.py
```

For convenient persistent logging, instead run it through SSH from **L1 terminal B**:

```bash
mkdir -p "$HOME/kvm-tcg-results"
set -o pipefail
ssh -p 2222 root@127.0.0.1 'python3 /tmp/cpu-bench.py' \
  2>&1 | tee "$HOME/kvm-tcg-results/kvm-1.txt"
```

Choose either method for the measurement; do not run both simultaneously. With SSH, the script still executes in L2, but the output file is saved in L1.

Record the five elapsed times, median, and checksums. Then shut down **inside L2**:

```bash
poweroff
```

Wait for QEMU to exit before starting the TCG guest.

> The disk uses `snapshot=on`: guest filesystem changes are discarded when QEMU exits. The script and newly installed packages may need to be recreated after each launch. Logs saved in L1 remain available.

## Step 4 — Boot with TCG

Run in **L1 terminal A**. Only the accelerator changes:

```bash
qemu-system-x86_64 \
  -accel tcg -cpu qemu64 -smp 1 -m 1G \
  -drive file="$HOME/kvm-demo/debian-base.qcow2",if=virtio,snapshot=on \
  -netdev user,id=n0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device virtio-net-pci,netdev=n0 \
  -nographic \
  -name accel-demo,debug-threads=on
```

Boot may take longer. Do not prescribe a fixed boot-time ratio: it depends on the environment and guest startup work.

Inside L2, repeat **Step 2 exactly**, including recreating the script. Allow startup and installation activity to settle.

Run the measurement directly in L2, or capture it from **L1 terminal B**:

```bash
set -o pipefail
ssh -p 2222 root@127.0.0.1 'python3 /tmp/cpu-bench.py' \
  2>&1 | tee "$HOME/kvm-tcg-results/tcg-1.txt"
```

Record the output, then shut down inside L2:

```bash
poweroff
```

## Step 5 — Compare the results

| Measurement | KVM | TCG |
| --- | ---: | ---: |
| Run 1 elapsed time (s) | | |
| Run 2 elapsed time (s) | | |
| Run 3 elapsed time (s) | | |
| Run 4 elapsed time (s) | | |
| Run 5 elapsed time (s) | | |
| Median elapsed time (s) | | |
| Checksum | | |

The checksum should match across every run and both modes. Calculate:

```text
TCG slowdown = median TCG elapsed time / median KVM elapsed time
```

For example, **hypothetical** medians of 12 seconds with TCG and 3 seconds with KVM give a slowdown of 4: TCG takes four times as long for this workload.

Expect KVM to generally complete CPU-intensive work faster, but report measured values rather than a predefined ratio. Keep the iteration count identical. If you change it to adjust classroom duration, rerun both modes.

For a stronger comparison, repeat complete sessions in alternating order (KVM, TCG, KVM, TCG), using distinct filenames such as `kvm-2.txt` and `tcg-2.txt`. Five runs within one session do not capture all between-session variability.

## Optional observation — QEMU CPU usage

While the guest benchmark is running, in another **L1 terminal**:

```bash
PID=$(pgrep -n -f 'qemu-system-x86_64.*accel-demo')
ps -T -p "$PID" -o tid,psr,comm
top -H -p "$PID"
```

Refresh `PID` after every QEMU launch. Typical vCPU thread names include `CPU 0/KVM` and `CPU 0/TCG`.

Observe several updates. Both modes may show a busy vCPU thread: direct guest execution under KVM still consumes CPU time associated with the vCPU thread. Low usage at guest idle is not evidence that KVM execution is unaccounted for.

Use this as a separate observation, or monitor both modes consistently. Press `q` to exit top.

## Interpretation and limitations

- The result describes this Python workload, CPU model, QEMU version, and environment; it is not a universal KVM/TCG performance ratio.
- CPU affinity is intentionally unrestricted. The scheduler may move execution between CPUs, and unrelated tasks can interfere.
- In OpenStack, this is **nested KVM versus TCG inside L1**. Physical-host scheduling, oversubscription, and other tenants can affect the results.
- The common CPU model reduces feature differences, but does not make all execution characteristics identical.
- These elapsed times measure workload completion, not wake-up latency, deadline guarantees, or boot duration.

## Connection to the next experiment

This experiment changes the execution mechanism: KVM versus TCG.

Next ([experiment 02](../02-compare-vanilla-vs-full-isolation/README.md)), keep KVM enabled and introduce CPU affinity, background stress, and the vanilla-versus-isolation comparison. Affinity controls **where a thread may execute**. CPU isolation reduces normal background scheduling on selected CPUs. Neither, by itself, guarantees that an OpenStack instance receives uninterrupted physical CPU time.

## References

- [QEMU accelerator and CPU options](https://qemu-project.gitlab.io/qemu/system/invocation.html)
- [QEMU TCG translator internals](https://www.qemu.org/docs/master/devel/tcg.html)
- [Bash timing semantics](https://www.gnu.org/software/bash/manual/html_node/Pipelines.html)
