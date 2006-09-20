/* -*- linux-c -*- 
 * time-estimation with minimal dependency on xtime
 * Copyright (C) 2006 Intel Corporation.
 *
 * This file is part of systemtap, and is free software.  You can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License (GPL); either version 2, or (at your option) any
 * later version.
 */

#include <linux/cpufreq.h>

#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC	1000000L
#endif

typedef struct __stp_time_t {
    /* 
     * A write lock is taken by __stp_time_timer_callback() and
     * __stp_time_cpufreq_callback().  The timer callback is called from a
     * softIRQ, and cpufreq callback guarantees that it is not called within
     * an interrupt context.  Thus there should be no opportunity for a
     * deadlock between writers.
     *
     * A read lock is taken by _stp_gettimeofday_us().  There is the potential
     * for this to occur at any time, so there is a slim chance that this will
     * happen while the write lock is held, and it will be impossible to get a
     * read lock.  However, we can limit how long we try to get the lock to
     * avoid a deadlock.
     *
     * Note that seqlock is safer than rwlock because some kernels
     * don't have read_trylock.
     */
    seqlock_t lock;

    /* These provide a reference time to correlate cycles to real time */
    int64_t base_ns;
    cycles_t base_cycles;

    /* The frequency in kHz of this CPU, for interpolating
     * cycle counts from the base time. */
    unsigned int cpufreq;

    /* Callback used to schedule updates of the base time */
    struct timer_list timer;
} stp_time_t;

DEFINE_PER_CPU(stp_time_t, stp_time);

/* Try to estimate the number of CPU cycles in a millisecond - i.e. kHz.  This
 * relies heavily on the accuracy of udelay.  By calling udelay twice, we
 * attempt to account for overhead in the call.
 * 
 * NB: interrupts should be disabled when calling this.
 *
 * FIXME: This not very accurate on Xen kernels!
 */
static unsigned int
__stp_estimate_cpufreq(void)
{
    cycles_t beg, mid, end;
    beg = get_cycles(); barrier();
    udelay(1); barrier();
    mid = get_cycles(); barrier();
    udelay(1001); barrier();
    end = get_cycles(); barrier();
    return (beg - 2*mid + end);
}

static void
__stp_time_timer_callback(unsigned long val)
{
    unsigned long flags;
    stp_time_t *time;
    struct timeval tv;
    int64_t ns;
    cycles_t cycles;

    local_irq_save(flags);

    do_gettimeofday(&tv);
    cycles = get_cycles();

    ns = (NSEC_PER_SEC * (int64_t)tv.tv_sec)
        + (NSEC_PER_USEC * tv.tv_usec);

    time = &__get_cpu_var(stp_time);
    write_seqlock(&time->lock);
    time->base_ns = ns;
    time->base_cycles = cycles;
    write_sequnlock(&time->lock);

    mod_timer(&time->timer, jiffies + 1);

    local_irq_restore(flags);
}

/* This is called as an IPI, with interrupts disabled. */
static void
__stp_init_time(void *info)
{
    struct timeval tv;
    stp_time_t *time = &__get_cpu_var(stp_time);


    seqlock_init(&time->lock);
    do_gettimeofday(&tv);
    time->base_cycles = get_cycles();
    time->base_ns = (NSEC_PER_SEC * (int64_t)tv.tv_sec)
        + (NSEC_PER_USEC * tv.tv_usec);
    time->cpufreq = __stp_estimate_cpufreq();

    init_timer(&time->timer);
    time->timer.expires = jiffies + 1;
    time->timer.function = __stp_time_timer_callback;
    add_timer(&time->timer);
}

#ifdef CONFIG_CPU_FREQ
static int
__stp_time_cpufreq_callback(struct notifier_block *self,
        unsigned long state, void *vfreqs)
{
    unsigned long flags;
    struct cpufreq_freqs *freqs;
    int freq_khz;
    stp_time_t *time;

    switch (state) {
        case CPUFREQ_POSTCHANGE:
        case CPUFREQ_RESUMECHANGE:
            freqs = (struct cpufreq_freqs *)vfreqs;
            freq_khz = freqs->new;
            time = &per_cpu(stp_time, freqs->cpu);
            write_seqlock_irqsave(&time->lock, flags);
            time->cpufreq = freq_khz;
            write_sequnlock_irqrestore(&time->lock, flags);
            break;
    }

    return NOTIFY_OK;
}

struct notifier_block __stp_time_notifier = {
    .notifier_call = __stp_time_cpufreq_callback,
};
#endif /* CONFIG_CPU_FREQ */

void
_stp_kill_time(void)
{
    int cpu;
    for_each_online_cpu(cpu) {
        stp_time_t *time = &per_cpu(stp_time, cpu);
        del_timer_sync(&time->timer);
    }
#ifdef CONFIG_CPU_FREQ
    cpufreq_unregister_notifier(&__stp_time_notifier,
            CPUFREQ_TRANSITION_NOTIFIER);
#endif
}

int
_stp_init_time(void)
{
    int ret = 0;
    int cpu, freq_khz;
    unsigned long flags;

    ret = on_each_cpu(__stp_init_time, NULL, 0, 1);

#ifdef CONFIG_CPU_FREQ
    if (ret) goto end;

    ret = cpufreq_register_notifier(&__stp_time_notifier,
            CPUFREQ_TRANSITION_NOTIFIER);
    if (ret) goto end;

    for_each_online_cpu(cpu) {
        preempt_disable();
        freq_khz = cpufreq_get(cpu);
        if (freq_khz > 0) {
            stp_time_t *time = &per_cpu(stp_time, cpu);
            write_seqlock_irqsave(&time->lock, flags);
            time->cpufreq = freq_khz;
            write_sequnlock_irqrestore(&time->lock, flags);
        }
        preempt_enable();
    }
end:
#endif

    return ret;
}

int64_t
_stp_gettimeofday_ns(void)
{
    int64_t base;
    cycles_t last, delta;
    unsigned int freq;
    unsigned int seq;
    stp_time_t *time;
    int i = 0;

    preempt_disable();

    time = &__get_cpu_var(stp_time);

    seq = read_seqbegin(&time->lock);
    base = time->base_ns;
    last = time->base_cycles;
    freq = time->cpufreq;
    while (unlikely(read_seqretry(&time->lock, seq))) {
        if (unlikely(++i >= MAXTRYLOCK))
            return 0;
        ndelay(TRYLOCKDELAY);
        seq = read_seqbegin(&time->lock);
        base = time->base_ns;
        last = time->base_cycles;
        freq = time->cpufreq;
    }

    delta = get_cycles() - last;

    preempt_enable();

    // Verify units:
    //   (D cycles) * (1E6 ns/ms) / (F cycles/ms [kHz]) = ns
    delta *= NSEC_PER_MSEC;
    do_div(delta, freq);
    return base + delta;
}

