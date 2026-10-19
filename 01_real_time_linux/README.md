## Hands on real-time Linux

#### Playing with Linux real-time schedulers and full-isolation / fully preemptible kernel configurations

In a cyber physical cloud environment, it is very likely that some Linux host or VM will need to execute tasks with real-time constraints.

In this lab, you will learn:
- how to test the *latency* of your host
- how to schedule applications with Linux built-in *real-time schedulers*
- how to *fully isolate* a CPU to protect its workload from *noisy neighbors*
- and how to reduce the latency of time-critical applications by enabling the *PREEMPT_RT fully preemptible* kernel configuration

### 0. Prerequisites
This lab activity requires a Linux host.
You can use your own Linux machine, a local VM, or you can create a VM on the Ericsson OpenStack environment, following the tutorial:

https://github.com/capric-school/capric_hands_on/tree/main/00_openstack_vm

To run real-time benchmarks on your host, install the rt-test utilities with:

```
sudo apt install rt-tests
```

and rt-app with:

```
sudo apt install rt-app
```

For stress workloads, install the stress-ng tool with:

```
sudo apt install stress-ng
```

### 1. Run cyclictest to measure system latency
Cyclictest provides accurate statistics on system latencies by repeatedly measuring the difference between a thread's intended wake-up time and the time at which it actually wakes up.
 
Try it with:

```
sudo cyclictest --mlockall --threads=1 --affinity=1 --interval=5000 --duration 1m
```
where:
- --mlockall: Locks the process memory into RAM so it can’t be swapped out, which is important for real-time consistency
- --threads=1: Spawns 1 thread to measure latency.
- --affinity=1: Pins the test thread to CPU core 1. This is useful to isolate testing to a specific core.
- --interval=5000: The thread will wake up every 5000 microseconds (5 milliseconds) to measure latency.
- --duration 1m: Runs the test for 1 minute.

You may add the ```-H 100 --histfile=LOG.log``` option to output histogram data in a file Log.log, and to set the latency histogram size to 100 microseconds. Latencies over 100µs are counted in an overflow bin.

More info available at:
https://documentation.ubuntu.com/real-time/latest/how-to/measure-maximum-latency/
https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/start


### 2. Use Linux real-time schedulers
Linux includes different scheduling classes. SCHED_OTHER is the default time sharing policy for general purpose tasks. 
The following *real-time scheduling* policies are available:
- **SCHED_FIFO**: fixed priority preemptive scheduling, with 99 priority levels available, from 1 (lowest) to 99 (highest). Tasks with the same priority run until completion, and are scheduled in fifo order. It can be used to perform rate monotonic scheduling of periodic tasks. 
- **SCHED_RR**: like SCHED_FIFO, but with a time slice defined to alternate tasks with the same priority using a configurable time quantum.
- **SCHED_DEADLINE**: Earliest Deadline First scheduling of sporadic/periodic tasks and with a runtime budget control using the constant bandwidth server policy. Every task can run up to a given "sched-runtime" every "sched-period" and it is scheduled according to its "sched-deadline" parameter.

To change the scheduling policy for a task, the following command can be used:
```
chrt [options] priority task_to_be_run
```
where the options can be:
- -o, --other: sets the SCHED_OTHER policy
- -r, --rr: sets the SCHED_RR policy
- -f, --fifo: sets the SCHED_FIFO policy
- -d, --deadline: sets the SCHED_DEADLINE policy. In this case, the priority argument must be 0 and the following arguments can be added:
    - -T--sched-runtime *nanoseconds* to set the runtime
    - -P, --sched-period *nanoseconds* to set the period
    - -D, --sched-deadline *nanoseconds* to set the deadline

We prepared a simple latency test code in the source file ```latency_test.c```.
Download, compile, and try to schedule it with real-time schedulers as follows.

Compile with gcc:
```
gcc -o latency latency_test.c 
```
Test with default scheduling (SCHED_OTHER):
```
$ ./latency 
Latencies: Min:      96 Act:    1157    Avg:957.210000	Max:    1849
Latencies: Min:      85 Act:    1129    Avg:857.860000	Max:    1849
Latencies: Min:      80 Act:    1058    Avg:877.066667	Max:    1849
Latencies: Min:      80 Act:    1105    Avg:897.897500	Max:    1849
```
Now with SCHED_FIFO and priority 90:
```
$ sudo chrt -f 90 ./latency 
Latencies: Min:      17 Act:    1117    Avg:802.700000	Max:    1208
Latencies: Min:      17 Act:     583    Avg:813.795000	Max:    1309
Latencies: Min:      17 Act:     341    Avg:825.606667	Max:    1678
Latencies: Min:      17 Act:     495    Avg:811.832500	Max:    1678
```
And now with SCHED_DEADLINE with runtime 1ms, period and deadline 5ms:
```
$ sudo chrt -d -T 1000000 -P 5000000 -D 5000000 0 ./latency 
Latencies: Min:      27 Act:     929    Avg:868.150000	Max:    1259
Latencies: Min:      23 Act:    1126    Avg:843.020000	Max:    1301
Latencies: Min:      19 Act:     691    Avg:842.036667	Max:    1301
Latencies: Min:      19 Act:    1084    Avg:838.660000	Max:    1301
```
While the test is running, you can check the scheduling policy with the command ```ps -eLfc``` from a second terminal.

You can also run cyclictest with SCHED_FIFO by adding the ```--priority=90``` option, where 90 is the priority level.
In the rt-tests suite, you can also find specific utilities for testing SCHED_DEADLINE, such as ```cyclicdeadline``` and ```deadline_test```.

You can also try rt-app (https://github.com/scheduler-tools/rt-app), after configuring a workload in a json file. An example of workload can be foun in ```test_realtime.json```, and can be used as follow:

```
sudo rt-app test_realtime.json
```

**Do real-time schedulers help to keep latencies low, also in case of stress?**
To answer, try to repeat the tests by running a stress workload in parallel (from a different terminal):
```
$ sudo stress-ng --cyclic 1 --cyclic-dist 250 --cyclic-method clock_ns --cyclic-policy rr -t 3600 --log-file cyclic-stress.log --verbose
```
Note that the above command will run stress-ng for 1 hour, and will log the results in the file ```cyclic-stress.log```. The ```--cyclic-dist``` option specifies the distribution of the sleep times, and the ```--cyclic-method``` option specifies the method to use for the cyclic stress. The ```--cyclic-policy``` option specifies the scheduling policy to use for the cyclic stress.

### 3. Configure a CPU to be used in full isolation
Using Linux boot parameters, it is possible to *fully isolate* one or more CPUs from the scheduler and kernel noise. This helps to run a group of critical tasks in isolation.

Edit the ```/etc/default/grub``` (or ```/etc/default/grub.d/50-cloudimg-settings.cfg```, in case of OS cloud images as in OpenStack) file and add the following options to the ```GRUB_CMDLINE_LINUX_DEFAULT```:
- **isolcpus=X,Y**: The isolcpus kernel option can be used to specify a set of CPUs (e.g., X and Y) to be isolated from scheduler choices;
- **nohz_full=X,Y**: Kernel needs to do work to maintain its internal state and services, and tick-based service generates noise for latency sensitive workloads. The nohz_full option enables tickless mode for the indicated CPUs (e.g., X and Y);
- **rcu_nocbs=X,Y**: RCU callbacks are invoked in softirq context. These callbacks often free memory, and the memory allocators can therefore impose large latencies. With the rcu_nocbs option, we can tell the kernel not to select the indicated CPUs (e.g., X and Y) for RCU callbacks;
- **irqaffinity=X,Y**: this option is used to tell the kernel to run interrupt handlers on specific CPUs, e.g., X and Y.  So, we must not indicate the CPUs that we want to isolate.

If you created a VM with 4 CPUs, try to isolate CPU 1 with the following configuration to add to ```GRUB_CMDLINE_LINUX_DEFAULT```:
```
isolcpus=1 nohz_full=1 rcu_nocbs=1 irqaffinity=0,2-3
```
Then, run:
```
sudo update-grub
```
and reboot the machine. When ready, run the following command to verify that the isolation has been set:
```
cat /proc/cmdline
```
and repeat the latency tests by setting the affinity of the workload to be run on the isolated CPU with the ```taskset``` utility:
```
sudo taskset -c 1 cyclictest --mlockall --priority=90 --threads=1 --affinity=1 --interval=5000 --duration 1m
```
The above command will run cyclictest on the isolated core 1.



### 4. Enable PREEMPT_RT
The main aim of the PREEMPT_RT patch is to minimize the amount of kernel code that is non-preemptible. Therefore, several substitution mechanisms and new mechanisms are implemented to avoid unexpected long latencies for time critical tasks, eliminating (almost) all non-preemptible kernel sections.

Since kernel version 6.12, PREEMPT_RT is part of the mainline kernel and only needs to be enabled as the preemption mode (with the CONFIG_PREEMPT_RT option).

A proper guide can be found here for previous kernels where a patch needs to be applied:

https://wiki.linuxfoundation.org/realtime/documentation/howto/applications/preemptrt_setup

For kernels equal or above 6.12 still recompilation is necessary to select the CONFIG_PREEMPT_RT option, but the patch does not need to be applied.

After enabling PREEMPT_RT, try to repeat the latency tests as done above.

Many guides are available on how to properly configure the kernel to avoid latency killers, such as this guideline for x86 machines: https://v3.xenomai.org/tips/x86/common/

