/************************************************************************************************/
/*                                                                                              */
/*  Copyright 2010  Broadcom Corporation                                                        */
/*                                                                                              */
/*     Unless you and Broadcom execute a separate written software license agreement governing  */
/*     use of this software, this software is licensed to you under the terms of the GNU        */
/*     General Public License version 2 (the GPL), available at                                 */
/*                                                                                              */
/*          http://www.broadcom.com/licenses/GPLv2.php                                          */
/*                                                                                              */
/*     with the following added to such license:                                                */
/*                                                                                              */
/*     As a special exception, the copyright holders of this software give you permission to    */
/*     link this software with independent modules, and to copy and distribute the resulting    */
/*     executable under terms of your choice, provided that you also meet, for each linked      */
/*     independent module, the terms and conditions of the license of that module.              */
/*     An independent module is a module which is not derived from this software.  The special  */
/*     exception does not apply to any modifications of the software.                           */
/*                                                                                              */
/*     Notwithstanding the above, under no circumstances may you combine this software in any   */
/*     way with any other Broadcom software provided under a license other than the GPL,        */
/*     without Broadcom's express prior written consent.                                        */
/*                                                                                              */
/************************************************************************************************/

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/types.h>

#include <asm/smp_twd.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <mach/io.h>
#include <mach/io_map.h>
#include <mach/kona_timer.h>
#include <mach/timer.h>

#ifdef CONFIG_LOCAL_TIMERS
#include <asm/smp_twd.h>
#endif

static struct kona_timer *gpt_evt = NULL;
static struct kona_timer *gpt_src = NULL;
static DEFINE_CLOCK_DATA(cd);

/*
 *read_persistent_clock -  Return time from a *fake* persistent clock.
 * Reads the time as 2 Jan, 1970 for the first time. This is necessary so
 * Android does not reset the time and then AlarmManager is happy.
 * Every subsequentcall will return 0 in timespec, so that the suspend/resume
 *timekeeping code will take the the appropriate path (i.e as if the arch does
 *not support read_persistent_clock()
 **/

static unsigned int first_persistent_clock_read = 1;
#define SECS_PER_DAY (86400UL)
void read_persistent_clock(struct timespec *ts)
{
	if (first_persistent_clock_read) {
		ts->tv_sec = SECS_PER_DAY;
		ts->tv_nsec = 0;
		first_persistent_clock_read = 0;
		return;
	}

	ts->tv_sec = 0;
	ts->tv_nsec = 0;
}

static int gptimer_set_next_event(unsigned long clc,
				  struct clock_event_device *unused)
{
	/* gptimer (0) is disabled by the timer interrupt already
	 *so, here we reload the next event value and re-enable
	 *the timer
	 *
	 * This way, we are potentially losing the time between
	 *timer-interrupt->set_next_event. CPU local timers, when
	 *they come in should get rid of skew
	 */
#ifdef CONFIG_GP_TIMER_CLOCK_OFF_FIX
	{
		volatile unsigned int i;
		kona_timer_disable_and_clear(gpt_evt);
		for (i = 0; i < 1800; i++) {
		}
	}
#endif
	kona_timer_set_match_start(gpt_evt, clc);
	return 0;
}

static void gptimer_set_mode(enum clock_event_mode mode,
			     struct clock_event_device *unused)
{
	switch (mode) {
	case CLOCK_EVT_MODE_ONESHOT:
		/* by default mode is one shot don't do any thing */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		kona_timer_disable_and_clear(gpt_evt);
	}
}

static cycle_t notrace gptimer_clksrc_read(struct clocksource *cs)
{
	cycle_t count = 0;

	count = kona_timer_get_counter(gpt_src);
	return count;
}

static struct clock_event_device clockevent_gptimer = {
	.name = "gpt_event_1",
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.shift = 32,
	.set_next_event = gptimer_set_next_event,
	.set_mode = gptimer_set_mode
};

static struct clocksource clksrc_gptimer = {
	.name = "gpt_source_2",
	.rating = 200,
	.read = gptimer_clksrc_read,
	.mask = CLOCKSOURCE_MASK(32),	/* Although Kona timers have 64 bit counters,
					   To avail all the four channels of HUB_TIMER
					   the match register is compared with 32 bit value
					   and to make everything in sync, the Linux framework
					   is informed that CS timer is 32 bit.
					 */
	.shift = 16,		/* Fix shift as 16 and calculate mult based on this during init */
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init gptimer_clockevents_init(void)
{
	clockevent_gptimer.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC,
					 clockevent_gptimer.shift);

	clockevent_gptimer.max_delta_ns =
	    clockevent_delta2ns(0xffffffff, &clockevent_gptimer);

#ifdef CONFIG_GP_TIMER_COMPARATOR_LOAD_DELAY
	/* This is to accomodate the polling in kona_timer_set_match_start().
	 */
	clockevent_gptimer.min_delta_ns =
	    clockevent_delta2ns((CLOCK_TICK_RATE * 5) / 32000,
				&clockevent_gptimer);
#else
	clockevent_gptimer.min_delta_ns =
	    clockevent_delta2ns(6, &clockevent_gptimer);
#endif

	clockevent_gptimer.cpumask = cpumask_of(0);
	clockevents_register_device(&clockevent_gptimer);
}

static void __init gptimer_clocksource_init(void)
{
	clksrc_gptimer.mult = clocksource_hz2mult(CLOCK_TICK_RATE,
						  clksrc_gptimer.shift);
	clocksource_register(&clksrc_gptimer);
	return;
}

static int gptimer_interrupt_cb(void *dev)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev;
	evt->event_handler(evt);
	return 0;
}

static void __init timers_init(struct gp_timer_setup *gpt_setup)
{
	struct timer_ch_cfg evt_tm_cfg;

	if (kona_timer_modules_init() < 0) {
		pr_err
		    ("timers_init: Unable to initialize kona timer modules \r\n");
		return;
	}

	if (kona_timer_module_set_rate(gpt_setup->name, gpt_setup->rate) < 0) {
		pr_err("timers_init: Unable to set the clock rate to %d	\r\n",
		       gpt_setup->rate);
		return;
	}

	/* Initialize Event timer */
	gpt_evt = kona_timer_request(gpt_setup->name, gpt_setup->ch_num);
	if (gpt_evt == NULL) {
		pr_err("timers_init: Unable to get GPT timer for event\r\n");
		return;
	}

	pr_info("timers_init: === SYSTEM TIMER NAME: %s CHANNEL NUMBER %d \
	RATE %d \r\n", gpt_setup->name, gpt_setup->ch_num, gpt_setup->rate);

	evt_tm_cfg.mode = MODE_ONESHOT;
	evt_tm_cfg.arg = &clockevent_gptimer;
	evt_tm_cfg.cb = gptimer_interrupt_cb;

	kona_timer_config(gpt_evt, &evt_tm_cfg);

	gptimer_set_next_event((CLOCK_TICK_RATE / HZ), NULL);

	/*
	 * IMPORTANT
	 * Note that we don't want to waste a channel for clock source. In Kona
	 *timer module by default there is a counter that keeps counting
	 *irrespective of the channels. So instead of implementing a periodic
	 *timer using a channel (which in the HW is not peridoic) we can
	 *simply read the counters of the timer that is used for event and
	 *send it for source. The only catch is that this timer should not be
	 *stopped by PM or any other sub-systems.
	 */
	gpt_src = gpt_evt;

	return;
}

unsigned long long notrace sched_clock(void)
{
	if (unlikely(gpt_src == NULL))
		return 0;

	return cyc_to_sched_clock(&cd, gptimer_clksrc_read(NULL), (u32)~0);
}

static void notrace kona_update_sched_clock(void)
{
	update_sched_clock(&cd, gptimer_clksrc_read(NULL), (u32)~0);
}

void __init gp_timer_init(struct gp_timer_setup *gpt_setup)
{
	timers_init(gpt_setup);
	gptimer_clocksource_init();
	gptimer_clockevents_init();
	gptimer_set_next_event((CLOCK_TICK_RATE / HZ), NULL);

#ifdef CONFIG_LOCAL_TIMERS
	twd_base = IOMEM(KONA_PTIM_VA);
#endif

	init_sched_clock(&cd, kona_update_sched_clock, 32, CLOCK_TICK_RATE);
}
