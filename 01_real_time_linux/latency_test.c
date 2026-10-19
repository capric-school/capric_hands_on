#include <sys/time.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>

static struct timespec r;
static struct timespec now;
static struct timespec next;
static int period;

#define NSEC_PER_SEC 1000000000ULL
static inline void timespec_add_us(struct timespec *t, uint64_t d)
{
    d *= 1000;
    t->tv_nsec += d;
    t->tv_sec += t->tv_nsec / NSEC_PER_SEC;
    t->tv_nsec %= NSEC_PER_SEC;
}

void wait_next_activation(void)
{
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &r, NULL);
    timespec_add_us(&r, period);
}

void start_periodic_timer(uint64_t offs, int t)
{
    clock_gettime(CLOCK_MONOTONIC, &r);
    timespec_add_us(&r, offs);
    period = t;
}

int64_t calcdiff_ns(struct timespec t1, struct timespec t2)
{
	int64_t diff;
	diff = NSEC_PER_SEC * (int64_t)((int) t1.tv_sec - (int) t2.tv_sec);
	diff += ((int) t1.tv_nsec - (int) t2.tv_nsec);
	return diff;
}

static void job_body(void)
{
    static int cnt;
    int64_t diff = 0;
    static int64_t max_diff = 0;
    static int64_t min_diff = 10000000000;
    static double tot = 0;
    double avg = 0;
    
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    diff = calcdiff_ns(now,next)/1000;
    
    next=r;
    
    if(diff < min_diff) min_diff = diff;
    if(diff > max_diff) max_diff = diff;
    
    cnt++;
    tot += (double)diff;
    avg = (double)tot/cnt;
    
    if (cnt && (cnt%100) == 0){
    	printf("Latencies: Min:%8ld Act:%8ld    Avg:%8f	Max:%8ld\n", min_diff, diff, avg, max_diff);
    }
}

int main()
{
    /*struct sched_param sp;
    sp.sched_priority = 11;
    sched_setscheduler(0, SCHED_FIFO, &sp);*/

    start_periodic_timer(5000, 5000);
    
    next=r;
    
    while(1) {
        wait_next_activation();
        job_body();
    }

    return 0;
}
