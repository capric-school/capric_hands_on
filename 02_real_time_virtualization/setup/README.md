# Environment setup

Everything you need to run the experiments. Estimated time: ~15 minutes plus the
2–3 minutes the image build takes on its own.

If you already followed the live demo in class, the only thing you really need is the
**guest image** built by `build-image.sh`. The rest of this document is the full
reproduction recipe.

---

## 1. Host requirements

- **CPU with hardware virtualization** (Intel VT-x or AMD-V) **enabled in BIOS/UEFI**.
  Many laptops ship with it disabled.
- **Linux distribution** with kernel ≥ 5.15. The lab was developed on Linux Mint 22.3
  with kernel 6.8 and 6.17, but any recent Ubuntu/Debian/Fedora/Arch works.
- **At least 4 cores and 8 GiB of RAM.** More is better for the multi-VM experiments.
- **Bare metal** is suggested since nested virtualization (running the KVM-backed VM in another VM) can unreliable for RT measurement without any tuning at host level. However, in this hands-on session we still use **OpenStack VM** for just understanding real-time virtualization mechanisms provided by Linux/KVM.
- **About 8 GiB of free disk space** for the base image plus snapshots.

### 1.1 Verify hardware virtualization

```bash
# AMD: should print 'svm'; Intel: should print 'vmx'
grep -Eo 'svm|vmx' /proc/cpuinfo | sort -u
```

If nothing prints, enable virtualization in BIOS/UEFI and retry. Many vendors label
it "SVM Mode" (AMD), "Intel Virtualization Technology" / "VT-x", or just
"Virtualization".

### 1.2 Verify the KVM modules are loaded

```bash
lsmod | grep kvm
# Expect on AMD:   kvm_amd   ...   kvm   ...
# Expect on Intel: kvm_intel ...   kvm   ...
```

If they're not loaded, `sudo modprobe kvm_amd` or `sudo modprobe kvm_intel`. They
should auto-load on boot once the BIOS setting is correct.

### 1.3 Verify `/dev/kvm` is accessible to your user

```bash
ls -l /dev/kvm
# expect: crw-rw---- 1 root kvm ... /dev/kvm
```

Add yourself to the `kvm` group, then re-login or run `newgrp`:

```bash
sudo usermod -aG kvm $USER
newgrp kvm     # take effect in the current shell without logging out
```

---

## 2. Install packages

On Debian, Ubuntu, or Mint:

```bash
sudo apt update
sudo apt install -y \
    qemu-system-x86 qemu-utils \
    libvirt-daemon-system libvirt-clients virtinst virt-viewer \
    cloud-image-utils \
    cpu-checker bridge-utils \
    linux-tools-$(uname -r) linux-tools-generic \
    rt-tests stress-ng htop sysstat numactl iperf3 \
    cgroup-tools build-essential sshpass
```

What each group is for:

| Package(s)                                   | Purpose                                                |
| -------------------------------------------- | ------------------------------------------------------ |
| `qemu-system-x86`, `qemu-utils`              | The emulator/virtualizer and image tools (`qemu-img`)  |
| `libvirt-daemon-system`, `libvirt-clients`   | The libvirt daemon and `virsh` CLI (Exp 03)            |
| `virtinst`, `virt-viewer`                    | `virt-install` and the graphical console viewer        |
| `cloud-image-utils`                          | `cloud-localds` to build the cloud-init seed ISO       |
| `linux-tools-*`                              | `perf` (Exp 04)                                        |
| `rt-tests`                                   | `cyclictest`, the RT-latency yardstick                 |
| `stress-ng`                                  | Synthetic CPU/memory load for interference scenarios   |
| `cgroup-tools`                               | cgroup CLI helpers (Exp 07)                            |
| `build-essential`                            | gcc + make for Exp 06 (sched_deadline) and Exp 08      |
| `sshpass`                                    | simple tool to avoid writhing ssh password every time  |


## 3. Build the demo guest image

A single small Debian 12 image, baked once with cloud-init, used in **snapshot mode**
by every experiment so it never gets modified.

```bash
cd <this-repo>
./setup/build-image.sh
```

The script:

1. Downloads `debian-12-genericcloud-amd64.qcow2` (~350 MB) into `~/kvm-demo/`.
2. Resizes it to 5 GiB.
3. Builds a `seed.iso` containing the cloud-init config (see `setup/user-data`).
4. Boots the image once, attached to the seed ISO. cloud-init installs packages 
   (`rt-tests`, `stress-ng`, `iperf3`, `htop`, `mbw`, `sysstat`, `numactl`, 
   `build-essential`), sets the root password, and enables SSH.
5. When you see the `guest login:` prompt, log in as `root` / `demo`, then run `cloud-init status --wait`, wait for it to respond with `status: done` and then run `poweroff`.
6. Copies the now-baked image to `~/kvm-demo/debian-base.qcow2`.

After this, the experiments use the base image read-only (`-drive snapshot=on`).
Every boot is a fresh, identical guest; no state leaks between experiments.

> The image lives under `~/kvm-demo/`. The lab references it via the `IMAGE`
> environment variable, defaulting to `$HOME/kvm-demo/debian-base.qcow2`. To use
> a different path, `export IMAGE=/wherever/your/image.qcow2` before running the
> helper scripts in `/scripts`.

---

## 4. Verify the setup

A quick smoke test:

```bash
./scripts/vm1.sh
# wait ~15 seconds for boot; login: root / demo
# inside the guest, run:
uname -a
exit  # or: poweroff
```

If you see a Debian guest boot to a login prompt and you can log in, the lab is
ready.

In another terminal while the VM is running:

```bash
ps -L -p "$(pgrep -nf 'name vm1')" -o pid,tid,psr,comm | head
# you should see threads named 'qemu-system-x86', 'CPU 0/KVM', 'CPU 1/KVM', etc.
```

If you see the `CPU N/KVM` thread names, everything is in place. Now go to
[experiment 01](../01-first-vm-kvm-vs-tcg/README.md) and start.

---

## 6. Troubleshooting

**`/dev/kvm: permission denied`**
You aren't in the `kvm` group. Re-run `sudo usermod -aG kvm $USER` and
`newgrp kvm` (or just log out and back in).

**`Could not access KVM kernel module: No such file or directory`**
Modules not loaded. `sudo modprobe kvm_amd` (or `kvm_intel`). If that errors,
virtualization is disabled in BIOS.

**`cloud-localds: command not found`**
Install `cloud-image-utils` from package management.

**The image build hangs at "Booting from Hard Disk..."**
The cloud image is waiting for cloud-init metadata. Make sure the `seed.iso` is
attached as the second `-drive`. The provided `build-image.sh` handles this.

**Boot is fine but `apt install` inside cloud-init fails with network errors**
The default `-netdev user` gives the guest a NAT'd network with DNS. Check that
your host has internet. Behind a corporate proxy, you may need to set
`http_proxy`/`https_proxy` in the cloud-init `runcmd`.

**`virt-install` complains about libvirtd not running**
`sudo systemctl start libvirtd`, then verify `virsh list` works without errors.

**Everything works but cyclictest numbers are awful**
Either the RT kernel command line isn't applied (`cat /proc/cmdline`) or some
background process is monopolizing the isolated CPU. Close browsers and
electron-based editors before measurements.
