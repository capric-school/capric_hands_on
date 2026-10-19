# Capri Kubernetes Real-Time Lab: Deploying Real-Time Applications on Kubernetes

## Lab environment and test application

Deploying real-time applications on Kubernetes with requirements on low and predictable latency requires a **multi-faceted approach**. You must optimize every layer, from the application code through the Kubernetes cluster configuration down to the underlying infrastructure. This lab aims to familiarize you with the different concepts for enabling real-time workloads using Kubernetes as the orchestration platform.

The lab runs on Ericsson's cloud computing platform, which uses OpenStack to manage compute, network, and storage resources. The infrastructure and virtualization layers are not optimized for real-time workloads, so you may experience some unwanted jitter, but the concepts presented here are still valid.

You will use a Kubernetes cluster built from 6 virtual machines, plus a support node, as illustrated below:

```
 Docker registry  API endpoint  Worker         Worker         Worker        Worker (RT)   Worker (RT)
 & file server
┌───────────┐     ┌──────────┐  ┌──────────┐   ┌─────────┐    ┌─────────┐   ┌─────────┐   ┌─────────┐
│ capri-1   │     │ capri-2  │  │ capri-3  │   │ capri-4 │    │ capri-5 │   │ capri-6 │   │ capri-7 │
└──┬──┬─────┘     └──┬─┬─────┘  └────┬─────┘   └─────┬───┘    └────┬────┘   └────┬────┘   └────┬────┘
   │  │172.16.0.218  │ │172.16.0.243 │172.16.0.182   │172.16.0.232 │172.16.0.90  │172.16.0.147 │172.16.0.146     
   │  └────────────────┴─────────────┴───────────────┴─────────────┴─────────────┴─────────────┘
   │129.192.83.120   │129.192.70.227
```

**Node roles:**

| Node | Role |
|------|------|
| **capri-1** | Docker registry and lab file server (not part of the Kubernetes cluster). Hosts the container images used by the exercises and serves the deployment manifests and kubeconfig file on port 8080. |
| **capri-2** | The node your `kubectl` talks to: it is the only cluster node with an external address, so the kubeconfig points at it. It also runs workloads like any other node. |
| **capri-3 – capri-5** | Non-real-time nodes. These run a standard kernel and are used in Exercises 1–3. |
| **capri-6 – capri-7** | Real-time nodes. These run a PREEMPT_RT kernel with isolated cores, are labelled `policy=rt` and tainted `rt-workload=true:NoSchedule`, and are used in Exercises 4–6. |

**Networks:**

| Network | Purpose |
|---------|---------|
| **129.192.x.x** (external) | Ericsson corporate network. Only capri-1 (`129.192.83.120`) and capri-2 (`129.192.70.227`) have addresses on this network. You use capri-1's external address to download lab files and the kubeconfig. |
| **172.16.0.x** (internal) | Internal cluster network connecting all nodes. All inter-node Kubernetes traffic (pod-to-pod, kubelet, API server) flows over this network. |

It will be used to deploy an application that measures the round-trip latency of UDP packets sent between a client and a server. As illustrated below, the client sends a UDP packet to the server, the server immediately echoes it back, and the client measures the round-trip time before sending the next packet. After a set number of iterations the client prints statistics from the run.

```
┌─────────┐             ┌───────────┐
│ Client  │             │  Server   │
└─────┬───┘             └───────┬───┘
      │        Request          │       
      │ ──────────────────────► │   ─   
      │                         │   │   
      │                         │   │ RTT  
      │        Response         │   │   
      │ ◄────────────────────── │   ─   
      │                         │       
```

## Preparations

You need to install **kubectl**, the command-line tool for communicating with and controlling a Kubernetes cluster. It sends commands to the Kubernetes API server to manage the full lifecycle of your applications and resources, such as deploying applications, inspecting cluster resources, and viewing logs. The following link describes how to install it: https://kubernetes.io/docs/tasks/tools/.

kubectl uses a file called kubeconfig, which is how kubectl determines which Kubernetes cluster to talk to and how to authenticate with it. The kubeconfig for the lab cluster can be downloaded with:

```
$ wget 129.192.83.120:8080/config
```

> **If `wget` cannot connect:** the lab file server on capri-1 port 8080 is
> started by the instructor and only runs during the exercises, so it is
> deliberately not available outside the session. Ask the instructor to start it
> rather than debugging your own network. Everything else in this lab — the
> Kubernetes API and the container registry — is unaffected.

Place it on your computer at `~/.kube/config` and verify that kubectl is properly configured by listing the cluster nodes:

```
$ kubectl get nodes
NAME      STATUS   ROLES                  AGE    VERSION
capri-2   Ready    control-plane,worker   312d   v1.32.13
capri-3   Ready    control-plane,worker   312d   v1.32.13
capri-4   Ready    control-plane,worker   312d   v1.32.13
capri-5   Ready    control-plane,worker   312d   v1.32.13
capri-6   Ready    control-plane,worker   312d   v1.32.13
capri-7   Ready    control-plane,worker   312d   v1.32.13
```

Your work will be done in a separate **namespace**. A namespace is a way to partition cluster resources among multiple users, allowing for the logical separation of resources such as pods, services, and deployments.

Using kubectl, create the namespace with:

```
$ kubectl create ns <your_name>
```

> The examples in this guide use the namespace `group1`. Replace `group1` with
> your own namespace name (the one you created above) in every `kubectl -n ...`
> command that follows.

-----

# Part 1 — Deploying on Kubernetes

> Do this part after the *"Introduction to Kubernetes"* section of the
> presentation. It only uses concepts from that section: deploying a pod,
> inspecting it with kubectl, and resource requests/limits and QoS classes.
> You will deploy the test application, take a baseline measurement, and then
> give it the Guaranteed QoS class.

## Realistic baseline: the non-real-time nodes are under load

A latency measurement taken on a completely idle cluster is misleading — in
production the nodes are always doing other work. To make the baseline
realistic, the **non-real-time** nodes (capri-2 … capri-5) already have a
background CPU load running on them (deployed by the instructor), so the nodes
that host the first exercises are busy rather than idle. The real-time nodes
(capri-6 and capri-7) are deliberately left clean.

Exercises 1–3 therefore run on these non-real-time nodes, so their measurements
reflect a busy, untuned host. Nothing in those manifests mentions a node: the
real-time nodes carry the taint `rt-workload=true:NoSchedule`, and the
Exercise 1–3 pods have no matching toleration, so the scheduler simply cannot
place them there. That is the taint doing its job — you will meet it properly in
Exercise 4, where you add the toleration that gets through it. Later exercises
move the workload onto the clean real-time nodes, which is where the improvement
from host-level tuning becomes visible.

The background load is the exception: it is a DaemonSet that tolerates every
taint (it has to, in order to run on these control-plane nodes at all), so it
*is* kept off capri-6/7 by an explicit node affinity rule rather than by the
taint.

## Exercise 1: Deploy the Application and Get Baseline Measurements

Each exercise comes with a ready-made deployment manifest in the `Deployments/`
directory on the lab file server. You do **not** need to edit any YAML
yourself — for every exercise you simply download the corresponding manifest,
read it to understand what changed compared to the previous step, deploy it,
and record the measurements. The explanations in each exercise describe the
change that the manifest already contains.

The `1-vanilla.yaml` manifest is the baseline: best-effort QoS, and no placement
rules at all beyond keeping the client and server on separate nodes — the taint
on capri-6/7 is what keeps it on the non-real-time nodes. Download it with:

```
$ wget 129.192.83.120:8080/Deployments/1-vanilla.yaml
```

It consists of:

  * **udpserver**: A UDP server deployed as a Deployment and fronted by a Kubernetes Service. The udpserver bounces incoming packets back to the sender and is started with `./udpserver 5000`, where the first argument is the port to listen on. It also accepts two optional arguments used in the real-time exercises: `rt` (run under `SCHED_FIFO`) and `busy` (busy-poll the socket instead of blocking — see Exercise 4). As a guard against a wedged busy-poll loop the server exits by itself after 600 seconds — but its Deployment immediately restarts it, so this is **not** a substitute for cleaning up: a forgotten server keeps holding its core and just shows a climbing `RESTARTS` count. Always delete your deployments.
  * **udpclient**: A UDP client deployed as a Job. It sends a UDP packet, waits for the response, and measures the round-trip time, repeating for a set number of iterations. It is started with `./udpclient udpserver-service 5000 100000`, where the first argument is the destination address, the second the destination port, and the third the number of iterations. It also accepts the optional `rt` and `busy` arguments. 100000 iterations take roughly 90 seconds. The client waits at most 500 ms for each reply, so a dropped datagram is counted as lost and reported rather than stalling the run.


Deploy the application in your namespace with:

```
$ kubectl -n group1 create -f 1-vanilla.yaml 
```

And check the status of the deployment with:

```
$ kubectl -n group1 get pods -o wide
NAME                         READY   STATUS      RESTARTS   AGE    IP           NODE      NOMINATED NODE   READINESS GATES
udpclient-6mq92              0/1     Completed   0          3m     10.1.1.175   capri-3   <none>           <none>
udpserver-69b84c77cd-pddw5   1/1     Running     0          3m     10.1.3.184   capri-5   <none>           <none>
```

The **udpclient** is deployed as a Kubernetes Job and runs until completion; its status should become **Completed** when finished. The `-o wide` argument also shows the node where each container was deployed; take note of this. Once the **udpclient** has completed, you can extract the statistics from its measurements. The **udpclient** prints the statistics to **stdout**, so you can fetch them with:

```
$ kubectl -n group1 logs udpclient-6mq92

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 206.000
Max: 21605.000
P50: 290.000
P70: 339.000
P90: 484.000
P99: 1664.000
P99,9: 3348.000
Samples: 100000 of 100000 requested
```
Take note of these numbers, as they will serve as a **baseline** for future measurements. The numbers mean the following:

| Percentile | What it means                                |
| ---------- | -------------------------------------------- |
| Min        | The minimum measured value.                  |
| Max        | The maximum measured value.                  |
| P50        | 50% of requests are faster than this value.  |
| P70        | 70% of requests are faster than this value.  |
| P90        | 90% of requests are faster than this value.  |
| P99        | 99% of requests are faster than this value.  |
| P99,9      | 99.9% of requests are faster than this value.|

Presenting latency as percentiles is useful because they reveal the entire distribution of response times, particularly the crucial "tail latency" (the worst-case performance experienced by a fraction of requests), which is hidden when using averages.

You can now delete the deployment with:

```
$ kubectl -n group1 delete -f 1-vanilla.yaml
```

And verify that it's removed:

```
$ kubectl -n group1 get pods
```

-----

## Exercise 2: Deploy the Application with Guaranteed Quality of Service (QoS)

Kubernetes classifies pods into specific Quality of Service (QoS) classes—**Guaranteed**, **Burstable**, and **BestEffort**—based on the resource requests and limits of the containers within the pod. This classification influences how pods are handled, particularly under **node pressure** (when a node runs out of resources). In that situation, Kubernetes evicts **BestEffort** pods first, then **Burstable**, and finally **Guaranteed** pods.

Setting resource requests and limits is a key part of ensuring stability and predictable performance for latency-sensitive applications.

  * **Requests** define the **minimum** amount of resources (CPU and memory) a container needs. The Kubernetes scheduler uses this value to decide which node to place the pod on.
  * **Limits** define the **maximum** amount of resources a container can use. If a container exceeds its memory limit, the operating system will terminate it. If it exceeds its CPU limit, the kernel will throttle its CPU usage, which can significantly impact performance and latency.

The **Guaranteed** QoS class is the highest QoS class and is essential for real-time applications because it provides the strongest guarantees for resource availability. You achieve this class when you set a container's requests **equal to** its limits for both CPU and memory.

Here is an example specification:

```
    spec:
      containers:
      - name: your-real-time-app
        image: your-real-time-app-image:v1.0
        resources:
          requests:
            cpu: "1"      # Requests 1 full CPU core
            memory: "1Gi"  # Requests 1 GiB of memory
          limits:
            cpu: "1"      # Limits CPU usage to 1 core
            memory: "1Gi"  # Limits memory usage to 1 GiB
```

The `2-guaranteed.yaml` manifest is the baseline with `requests` set equal to `limits` for both the client and the server, giving them the **Guaranteed QoS** class. Download it and deploy it to get new measurements:

```
$ wget 129.192.83.120:8080/Deployments/2-guaranteed.yaml
$ kubectl -n group1 create -f 2-guaranteed.yaml
```

Wait for the client to finish, then fetch its logs and take note of the numbers:

```
$ kubectl -n group1 get pods -o wide
NAME                         READY   STATUS      RESTARTS   AGE   IP           NODE      NOMINATED NODE   READINESS GATES
udpclient-7g7qq              0/1     Completed   0          80s   10.1.0.159   capri-2   <none>           <none>
udpserver-7fb7587fb4-z6rnd   1/1     Running     0          80s   10.1.3.175   capri-5   <none>           <none>

$ kubectl -n group1 logs udpclient-7g7qq

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 217.000
Max: 11762.000
P50: 301.000
P70: 342.000
P90: 456.000
P99: 1711.000
P99,9: 3341.000
Samples: 100000 of 100000 requested
```

You can check the QoS class Kubernetes assigned:

```
$ kubectl -n group1 get pod udpclient-7g7qq -o jsonpath='{.status.qosClass}'
Guaranteed
```

Note that the median is essentially unchanged from Exercise 1. The Guaranteed
class does not give the pod a dedicated core here — these non-real-time nodes do
not run the CPU Manager's static policy, so the pod still shares the node's CPUs
with the background load. What Guaranteed buys you at this stage is priority
under pressure: it is the last class to be throttled or evicted. Getting a
dedicated core needs the node-level configuration you will meet in Exercise 4.

And delete the deployment:

```
$ kubectl -n group1 delete -f 2-guaranteed.yaml
```

-----

# Part 2 — Tuning for Real-Time

> Do this part after the *"Making Kubernetes Deterministic"* section of the
> presentation. These exercises apply the real-time techniques from that
> section: real-time scheduling inside the pod, steering pods onto tuned
> real-time nodes, controlling placement, and bypassing the pod network.
> You will continue from the Guaranteed-QoS deployment you ran in Part 1 and
> keep improving its latency and determinism, step by step.

## Exercise 3: Schedule the Client and Server with a Real-Time Scheduling Policy

You can set a **real-time scheduling policy** for a process using the `chrt` command or from the application code using the `sched_setscheduler` API. References:

- https://man7.org/linux/man-pages/man1/chrt.1.html
- https://man7.org/linux/man-pages/man2/sched_setscheduler.2.html

Here is an example of setting the scheduling policy to **SCHED_FIFO** from application code:

```
struct sched_param sp = { .sched_priority = 50 };
int ret;

ret = sched_setscheduler(0, SCHED_FIFO, &sp);
if (ret == -1) {
  perror("sched_setscheduler");
  return 1;
}
```

It can also be done when starting the application:

```
chrt -f 50 ./udpserver 5000
```

To avoid recompiling the applications and rebuilding the Docker image for this exercise, you'll use the `chrt` command to schedule the client and the server with the **fifo** policy on priority **50**.

Linux's real-time scheduling policies (**SCHED_FIFO** and **SCHED_RR**) give processes a high level of priority and control over CPU time. If any unprivileged process could set itself to a high real-time priority, it could starve critical system processes and make the machine unresponsive. Linux therefore gates these policies behind the **SYS_NICE** capability, which a container does not get by default.

In Kubernetes you grant a single capability through the container's `securityContext`:

```
      containers:
        - name: udpclient
          ...
          securityContext:
            capabilities:
              add: ["SYS_NICE"]
```

The `3-schedfifo.yaml` manifest builds on the Guaranteed deployment and already
contains these two changes: the client and server are started with `chrt -f 50`
(SCHED_FIFO, priority 50), and each container is granted the `SYS_NICE`
capability. Download it, deploy it, and get the measurements from the client:

```
$ wget 129.192.83.120:8080/Deployments/3-schedfifo.yaml
$ kubectl -n group1 create -f 3-schedfifo.yaml
$ kubectl -n group1 get pods -o wide
NAME                         READY   STATUS      RESTARTS   AGE   IP           NODE      NOMINATED NODE   READINESS GATES
udpclient-t7x6w              0/1     Completed   0          73s   10.1.1.8     capri-3   <none>           <none>
udpserver-6bfd7c8777-7zl2l   1/1     Running     0          73s   10.1.0.51    capri-2   <none>           <none>

$ kubectl -n group1 logs udpclient-t7x6w

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 221.000
Max: 3290.000
P50: 284.000
P70: 300.000
P90: 332.000
P99: 460.000
P99,9: 928.000
Samples: 100000 of 100000 requested
```

Compare the **P99** with Exercise 2: the median barely moves (the pods still
share the node's CPUs with the background load), but the tail collapses — from
roughly 1500 µs to under 500 µs. That is the real-time scheduling policy at
work: when a packet arrives, the kernel preempts whatever ordinary task is
running and runs the udpserver immediately, instead of making it wait its turn.

You can confirm the policy actually took effect from inside the pod:

```
$ kubectl -n group1 exec <udpserver-pod> -- chrt -p 1
pid 1's current scheduling policy: SCHED_FIFO
pid 1's current scheduling priority: 50
```

Take note of the numbers and delete the deployment:

```
$ kubectl -n group1 delete -f 3-schedfifo.yaml
```

-----

## Exercise 4: Schedule the Client and Server on Real-Time Nodes

So far (Exercises 1–3) the workload has run on the non-real-time nodes, sharing
them with the instructor-deployed background load — not because those manifests
asked for those nodes, but because the taint on capri-6/7 left the scheduler
nowhere else to put them. In this exercise you move the workload onto the
dedicated real-time nodes, which are tuned for determinism and free of that
noise — so this is where host-level tuning should start to pay off. If you
run:

```
$ kubectl get nodes -o wide
NAME      STATUS   ROLES                  AGE    VERSION    INTERNAL-IP    EXTERNAL-IP   OS-IMAGE             KERNEL-VERSION        CONTAINER-RUNTIME
capri-2   Ready    control-plane,worker   312d   v1.32.13   172.16.0.243   <none>        Ubuntu 24.04.3 LTS   6.17.0-22-generic     containerd://1.6.39
capri-3   Ready    control-plane,worker   312d   v1.32.13   172.16.0.182   <none>        Ubuntu 24.04.3 LTS   6.17.0-23-generic     containerd://1.6.39
capri-4   Ready    control-plane,worker   312d   v1.32.13   172.16.0.232   <none>        Ubuntu 24.04.3 LTS   6.8.0-106-generic     containerd://1.6.39
capri-5   Ready    control-plane,worker   312d   v1.32.13   172.16.0.90    <none>        Ubuntu 24.04.3 LTS   6.8.0-111-generic     containerd://1.6.39
capri-6   Ready    control-plane,worker   312d   v1.32.13   172.16.0.147   <none>        Ubuntu 24.04.3 LTS   6.8.1-1015-realtime   containerd://1.6.39
capri-7   Ready    control-plane,worker   312d   v1.32.13   172.16.0.146   <none>        Ubuntu 24.04.3 LTS   6.8.1-1015-realtime   containerd://1.6.39
```

This gives a listing of all nodes and their kernel version. As you can see, **capri-6** and **capri-7** use the kernel `6.8.1-1015-realtime` — Ubuntu's real-time kernel, built with the `PREEMPT_RT` patch set. This converts spinlocks to sleeping mutexes, makes interrupt handlers run as preemptible threads, and enables full kernel preemption — turning the standard Linux kernel into one with bounded worst-case latency (typically tens of microseconds rather than milliseconds).

In addition, these nodes have the following boot-time tunings applied:

* Cores 2–7 are isolated (using `isolcpus=2-7`) so that regular tasks are not scheduled on them. Cores 0–1 are left for the operating system and Kubernetes.
* Cores 2–7 are "full tickless" using `nohz_full=2-7`. This prevents the kernel timer tick from interrupting a process running on those cores.
* Cores 2–7 have RCU (Read-Copy-Update) callbacks offloaded using `rcu_nocb_poll=2-7`.
* `irqaffinity=0` routes device interrupts to core 0, keeping the isolated cores free of interrupt work.
* `intel_pstate=disable` stops the CPU frequency governor from drifting the clock, and `nosoftlockup` plus `skew_tick=1` remove two further sources of periodic kernel work.
* `tsc=reliable` tells the kernel that the TSC is a reliable clock source across all cores, preventing it from falling back to a less accurate and potentially more jittery clock.
* The RT bandwidth throttle is **disabled** (`kernel.sched_rt_runtime_us = -1`). By default Linux caps all `SCHED_FIFO`/`SCHED_RR` tasks at 95% of each second, so a runaway real-time thread cannot lock up the machine: once an RT task has used 950 ms of a 1 s window it is suspended until the next one. A busy-poll RT loop wants 100% of its core, so on capri-6 and capri-7 the cap is lifted — and that is **the only pair of nodes where it is**. capri-2…capri-5 keep the 95% default, which is part of what makes them the "untuned" nodes. This is why Exercises 4–6 can busy-poll at all: the same workload on a non-real-time node would be stalled for roughly 50 ms every second.

These settings are applied by the instructor when the nodes are built, and they
live on the kernel command line, so they are not visible through `kubectl` — take
them as given. What you *can* see is the kernel they result in, in the
`KERNEL-VERSION` column of `kubectl get nodes -o wide` above: `6.8.1-1015-realtime`
on capri-6 and capri-7 versus a standard kernel everywhere else.

> **Consequence of the lifted RT cap:** on the real-time nodes a busy-poll
> `SCHED_FIFO` process genuinely owns its core — nothing else gets to run there.
> That is exactly what you want for real time, but it means `kubectl exec` into a
> running Exercise 4–6 **server** pod will appear to hang: the shell you start is
> a normal (`SCHED_OTHER`) process, and it never gets scheduled on the saturated
> core. Don't try to shell into those pods; read the client's log instead.

With the cores isolated at the OS level, the kubelet running on these nodes has also been configured to use these cores for its pods. This is done using the kubelet CPU Manager and its **static policy** (https://kubernetes.io/docs/tasks/administer-cluster/cpu-management-policies/). The CPU Manager's static policy grants exclusive CPUs to containers that are both part of a Guaranteed pod and request integer CPUs. To enable this feature, the kubelet on these nodes has been given the following configuration:

```
cpuManagerPolicy: static
reservedSystemCPUs: "0-1"
```

You can confirm this yourself with `kubectl` — the kubelet serves its running
configuration through the API server, so no access to the node is needed:

```
$ kubectl get --raw "/api/v1/nodes/capri-6/proxy/configz" | grep -o '"cpuManagerPolicy":"[^"]*"'
"cpuManagerPolicy":"static"

$ kubectl get --raw "/api/v1/nodes/capri-6/proxy/configz" | grep -o '"reservedSystemCPUs":"[^"]*"'
"reservedSystemCPUs":"0-1"
```

Try the same against a non-real-time node and you get the contrast directly —
which is exactly why Exercise 2's Guaranteed pod did not get a dedicated core:

```
$ kubectl get --raw "/api/v1/nodes/capri-3/proxy/configz" | grep -o '"cpuManagerPolicy":"[^"]*"'
"cpuManagerPolicy":"none"
```

The `cpuManagerPolicy: static` setting enables the static policy. `reservedSystemCPUs: "0-1"` reserves cores 0 and 1 for system and Kubernetes management processes (containerd, kubelet, etc.), leaving cores 2–7 as the exclusive pool for real-time pods. Two housekeeping cores (rather than one) keep the container runtime responsive while busy-poll pods spin on the isolated cores.

**Node scheduling** is the process of placing containers on specific nodes in Kubernetes. While the Kubernetes scheduler automatically assigns a pod to a suitable node, you can influence this decision.

The **capri-6** and **capri-7** nodes are configured for real-time workloads and are labeled with `policy=rt`. All labels can be shown with:

```
$ kubectl get nodes --show-labels
NAME      STATUS   ROLES                  AGE    VERSION    LABELS
capri-2   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-2,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-2,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=
capri-3   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-3,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-3,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=
capri-4   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-4,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-4,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=
capri-5   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-5,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-5,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=
capri-6   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-6,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-6,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=,policy=rt
capri-7   Ready    control-plane,worker   312d   v1.32.13   beta.kubernetes.io/arch=amd64,beta.kubernetes.io/os=linux,hostname=capri-7,k8sd.io/role=control-plane,kubernetes.io/arch=amd64,kubernetes.io/hostname=capri-7,kubernetes.io/os=linux,node-role.kubernetes.io/control-plane=,node-role.kubernetes.io/worker=,policy=rt
```

In this exercise you will deploy the udpserver and udpclient on separate nodes labeled with `policy=rt`. Labels are arbitrary key-value pairs attached to nodes (or any Kubernetes object). They are added with:

```
$ kubectl label node <node-name> <key>=<value>
```

For example, the lab's real-time nodes were labeled with:

```
$ kubectl label node capri-6 policy=rt
$ kubectl label node capri-7 policy=rt
```

A label on its own only *attracts* pods that ask for it — it does nothing to keep
other pods away. So the real-time nodes are also **tainted**:

```
$ kubectl taint node capri-6 rt-workload=true:NoSchedule
$ kubectl taint node capri-7 rt-workload=true:NoSchedule
```

A taint is a fence: the scheduler refuses to place a pod on a tainted node unless
that pod carries a matching **toleration**. This is what keeps the Exercise 1–3
workloads — and any stray pod — off the real-time nodes. The label is the *pull*,
the taint is the *fence*, and the toleration is your pod's key through it. You
need the label and the toleration together: the toleration alone gets you through
the fence but does not steer you onto the pool, and the label alone is now blocked
by the fence.

To do the scheduling, you need to combine three Kubernetes scheduling features in your deployment specification:
1. **Node Affinity** to select only the nodes with the label `policy=rt`.
2. **Pod Anti-Affinity** to prevent the client and server pods from being scheduled on the same node.
3. **A Toleration** for `rt-workload`, so the scheduler is permitted to place the pod on a tainted real-time node.

Here is an example of a pod spec using **nodeAffinity** and **podAntiAffinity**:

```
apiVersion: batch/v1
kind: Job
metadata:
  name: udpclient
  labels:
    app: udpclient
spec:
  template:
    metadata:
      labels:
        app: udpclient
    spec:
      affinity:
        # Node Affinity: REQUIRED to deploy ONLY on nodes with policy=rt
        nodeAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
            nodeSelectorTerms:
            - matchExpressions:
              - key: policy
                operator: In
                values:
                - rt
        
        # Pod Anti-Affinity: REQUIRED to prevent co-location with the udpserver
        podAntiAffinity:
          requiredDuringSchedulingIgnoredDuringExecution:
          - labelSelector:
              matchLabels:
                app: udpserver # <-- Avoid being deployed with the udpserver
            topologyKey: "kubernetes.io/hostname"

      # Toleration: the key through the fence on capri-6 and capri-7
      tolerations:
      - { key: rt-workload, operator: Exists, effect: NoSchedule }
```

The `4-rtnodes.yaml` manifest already applies this: it adds the `nodeAffinity`,
`podAntiAffinity` and `tolerations` above to both the client and the server.
Download it, deploy it, verify that the placement is on **capri-6** and
**capri-7**, and get the measurements:

```
$ wget 129.192.83.120:8080/Deployments/4-rtnodes.yaml
$ kubectl -n group1 create -f 4-rtnodes.yaml

$ kubectl -n group1 get pods -o wide
NAME                         READY   STATUS      RESTARTS   AGE   IP           NODE      NOMINATED NODE   READINESS GATES
udpclient-q68xk              0/1     Completed   0          81s   10.1.9.159   capri-6   <none>           <none>
udpserver-7568c74bb8-dc5vb   1/1     Running     0          81s   10.1.10.67   capri-7   <none>           <none>

kubectl -n group1 logs udpclient-q68xk

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 154.000
Max: 12479.000
P50: 199.000
P70: 210.000
P90: 233.000
P99: 331.000
P99,9: 1392.000
Samples: 100000 of 100000 requested
```

Compared with Exercise 3 on the busy, untuned nodes, the median and the tail
(P99) both improve: the workload now has an isolated core to itself, busy-polls
so the vCPU stays hot, and shares the node with no background noise. (The
occasional multi-millisecond **Max** is a residual artifact of running on a KVM
guest — the hypervisor can still rarely deschedule a vCPU — but the P50/P99 are
clearly better and stable.)

### Did it really get an exclusive core?

Real-time configuration fails **silently** — the application still runs, just
with jitter — so it is worth confirming rather than assuming.

Two of the three preconditions you can check yourself with `kubectl`. First, that
the node runs the static CPU Manager policy (shown above via `configz`). Second,
that the pod actually qualifies for an exclusive core, which needs Guaranteed QoS
*and* a whole-number CPU request:

```
$ kubectl -n group1 get pod <udpserver-pod> -o jsonpath='{.status.qosClass}{"\n"}'
Guaranteed

$ kubectl -n group1 get pod <udpserver-pod> -o jsonpath='{.spec.containers[0].resources}{"\n"}'
{"limits":{"cpu":"1","memory":"1Gi"},"requests":{"cpu":"1","memory":"1Gi"}}
```

Guaranteed with `cpu: "1"` — an integer, not `500m` — is what makes the pod
eligible. A Burstable pod, or a Guaranteed pod asking for a fractional CPU such
as `500m`, stays on the shared pool no matter how the node is tuned.

The third part, *which* core was handed out, is recorded by the kubelet on the
node itself in `/var/lib/kubelet/cpu_manager_state`. That file is not reachable
through `kubectl`, so you cannot read it from your laptop — but it is worth seeing
what it contains. For a run of this exercise:

```
{"policyName":"static",
 "defaultCpuSet":"0-1,3-7",
 "entries":{"fc2b0286-...":{"udpserver":"2"}}}
```

Three things to read from this:

* `"policyName":"static"` — the static CPU Manager policy is active on this node.
* `{"udpserver":"2"}` — the udpserver container was given core **2**, exclusively.
* `"defaultCpuSet":"0-1,3-7"` — core 2 has been **removed** from the shared pool,
  so no other container can be scheduled onto it.

Note that cores 0–1 remain in the shared pool: those are the `reservedSystemCPUs`,
left for the kubelet and the rest of the system, and the exclusive cores are only
ever carved out of the remainder.

And delete the deployment:

```
$ kubectl -n group1 delete -f 4-rtnodes.yaml
```

-----

## Exercise 5: Host Networking

**Host networking** is a Kubernetes networking mode where a pod directly uses the network stack of the node it's running on, rather than having its own isolated network namespace and IP address. The pod shares the node's IP address and can access all of the node's network interfaces. This configuration is enabled by setting `hostNetwork: true` in the pod specification.

Here is an example:

```
apiVersion: v1
kind: Pod
metadata:
  name: my-pod
spec:
  hostNetwork: true
  containers:
  - name: my-container
    image: my-image
    ports:
    - containerPort: 80
```

In this exercise, host networking is used to illustrate the cost of the extra networking layers in Kubernetes. By keeping the client and server on **separate** real-time nodes (as in Exercise 4) but bypassing the pod overlay, you can see how much latency the overlay adds. While host networking offers performance benefits, it also has significant downsides:

  * **Security risks**: The pod shares the host's network namespace and can access any network service or port on the host machine, breaking Kubernetes' network isolation model.
  * **Port conflicts**: Only one pod using host networking can bind to a given port on a node at a time.
  * **Loss of portability**: Pods using host networking are tied to a specific node's IP address, making them less portable.

The `5-hostnetwork.yaml` manifest enables host networking (`hostNetwork: true`)
for both the client and the server. Because they now use the node's network
stack instead of the pod overlay, the manifest places the server on **capri-6**
and the client on **capri-7** with `nodeName`, and the client is configured to
send directly to the server node's IP address (`172.16.0.147`) rather than the
Service name.

> **Pick a unique port if others are running this at the same time.** With host
> networking the server binds the port directly on the node, and only one
> process can bind a given UDP port per node. If several groups run this
> exercise concurrently you must **each choose a different port**. After
> downloading the manifest, edit it and change every occurrence of `5000` to a
> unique value in the range **5000–5999** (a good choice is `5000 +` your group
> number, e.g. group 7 → `5007`). There are three places, and they must all
> match: the server's `args` (`./udpserver <port> ...`), the server's
> `containerPort`, and the client's `args` (`./udpclient 172.16.0.147 <port> ...`).
> If you are the only one running it, the default `5000` is fine.

Download it, (optionally edit the port,) deploy it, and get the measurements:

```
$ wget 129.192.83.120:8080/Deployments/5-hostnetwork.yaml
$ kubectl -n group1 create -f 5-hostnetwork.yaml

$ kubectl -n group1 get pods -o wide
NAME                         READY   STATUS      RESTARTS   AGE   IP             NODE      NOMINATED NODE   READINESS GATES
udpclient-2z8dl              0/1     Completed   0          80s   172.16.0.146   capri-7   <none>           <none>
udpserver-54f847c4b8-9jhf2   1/1     Running     0          80s   172.16.0.147   capri-6   <none>           <none>

$ kubectl -n group1 logs udpclient-2z8dl

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 130.000
Max: 9906.000
P50: 137.000
P70: 150.000
P90: 178.000
P99: 214.000
P99,9: 743.000
Samples: 100000 of 100000 requested
```

Compare these numbers with Exercise 4 (same two nodes, but over the pod overlay
network): bypassing the overlay measurably reduces the round-trip latency
(here P50 drops from ~200 µs to ~137 µs).

And delete the deployment:

```
$ kubectl -n group1 delete -f 5-hostnetwork.yaml
```

-----

## Exercise 6: Schedule the Client and Server on the Same Node

So far the client and server have always run on different nodes, so every
packet crosses the network. The biggest latency reduction comes from removing
the network from the path entirely: place both pods on the **same** node, where
they communicate over the node-local loopback instead of the wire.

To force two pods onto the same node — without hard-coding *which* node, so that
several groups spread across the real-time nodes instead of all piling onto one
— use **pod affinity** (the mirror image of the anti-affinity from Exercise 4).
The client requires the scheduler to place it on a node that already runs a pod
labeled `app=udpserver`, combined with the `policy=rt` node affinity:

```
apiVersion: batch/v1
kind: Job
metadata:
  name: udpclient
  labels:
    app: udpclient
spec:
  template:
    metadata:
      labels:
        app: udpclient
    spec:
      affinity:
        nodeAffinity:            # only real-time nodes
          requiredDuringSchedulingIgnoredDuringExecution:
            nodeSelectorTerms:
            - matchExpressions:
              - {key: policy, operator: In, values: [rt]}
        podAffinity:             # SAME node as this group's udpserver
          requiredDuringSchedulingIgnoredDuringExecution:
          - labelSelector:
              matchLabels:
                app: udpserver
            topologyKey: "kubernetes.io/hostname"
```

Pod affinity only matches pods in the **same namespace**, so each group's client
lands with its *own* server, and concurrent groups in different namespaces don't
interfere.

The `6-samenode.yaml` manifest applies this: the server requires a `policy=rt`
node and the client requires the same node as the server (the scheduler decides
which RT node). Download it, deploy it, verify both pods land on the **same**
node, and get the measurements:

```
$ wget 129.192.83.120:8080/Deployments/6-samenode.yaml
$ kubectl -n group1 create -f 6-samenode.yaml

$ kubectl -n group1 get pods -o wide
NAME                        READY   STATUS      RESTARTS   AGE   IP           NODE      NOMINATED NODE   READINESS GATES
udpclient-ndr57             0/1     Completed   0          88s   10.1.9.101   capri-7   <none>           <none>
udpserver-5b8c658f7-fthcv   1/1     Running     0          88s   10.1.9.143   capri-7   <none>           <none>

$ kubectl -n group1 logs udpclient-ndr57

Latency Test Complete (numbers are round-trip times):
--- UDP Latency Statistics (us) ---
Min: 23.000
Max: 882.000
P50: 25.000
P70: 30.000
P90: 41.000
P99: 49.000
P99,9: 70.000
Samples: 100000 of 100000 requested
```

(Both pods are on the **same** node — here the scheduler chose capri-7; another
group might land on capri-6.) This is the lowest latency of the whole lab: with
no network hop, busy-poll, and an isolated core, the round trip is dominated by
loopback and the application itself — **P50 ~25 µs, P99 ~50 µs.**

> **Always delete each deployment as soon as you finish reading its result.**
> The real-time exercises (4–6) run a busy-poll server that holds a whole CPU
> core and spins continuously. If you leave several of them running you tie up
> the limited real-time cores other groups need. The server exits by itself every
> 600 s as a guard against a wedged spin loop, but its Deployment restarts it
> straight away — so that is not cleanup, it only makes a forgotten deployment
> visible as a rising `RESTARTS` count. Delete it explicitly.

Finally delete the deployment and your namespace:

```
$ kubectl -n group1 delete -f 6-samenode.yaml
$ kubectl delete ns group1
```

-----

## Troubleshooting

**A pod is stuck in `Terminating`.**
This usually means the node it was on is briefly busy or unreachable, so the
kubelet hasn't confirmed the container is gone. First just wait ~30 s. If it is
still stuck, force-delete it:

```
$ kubectl -n group1 delete pod <pod-name> --grace-period=0 --force
```

To clear everything in your namespace at once (deployments, jobs, services,
pods) before retrying an exercise:

```
$ kubectl -n group1 delete deploy,job,svc,pod --all --grace-period=0 --force
```

**A pod stays in `Pending`.**
On the real-time exercises (4–6) the real-time nodes have a limited number of
exclusive cores. If several groups run at once they can be temporarily
exhausted, and your pod waits until a core frees up. Check why with:

```
$ kubectl -n group1 describe pod <pod-name>      # look at the Events section
```

Delete any exercises you are no longer using so others get their cores back
(see the cleanup reminder in Exercise 6).

**Exercise 5 only: a pod fails immediately instead of going `Pending`.**
Exercise 5 pins the pods with `nodeName`, which bypasses the Kubernetes
scheduler entirely — so there is no scheduler to hold the pod in `Pending` while
it waits for a free core. Instead the kubelet on that node rejects the pod
outright and you see a status like `OutOfcpu`, or an event such as:

```
$ kubectl -n group1 describe pod <pod-name>
...
  Warning  Failed  ...  not enough cpus available to satisfy request
```

This means capri-6 or capri-7 has no exclusive core left. Wait for another
group to clean up and re-create the deployment:

```
$ kubectl -n group1 delete -f 5-hostnetwork.yaml
$ kubectl -n group1 create -f 5-hostnetwork.yaml
```

**Exercise 5 only: the server pod starts but the client reports no responses.**
Another group is probably already bound to the same UDP port on that node.
Pick a unique port as described in the exercise and change all three
occurrences of `5000`.

**The client fails with `ERROR, no such host as udpserver-service`.**
You almost certainly deleted a deployment and re-created it within about half a
minute. CoreDNS caches negative lookups (NXDOMAIN) for 30 seconds, so when the
Service disappears and comes straight back under the same name, the client's
lookup can still hit the stale "does not exist" entry and give up.

Wait ~30 seconds between deleting and re-creating, or simply run it again:

```
$ kubectl -n group1 delete -f 1-vanilla.yaml
$ sleep 30
$ kubectl -n group1 create -f 1-vanilla.yaml
```

This does not affect Exercise 5, which addresses the server by IP rather than by
Service name.

**The same error, but you did *not* just re-create the deployment.**
There is a second, rarer cause with the identical symptom: the `kubectl apply`
only partially succeeded. This cluster runs its datastore on all six nodes, and
if a leader election happens mid-apply you may see `etcdserver: leader changed`
and end up with the Deployment created but the Service missing. Check whether the
Service actually exists:

```
$ kubectl -n group1 get svc
```

If `udpserver-service` is not listed, just apply the manifest again — the Service
will be created and the client will resolve it.

**A pod stays `Pending` with an event mentioning `untolerated taint {rt-workload: true}`.**
capri-6 and capri-7 are fenced off with that taint, so a pod may only land there
if it carries a matching toleration. Exercises 4–6 already include one; if you
edited the manifest by hand, check that the pod spec still has:

```
      tolerations:
      - { key: rt-workload, operator: Exists, effect: NoSchedule }
```

Note that it belongs in the **pod** spec (alongside `affinity`), not in the
Deployment's or Job's outer `spec`.

For Exercises 1–3 this message is expected in the event text and is *not* the
problem: those pods deliberately have no toleration, so the scheduler reports
capri-6/7 as unavailable and places them on capri-2…capri-5 instead. Only worry
if the pod stays `Pending` — that means the non-real-time nodes could not take it
either, and the same event will say why.

**The client reports `Lost: N datagram(s)`.**
UDP is unreliable, and over the pod overlay a datagram is occasionally dropped.
The client waits up to **500 ms** for each reply; if nothing arrives it counts
that datagram as lost, leaves it out of the statistics, and carries on. A handful
of losses in 100000 iterations is normal and does not invalidate your numbers —
the percentiles are computed over the replies actually received, which is why the
output also tells you how many samples it used:

```
Samples: 99998 of 100000 requested
Lost: 2 datagram(s) with no reply within 500 ms (excluded from the statistics above)
```

If you see a *large* number of losses, something is wrong rather than merely
unlucky — most likely another group is bound to the same host port in Exercise 5.

**The client exits with `no reply ... for 50 consecutive attempts`.**
This means no reply came back at all, so the client gave up after about 25
seconds instead of waiting forever. Usual causes: the server pod is not running
yet, it crashed, or in Exercise 5 the port in the client's args does not match
the port the server is listening on. Check that the server is `Running`, and in
Exercise 5 that all three occurrences of the port agree:

```
$ kubectl -n group1 get pods
$ kubectl -n group1 logs <udpserver-pod>
```

**Always delete each deployment as soon as you have recorded its result** — the
real-time exercises run a busy-poll workload that holds a whole CPU core, so
leaving them running ties up the shared real-time nodes for everyone else.

-----

## Summary

You started with a plain, best-effort deployment and improved its latency and
determinism one step at a time. Each step removed a source of latency or a
source of variability (jitter). Fill in the table with the numbers you recorded
to see the progression:

| Step | Change applied | RTT P99 (us) | RTT Max (us) |
| ---- | -------------- | -----------: | -----------: |
| 1. Vanilla        | Best-effort, shared CPUs, overlay network |   |   |
| 2. Guaranteed QoS | Integer requests == limits (highest QoS, not evicted first) |   |   |
| 3. SCHED_FIFO     | Real-time scheduling policy + capabilities |   |   |
| 4. RT nodes       | Isolated / nohz_full / PREEMPT_RT nodes + exclusive cores + busy-poll |   |   |
| 5. Host network   | Same two nodes, busy-poll, bypass the pod overlay |   |   |
| 6. Same node      | Busy-poll, remove the network hop entirely |   |   |

Steps 1–5 all keep the client and server on **separate** nodes, so they are
directly comparable; each one improves the cross-node latency. Step 6 then
removes the network from the path entirely for the lowest result.

For reference, typical P50 / P99 values measured on this cluster (your numbers
will vary, especially the Max, because the nodes are virtual machines):

| Step | P50 (us) | P99 (us) |
| ---- | -------: | -------: |
| 1. Vanilla        | ~280 | ~1600 |
| 2. Guaranteed     | ~290 | ~1600 |
| 3. SCHED_FIFO     | ~280 | ~550  |
| 4. RT nodes       | ~210 | ~500  |
| 5. Host network   | ~140 | ~210  |
| 6. Same node      | ~25  | ~50   |

> **Steps 3 and 4 are close on the P99.** The big tail improvement happens at
> Step 3, when the real-time scheduling policy is applied — P99 drops from about
> 1600 µs to roughly 550 µs. Step 4 then clearly improves the **median** (about
> 280 → 210 µs) because the pod gets an isolated core on a quiet node, but its
> P99 only moves a little and run-to-run variation can overlap with Step 3. That
> is worth noticing rather than glossing over: the isolated core makes the
> *typical* packet faster, while the remaining tail on these nodes is dominated
> by the fact that they are KVM virtual machines, which no amount of in-guest
> tuning removes.

> **Do not expect Step 2 to improve the median or the P99.** Measured over four
> runs of each, Exercise 1 and Exercise 2 are statistically indistinguishable on
> these nodes: P50 276 vs 294 us and P99 1546 vs 1680 us, with the ranges
> overlapping heavily. The Guaranteed class does not give the pod a dedicated
> core here — these non-real-time nodes do not run the CPU Manager's static
> policy — so it cannot reduce ordinary scheduling latency.
>
> Where it *does* help is the extreme tail. Across the same runs the **Max**
> dropped from roughly 22 ms to 13 ms, and it was lower in every single run.
> That is the mechanism working as designed: a Guaranteed pod is the last to be
> throttled or evicted under pressure, which shows up in the rare outlier rather
> than at the P99. The lesson is that **a QoS class is not a latency feature** —
> it is a priority-under-pressure feature.

The six steps map directly to the techniques covered in the presentation: QoS &
exclusive CPUs, real-time scheduling inside the pod, node tuning and steering,
and bypassing the default network datapath.


