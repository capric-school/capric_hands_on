# CaPriC 2026 — Hands-on Labs

**2nd Capri PhD School on Cyber Physical Cloud (CaPriC)**  
**19–23 October 2026 · Anacapri, Capri Island, Italy**

[CaPriC 2026 website](https://capric-school.github.io/) · [School program](https://capric-school.github.io/#program)

Welcome to the hands-on repository for **CaPriC 2026**. It brings together practical material for exploring how Linux, virtual machines, and containers can support the timing and isolation requirements of **cyber-physical cloud** systems.

The exercises complement the school lectures. Follow the modules in order if you are new to the environment, or use their individual guides to reproduce experiments after the school.

## Lab modules

| # | Module | What you will explore | Start here |
|---|---|---|---|
| 00 | **OpenStack VM** | Accessing the cloud environment; SSH keys, networking, security groups, and virtual machine provisioning. | [OpenStack setup guide](00_openstack_vm/README.md) |
| 01 | **Real-time Linux** | Linux scheduling, preemption, CPU isolation, and the measurement of scheduling latency. | [Real-time Linux](01_real_time_linux/README.md) |
| 02 | **Real-time virtualization** | QEMU/KVM fundamentals, KVM versus software emulation (TCG), and vanilla versus isolated host configurations. | [Real-time Virtualization](02_real_time_virtualization/README.md) |
| 03 | **Real-time containers** | Container-based deployment and the challenges of resource isolation and timing predictability. | [Real-time Containers](03_real_time_containers/README.md) |

## Getting started

### 1. Obtain the repository

Clone it on the **Linux machine where you will perform the exercises** (for example, your assigned lab VM):

```bash
git clone https://github.com/capric-school/capric_hands_on.git
cd capric_hands_on
```

Alternatively, use GitHub's **Code → Download ZIP** option and extract the archive on the machine where you will work.

To get the latest instructions later:

```bash
git pull --ff-only
```

### 2. Access the lab environment

If you are using a school-provided OpenStack VM, begin with [Module 00](00_openstack_vm/README.md). It explains the cloud dashboard, SSH access, networking, and instance creation.

You will need a web browser and an SSH client. macOS/Linux Terminal and Windows PowerShell with OpenSSH are suitable for connecting to a Linux VM. The Linux-only experiment commands should run **inside the lab VM**, not on your laptop's operating system.

**Accounts and credentials:** Access details are provided by the organizers through the designated private channel. Do not commit passwords, private SSH keys, access tokens, or unencrypted account lists to this public repository.

## Questions and feedback

For issues with the material, you can [open a GitHub issue](https://github.com/capric-school/capric_hands_on/issues) with the module name, your environment, the command you ran, and the relevant *sanitized* output. During the school, you can also ask the instructors directly.

For school information, visit [the official CaPriC website](https://capric-school.github.io/) or contact [capric-school@unina.it](mailto:capric-school@unina.it).

---

*CaPriC 2026 · Cyber Physical Cloud*
