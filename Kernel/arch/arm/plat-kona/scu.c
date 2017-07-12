/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Portions of this software are Copyright 2011 Broadcom Corporation */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <plat/scu.h>


static void __iomem *scu_base;
static DEFINE_SPINLOCK(scu_lock);

#if defined(CONFIG_RHEA_B0_PM_ASIC_WORKAROUND)
/* Ref counting for scu_standby signal disable requests */
static int scu_standby_disable_cnt;
module_param_named(scu_standby_disable, scu_standby_disable_cnt, int,
		   S_IRUGO | S_IWUSR | S_IWGRP);

int scu_standby(bool enable)
{
	unsigned int val;
	unsigned long flgs;

	if (!scu_base)
		return -ENODEV;

	spin_lock_irqsave(&scu_lock, flgs);
	if (enable) {
		if (scu_standby_disable_cnt)
			scu_standby_disable_cnt--;

		if (scu_standby_disable_cnt == 0) {
			val = readl(scu_base + SCU_CONTROL_OFFSET);
			val |= SCU_CONTROL_SCU_STANDBY_EN_MASK;
			writel(val, scu_base + SCU_CONTROL_OFFSET);
		}
	} else {
		if (scu_standby_disable_cnt == 0) {
			val = readl(scu_base + SCU_CONTROL_OFFSET);
			val &= ~SCU_CONTROL_SCU_STANDBY_EN_MASK;
			writel(val, scu_base + SCU_CONTROL_OFFSET);
		}

		scu_standby_disable_cnt++;
	}
	spin_unlock_irqrestore(&scu_lock, flgs);

	return 0;
}
#else
int scu_standby(bool enable)
{
	return 0;
}
#endif /* CONFIG_RHEA_B0_PM_ASIC_WORKAROUND */

int scu_invalidate_all(void)
{
	if (!scu_base)
		return -ENODEV;

	writel_relaxed(0xFFFF, scu_base + SCU_INVALIDATE_ALL_OFFSET);

	return 0;
}

/*
 * Set the executing CPUs power mode as defined.  This will be in
 * preparation for it executing a WFI instruction.
 *
 * This function must be called with preemption disabled, and as it
 * has the side effect of disabling coherency, caches must have been
 * flushed.  Interrupts must also have been disabled.
 */
int scu_set_power_mode(unsigned int mode)
{
	unsigned int val;
	int cpu = smp_processor_id();

	if (!scu_base)
		return -ENODEV;

	if (mode > 3 || mode == 1 || cpu > 3)
		return -EINVAL;

	val = readb(scu_base + SCU_POWER_STATUS_OFFSET + cpu) & ~0x03;
	val |= mode;
	writeb_relaxed(val, scu_base + SCU_POWER_STATUS_OFFSET + cpu);

	return 0;
}

void scu_init(void __iomem *base)
{
	unsigned int val;

	scu_base = base;

	val = readl(scu_base + SCU_CONTROL_OFFSET);
	val |= SCU_CONTROL_SCU_STANDBY_EN_MASK;
	writel(val, scu_base + SCU_CONTROL_OFFSET);
}
