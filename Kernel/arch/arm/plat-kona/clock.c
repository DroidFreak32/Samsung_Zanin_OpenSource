/*****************************************************************************
*
* Kona generic clock framework
*
* Copyright 2010 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clkdev.h>
#include <plat/clock.h>
#include <asm/io.h>
#include<plat/pi_mgr.h>

#ifdef CONFIG_SMP
#include <asm/cpu.h>
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
#endif

/* global spinlock for clock API */
static DEFINE_SPINLOCK(clk_gen_lock);

int clk_debug = 0;

/*clk_dfs_request_update -  action*/
enum {
	CLK_STATE_CHANGE,	/*param = 1 => enable. param =0 => disable */
	CLK_RATE_CHANGE		/*param = rate */
};

/*fwd declarations....*/
static int __pll_clk_enable(struct clk *clk);
static int __pll_chnl_clk_enable(struct clk *clk);
static int peri_clk_set_voltage_lvl(struct peri_clk *peri_clk, int voltage_lvl);
#ifdef CONFIG_KONA_PI_MGR
static int clk_dfs_request_update(struct clk *clk, u32 action, u32 param);
#endif
static int ccu_init_state_save_buf(struct ccu_clk *ccu_clk);

__weak void mach_dump_ccu_registers(struct clk* clk);

static int __ccu_clk_init(struct clk *clk)
{
	struct ccu_clk *ccu_clk;
	int ret = 0;
	ccu_clk = to_ccu_clk(clk);

	clk_dbg("%s - %s\n", __func__, clk->name);

	CCU_ACCESS_EN(ccu_clk, 1);

	INIT_LIST_HEAD(&ccu_clk->clk_list);
	/*
	   Initilize CCU context save buf if CCU state save parameters
	   are defined for this CCU.
	 */
	if (ccu_clk->ccu_state_save)
		ccu_init_state_save_buf(ccu_clk);

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	if (clk->flags & CCU_KEEP_UNLOCKED) {
		ccu_clk->write_access_en_count = 0;
		/* enable write access */
		ccu_write_access_enable(ccu_clk, true);
	}
	CCU_ACCESS_EN(ccu_clk, 0);

	return ret;
}

static int __peri_clk_init(struct clk *clk)
{
	struct peri_clk *peri_clk;
	int ret = 0;

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	peri_clk = to_peri_clk(clk);
	BUG_ON(peri_clk->ccu_clk == NULL);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

	/*Add DSF request */
#ifdef CONFIG_KONA_PI_MGR
	if (peri_clk->clk_dfs) {
		BUG_ON(peri_clk->ccu_clk->pi_id == -1);
		ret = pi_mgr_dfs_add_request_ex(&peri_clk->clk_dfs->dfs_node,
						(char *)clk->name,
						peri_clk->ccu_clk->pi_id,
						PI_MGR_DFS_MIN_VALUE,
						PI_MGR_DFS_WIEGHTAGE_NONE);
		if (ret)
			clk_dbg
			    ("%s: failed to add dfs node for the clock: %s\n",
			     __func__, clk->name);
	}
#endif

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &peri_clk->ccu_clk->clk_list);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);
	return ret;
}

static int __bus_clk_init(struct clk *clk)
{
	int ret = 0;
	struct bus_clk *bus_clk;

	bus_clk = to_bus_clk(clk);
	BUG_ON(bus_clk->ccu_clk == NULL);

	clk_dbg("%s - %s\n", __func__, clk->name);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);

	/*Add DSF request */
#ifdef CONFIG_KONA_PI_MGR
	if (bus_clk->clk_dfs) {
		BUG_ON(bus_clk->ccu_clk->pi_id == -1);
		ret = pi_mgr_dfs_add_request_ex(&bus_clk->clk_dfs->dfs_node,
						(char *)clk->name,
						bus_clk->ccu_clk->pi_id,
						PI_MGR_DFS_MIN_VALUE,
						PI_MGR_DFS_WIEGHTAGE_NONE);
		if (ret)
			clk_dbg
			    ("%s: failed to add dfs node for the clock: %s\n",
			     __func__, clk->name);
	}
#endif

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &bus_clk->ccu_clk->clk_list);

	return ret;
}

static int __ref_clk_init(struct clk *clk)
{
	struct ref_clk *ref_clk;
	int ret = 0;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);
	BUG_ON(ref_clk->ccu_clk == NULL);

	clk_dbg("%s, clock name: %s\n", __func__, clk->name);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);
	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &ref_clk->ccu_clk->clk_list);

	return ret;
}

static int __pll_clk_init(struct clk *clk)
{
	struct pll_clk *pll_clk;
	int ret = 0;

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);
	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);
	BUG_ON(pll_clk->ccu_clk == NULL);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	if (CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)) {
		__pll_clk_enable(clk);
	}

	else if (CLK_FLG_ENABLED(clk, DISABLE_ON_INIT)) {
		if (clk->ops && clk->ops->enable)
			clk->ops->enable(clk, 0);
	}
	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &pll_clk->ccu_clk->clk_list);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);
	return ret;
}

static int __pll_chnl_clk_init(struct clk *clk)
{
	struct pll_chnl_clk *pll_chnl_clk;
	int ret = 0;

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);
	BUG_ON(pll_chnl_clk->ccu_clk == NULL);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);

	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &pll_chnl_clk->ccu_clk->clk_list);

	if (CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)) {
		__pll_chnl_clk_enable(clk);
	}

	else if (CLK_FLG_ENABLED(clk, DISABLE_ON_INIT)) {
		if (clk->ops && clk->ops->enable)
			clk->ops->enable(clk, 0);
	}

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);
	return ret;
}

static int __misc_clk_init(struct clk *clk)
{
	int ret = 0;

	if (clk->ops && clk->ops->init)
		ret = clk->ops->init(clk);
	return ret;
}

static int __clk_init(struct clk *clk)
{
	int ret = 0;
	clk_dbg("%s - %s clk->init = %d clk->clk_type = %d\n", __func__,
		clk->name, clk->init, clk->clk_type);
	if (!clk->init) {
		switch (clk->clk_type) {
		case CLK_TYPE_CCU:
			ret = __ccu_clk_init(clk);
			break;

		case CLK_TYPE_PERI:
			ret = __peri_clk_init(clk);
			break;

		case CLK_TYPE_BUS:
			ret = __bus_clk_init(clk);
			break;

		case CLK_TYPE_REF:
			ret = __ref_clk_init(clk);
			break;

		case CLK_TYPE_PLL:
			ret = __pll_clk_init(clk);
			break;

		case CLK_TYPE_PLL_CHNL:
			ret = __pll_chnl_clk_init(clk);
			break;
		case CLK_TYPE_MISC:
			ret = __misc_clk_init(clk);
			break;
		default:
			clk_dbg("%s - %s: unknown clk_type\n", __func__,
				clk->name);
			if (clk->ops && clk->ops->init)
				ret = clk->ops->init(clk);
			break;
		}
		clk->init = 1;
	}
	return ret;
}

static struct ccu_clk *get_ccu_clk(struct clk *clk)
{
	struct ccu_clk *ccu_clk = NULL;
	switch (clk->clk_type) {

	case CLK_TYPE_CCU:
		ccu_clk = to_ccu_clk(clk);
		break;

	case CLK_TYPE_PERI:
		ccu_clk = to_peri_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;

	case CLK_TYPE_BUS:
		ccu_clk = to_bus_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;

	case CLK_TYPE_REF:
		ccu_clk = to_ref_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;

	case CLK_TYPE_PLL:
		ccu_clk = to_pll_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;

	case CLK_TYPE_PLL_CHNL:
		ccu_clk = to_pll_chnl_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;

	case CLK_TYPE_CORE:
		ccu_clk = to_core_clk(clk)->ccu_clk;
		BUG_ON(ccu_clk == NULL);
		break;
	}
	return ccu_clk;
}

static int clk_lock(struct clk *clk, unsigned long *flags)
{
	struct ccu_clk *ccu_clk = get_ccu_clk(clk);
	if (ccu_clk)
		spin_lock_irqsave(&ccu_clk->lock, *flags);
	else
		spin_lock_irqsave(&clk_gen_lock, *flags);
	return 0;
}

static int clk_unlock(struct clk *clk, unsigned long *flags)
{
	struct ccu_clk *ccu_clk = get_ccu_clk(clk);
	if (ccu_clk)
		spin_unlock_irqrestore(&ccu_clk->lock, *flags);
	else
		spin_unlock_irqrestore(&clk_gen_lock, *flags);
	return 0;
}

int clk_init(struct clk *clk)
{
	int ret = 0;
	unsigned long flags;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	clk_dbg("%s - %s\n", __func__, clk->name);
	clk_lock(clk, &flags);
	ret = __clk_init(clk);
	clk_unlock(clk, &flags);

	return ret;
}

EXPORT_SYMBOL(clk_init);

static int __clk_reset(struct clk *clk)
{
	int ret = 0;

	if (!clk)
		return -EINVAL;

	if (!clk->ops || !clk->ops->reset)
		return -EINVAL;
	clk_dbg("%s - %s\n", __func__, clk->name);
	ret = clk->ops->reset(clk);

	return ret;
}

int clk_reset(struct clk *clk)
{
	int ret;
	unsigned long flags;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	clk_lock(clk, &flags);
	ret = __clk_reset(clk);
	clk_unlock(clk, &flags);

	return ret;
}

EXPORT_SYMBOL(clk_reset);

static int __ccu_clk_enable(struct clk *clk)
{
	int ret = 0;
	int inx;
	struct ccu_clk *ccu_clk;
	clk_dbg("%s ccu name:%s\n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);
	/*Make sure that all dependent & src clks are enabled/disabled*/
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Enabling dependant clock %s\n", __func__,
			clk->dep_clks[inx]->name);

		__clk_enable(clk->dep_clks[inx]);
	}
	/*enable PI */
#ifdef CONFIG_KONA_PI_MGR
	if (ccu_clk->pi_id != -1) {
		struct pi *pi = pi_mgr_get(ccu_clk->pi_id);

		BUG_ON(!pi);
		__pi_enable(pi);
	}
#endif
	if (clk->use_cnt++ == 0) {
		if (clk->ops && clk->ops->enable) {
			ret = clk->ops->enable(clk, 1);
		}
	}
	return ret;
}

static int __peri_clk_enable(struct clk *clk)
{
	struct peri_clk *peri_clk;
	int inx;
	int ret = 0;
	struct clk *src_clk;

	clk_dbg("%s clock name: %s \n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);
	BUG_ON(!peri_clk->ccu_clk);

	if (!(clk->flags & DONOT_NOTIFY_STATUS_TO_CCU)
	    && !(clk->flags & AUTO_GATE)) {
		clk_dbg("%s: peri clock %s enable CCU\n", __func__, clk->name);
		__ccu_clk_enable(&peri_clk->ccu_clk->clk);
	}

	/*Make sure that all dependent & src clks are enabled/disabled */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Enabling dependant clock %s \n", __func__,
			clk->dep_clks[inx]->name);

		__clk_enable(clk->dep_clks[inx]);
	}

	/*Enable src clock, if valid */
	if (PERI_SRC_CLK_VALID(peri_clk)) {
		src_clk = GET_PERI_SRC_CLK(peri_clk);
		BUG_ON(!src_clk);
		clk_dbg("%s, after enabling source clock %s\n", __func__,
			src_clk->name);
		__clk_enable(src_clk);
	}

	clk_dbg("%s:%s use count = %d\n", __func__, clk->name,
		peri_clk->clk.use_cnt);

	/*Increment usage count... return if already enabled */
	if (clk->use_cnt++ == 0) {
		CCU_ACCESS_EN(peri_clk->ccu_clk, 1);
		/*Update DFS request before enabling the clock */
#ifdef CONFIG_KONA_PI_MGR
		if (peri_clk->clk_dfs) {
			clk_dfs_request_update(clk, CLK_STATE_CHANGE, 1);
		}
#endif
		if (CLK_FLG_ENABLED(clk, ENABLE_HVT))
			peri_clk_set_voltage_lvl(peri_clk, VLT_HIGH);

		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);

		CCU_ACCESS_EN(peri_clk->ccu_clk, 0);
	}

	return ret;
}

static int __bus_clk_enable(struct clk *clk)
{
	struct bus_clk *bus_clk;
	int inx;
	int ret = 0;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);

	clk_dbg("%s : %s use cnt= %d\n", __func__, clk->name, clk->use_cnt);

	bus_clk = to_bus_clk(clk);

	BUG_ON(bus_clk->ccu_clk == NULL);

	if (!(clk->flags & AUTO_GATE) && (clk->flags & NOTIFY_STATUS_TO_CCU)) {
		clk_dbg("%s: bus clock %s incrementing CCU count\n", __func__,
			clk->name);
		__ccu_clk_enable(&bus_clk->ccu_clk->clk);
	}

	/*Make sure that all dependent & src clks are enabled/disabled */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Enabling dependant clock %s \n", __func__,
			clk->dep_clks[inx]->name);
		__clk_enable(clk->dep_clks[inx]);
	}

	if (bus_clk->src_clk) {
		clk_dbg("%s src clock %s enable \n", __func__,
			bus_clk->src_clk->name);
		__clk_enable(bus_clk->src_clk);
	}

	/*Increment usage count... return if already enabled */
	if (clk->use_cnt++ == 0) {
		CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
		/*Update DFS request before enabling the clock */
#ifdef CONFIG_KONA_PI_MGR
		if (bus_clk->clk_dfs) {
			clk_dfs_request_update(clk, CLK_STATE_CHANGE, 1);
		}
#endif
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);

		CCU_ACCESS_EN(bus_clk->ccu_clk, 0);
	}
	return ret;

}

static int __ref_clk_enable(struct clk *clk)
{
	int ret = 0;
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);

	if (clk->use_cnt++ == 0) {
		CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);

		CCU_ACCESS_EN(ref_clk->ccu_clk, 0);
	}
	return ret;
}

static int __pll_clk_enable(struct clk *clk)
{
	int ret = 0;
	struct pll_clk *pll_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	if (clk->use_cnt++ == 0) {
		CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);

		CCU_ACCESS_EN(pll_clk->ccu_clk, 0);
	}
	return ret;
}

static int __pll_chnl_clk_enable(struct clk *clk)
{
	int ret = 0;
	struct pll_chnl_clk *pll_chnl_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	__pll_clk_enable(&pll_chnl_clk->pll_clk->clk);

	if (clk->use_cnt++ == 0) {
		CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);

		CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);
	}
	return ret;
}
static int __misc_clk_enable(struct clk *clk)
{
	int ret = 0;
	int inx;

	/*Make sure that all dependent clks are enabled*/
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Disabling dependant clock %s\n", __func__,
			clk->dep_clks[inx]->name);
		__clk_enable(clk->dep_clks[inx]);
	}
	if (clk->use_cnt++ == 0) {
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);
	}
	return ret;

}

int __clk_enable(struct clk *clk)
{
	int ret = 0;
	clk_dbg("%s - %s\n", __func__, clk->name);
	switch (clk->clk_type) {
	case CLK_TYPE_CCU:
		ret = __ccu_clk_enable(clk);
		break;

	case CLK_TYPE_PERI:
		ret = __peri_clk_enable(clk);
		break;

	case CLK_TYPE_BUS:
		ret = __bus_clk_enable(clk);
		break;

	case CLK_TYPE_REF:
		ret = __ref_clk_enable(clk);
		break;

	case CLK_TYPE_PLL:
		ret = __pll_clk_enable(clk);
		break;

	case CLK_TYPE_PLL_CHNL:
		ret = __pll_chnl_clk_enable(clk);
		break;
	case CLK_TYPE_MISC:
		ret = __misc_clk_enable(clk);
		break;
	default:
		clk_dbg("%s - %s: unknown clk_type\n", __func__, clk->name);
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 1);
		else {
			clk_dbg
			    ("%s - %s: unknown clk_type & func ptr == NULL \n",
			     __func__, clk->name);
		}
		break;
	}
	return ret;
}

int clk_enable(struct clk *clk)
{
	int ret;
	unsigned long flags;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	clk_lock(clk, &flags);
	ret = __clk_enable(clk);
	clk_unlock(clk, &flags);

	return ret;
}

EXPORT_SYMBOL(clk_enable);

static int __ccu_clk_disable(struct clk *clk)
{
	int ret = 0;
	int inx;
	struct ccu_clk *ccu_clk;
	clk_dbg("%s ccu name:%s\n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	if (clk->use_cnt && --clk->use_cnt == 0) {
		if (clk->ops && clk->ops->enable) {
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
			ret = clk->ops->enable(clk, 0);
#endif
		}
	}
	/*disable PI */
#ifdef CONFIG_KONA_PI_MGR
	if (ccu_clk->pi_id != -1) {
		struct pi *pi = pi_mgr_get(ccu_clk->pi_id);

		BUG_ON(!pi);
		__pi_disable(pi);
	}
#endif
	/*Make sure that all dependent & src clks are disabled*/
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Disabling dependant clock %s\n", __func__,
				clk->dep_clks[inx]->name);
		__clk_disable(clk->dep_clks[inx]);
	}

	return ret;
}

static int __peri_clk_disable(struct clk *clk)
{
	struct peri_clk *peri_clk;
	int inx;
	struct clk *src_clk;
	int ret = 0;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);

	peri_clk = to_peri_clk(clk);
	clk_dbg("%s: clock name: %s \n", __func__, clk->name);

	BUG_ON(!peri_clk->ccu_clk);

	/*decrment usage count... return if already disabled or usage count is non-zero */
	if (clk->use_cnt && --clk->use_cnt == 0) {
		CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif
		if (clk->flags & ENABLE_HVT)
			peri_clk_set_voltage_lvl(peri_clk, VLT_NORMAL);

		/*update DFS request */

#ifdef CONFIG_KONA_PI_MGR
		if (peri_clk->clk_dfs) {
			clk_dfs_request_update(clk, CLK_STATE_CHANGE, 0);
		}
#endif

		CCU_ACCESS_EN(peri_clk->ccu_clk, 0);
	}

	/*Make sure that all dependent & src clks are disabled */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Disabling dependant clock %s \n", __func__,
			clk->dep_clks[inx]->name);
		__clk_disable(clk->dep_clks[inx]);
	}

	if (PERI_SRC_CLK_VALID(peri_clk)) {
		src_clk = GET_PERI_SRC_CLK(peri_clk);
		BUG_ON(!src_clk);
		clk_dbg("%s, disabling source clock \n", __func__);

		__clk_disable(src_clk);
	}

	if (!(clk->flags & DONOT_NOTIFY_STATUS_TO_CCU)
	    && !(clk->flags & AUTO_GATE)) {
		clk_dbg("%s: peri clock %s decrementing CCU count\n", __func__,
			clk->name);
		__ccu_clk_disable(&peri_clk->ccu_clk->clk);
	}
	return ret;
}

static int __bus_clk_disable(struct clk *clk)
{
	struct bus_clk *bus_clk;
	int ret = 0;
	int inx;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);

	clk_dbg("%s : %s use cnt= %d\n", __func__, clk->name, clk->use_cnt);

	bus_clk = to_bus_clk(clk);
	BUG_ON(bus_clk->ccu_clk == NULL);

	/*decrment usage count... return if already disabled or usage count is non-zero */
	if (clk->use_cnt && --clk->use_cnt == 0) {
		CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif

		/*update DFS request */
#ifdef CONFIG_KONA_PI_MGR
		if (bus_clk->clk_dfs) {
			clk_dfs_request_update(clk, CLK_STATE_CHANGE, 0);
		}
#endif

		CCU_ACCESS_EN(bus_clk->ccu_clk, 0);
	}

	/*Disable dependent & src clks */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s, Disabling dependant clock %s \n", __func__,
			clk->dep_clks[inx]->name);
		__clk_disable(clk->dep_clks[inx]);
	}

	if (bus_clk->src_clk) {
		clk_dbg("%s src clock %s disable \n", __func__,
			bus_clk->src_clk->name);
		__clk_disable(bus_clk->src_clk);
	}

	if (!(clk->flags & AUTO_GATE) && (clk->flags & NOTIFY_STATUS_TO_CCU)) {
		clk_dbg("%s: bus clock %s decrementing CCU count\n", __func__,
			clk->name);
		__ccu_clk_disable(&bus_clk->ccu_clk->clk);
	}

	return ret;
}

static int __ref_clk_disable(struct clk *clk)
{
	int ret = 0;
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);

	if (clk->use_cnt && --clk->use_cnt == 0) {
		CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif

		CCU_ACCESS_EN(ref_clk->ccu_clk, 0);
	}
	return ret;
}

static int __pll_clk_disable(struct clk *clk)
{
	int ret = 0;
	struct pll_clk *pll_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	if (clk->use_cnt && --clk->use_cnt == 0) {
		CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif

		CCU_ACCESS_EN(pll_clk->ccu_clk, 0);
	}
	return ret;
}

static int __pll_chnl_clk_disable(struct clk *clk)
{
	int ret = 0;
	struct pll_chnl_clk *pll_chnl_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	if (clk->use_cnt && --clk->use_cnt == 0) {
		CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif

		CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);
	}
	__pll_clk_disable(&pll_chnl_clk->pll_clk->clk);
	return ret;
}

static int __misc_clk_disable(struct clk *clk)
{
	int inx, ret = 0;
	if (clk->use_cnt && --clk->use_cnt == 0) {
		/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
#endif
	}
	/*Make sure that all dependent clks are disabled*/
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx];
		inx++) {
		clk_dbg("%s, Disabling dependant clock %s\n", __func__,
			clk->dep_clks[inx]->name);
		__clk_disable(clk->dep_clks[inx]);
	}

	return ret;
}

void __clk_disable(struct clk *clk)
{
	int ret = 0;
	clk_dbg("%s - %s\n", __func__, clk->name);
	/**Return if the clk is already in disabled state*/
	if (clk->use_cnt == 0)
		return;

	switch (clk->clk_type) {
	case CLK_TYPE_CCU:
		ret = __ccu_clk_disable(clk);
		break;

	case CLK_TYPE_PERI:
		ret = __peri_clk_disable(clk);
		break;

	case CLK_TYPE_BUS:
		ret = __bus_clk_disable(clk);
		break;

	case CLK_TYPE_REF:
		ret = __ref_clk_disable(clk);
		break;

	case CLK_TYPE_PLL:
		ret = __pll_clk_disable(clk);
		break;

	case CLK_TYPE_PLL_CHNL:
		ret = __pll_chnl_clk_disable(clk);
		break;
	case CLK_TYPE_MISC:
		ret = __misc_clk_disable(clk);
		break;

	default:
		clk_dbg("%s - %s: unknown clk_type\n", __func__, clk->name);
/*Debug interface to avoid clk disable*/
#ifndef CONFIG_KONA_PM_NO_CLK_DISABLE
		if (clk->ops && clk->ops->enable)
			ret = clk->ops->enable(clk, 0);
		else {
			clk_dbg
			    ("%s - %s: unknown clk_type & func ptr == NULL \n",
			     __func__, clk->name);
		}
#endif /*CONFIG_KONA_PM_NO_CLK_DISABLE */
		break;
	}
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (IS_ERR_OR_NULL(clk))
		return;

	clk_lock(clk, &flags);
	__clk_disable(clk);
	clk_unlock(clk, &flags);
}

EXPORT_SYMBOL(clk_disable);

static unsigned long __ccu_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct ccu_clk *ccu_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	CCU_ACCESS_EN(ccu_clk, 1);
	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(ccu_clk, 0);

	return rate;
}

static unsigned long __peri_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct peri_clk *peri_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return rate;
}

static unsigned long __bus_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct bus_clk *bus_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);
	bus_clk = to_bus_clk(clk);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return rate;
}

static unsigned long __ref_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return rate;
}

static unsigned long __pll_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct pll_clk *pll_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return rate;
}

static unsigned long __pll_chnl_clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	struct pll_chnl_clk *pll_chnl_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->get_rate)
		rate = clk->ops->get_rate(clk);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);

	return rate;
}

static unsigned long __clk_get_rate(struct clk *clk)
{
	unsigned long rate = 0;
	clk_dbg("%s - %s\n", __func__, clk->name);
	switch (clk->clk_type) {
	case CLK_TYPE_CCU:
		rate = __ccu_clk_get_rate(clk);
		break;

	case CLK_TYPE_PERI:
		rate = __peri_clk_get_rate(clk);
		break;

	case CLK_TYPE_BUS:
		rate = __bus_clk_get_rate(clk);
		break;

	case CLK_TYPE_REF:
		rate = __ref_clk_get_rate(clk);
		break;

	case CLK_TYPE_PLL:
		rate = __pll_clk_get_rate(clk);
		break;

	case CLK_TYPE_PLL_CHNL:
		rate = __pll_chnl_clk_get_rate(clk);
		break;

	default:
		clk_dbg("%s - %s: unknown clk_type\n", __func__, clk->name);
		if (clk->ops && clk->ops->get_rate)
			rate = clk->ops->get_rate(clk);
		else {
			clk_dbg
			    ("%s - %s: unknown clk_type & func ptr == NULL \n",
			     __func__, clk->name);
		}
		break;
	}

	return rate;
}

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags, rate;

	if (IS_ERR_OR_NULL(clk) || !clk->ops || !clk->ops->get_rate)
		return -EINVAL;

	clk_lock(clk, &flags);
	rate = __clk_get_rate(clk);
	clk_unlock(clk, &flags);
	return rate;
}

EXPORT_SYMBOL(clk_get_rate);

static long __ccu_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	struct ccu_clk *ccu_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	CCU_ACCESS_EN(ccu_clk, 1);
	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(ccu_clk, 0);

	return actual;
}

static long __peri_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	struct peri_clk *peri_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return actual;
}

static long __bus_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	struct bus_clk *bus_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);
	bus_clk = to_bus_clk(clk);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return actual;
}

static unsigned long __ref_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return actual;
}

static unsigned long __pll_clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	struct pll_clk *pll_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return actual;
}

static unsigned long __pll_chnl_clk_round_rate(struct clk *clk,
					       unsigned long rate)
{
	long actual = 0;
	struct pll_chnl_clk *pll_chnl_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->round_rate)
		actual = clk->ops->round_rate(clk, rate);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);

	return actual;
}

static long __clk_round_rate(struct clk *clk, unsigned long rate)
{
	long actual = 0;
	clk_dbg("%s - %s\n", __func__, clk->name);
	switch (clk->clk_type) {
	case CLK_TYPE_CCU:
		actual = __ccu_clk_round_rate(clk, rate);
		break;

	case CLK_TYPE_PERI:
		actual = __peri_clk_round_rate(clk, rate);
		break;

	case CLK_TYPE_BUS:
		actual = __bus_clk_round_rate(clk, rate);
		break;

	case CLK_TYPE_REF:
		actual = __ref_clk_round_rate(clk, rate);
		break;

	case CLK_TYPE_PLL:
		actual = __pll_clk_round_rate(clk, rate);
		break;

	case CLK_TYPE_PLL_CHNL:
		actual = __pll_chnl_clk_round_rate(clk, rate);
		break;

	default:
		clk_dbg("%s - %s: unknown clk_type\n", __func__, clk->name);
		if (clk->ops && clk->ops->round_rate)
			actual = clk->ops->round_rate(clk, rate);
		else {
			clk_dbg
			    ("%s - %s: unknown clk_type & func ptr == NULL \n",
			     __func__, clk->name);
		}
		break;
	}

	return actual;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags, actual;

	if (IS_ERR_OR_NULL(clk) || !clk->ops || !clk->ops->round_rate)
		return -EINVAL;

	clk_lock(clk, &flags);
	actual = __clk_round_rate(clk, rate);
	clk_unlock(clk, &flags);

	return actual;
}

EXPORT_SYMBOL(clk_round_rate);

static long __ccu_clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	struct ccu_clk *ccu_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	CCU_ACCESS_EN(ccu_clk, 1);
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

	CCU_ACCESS_EN(ccu_clk, 0);

	return ret;
}

static long __peri_clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	struct peri_clk *peri_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

#ifdef CONFIG_KONA_PI_MGR
	if (peri_clk->clk_dfs) {
		clk_dfs_request_update(clk, CLK_RATE_CHANGE,
				       __clk_get_rate(clk));
	}
#endif

	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return ret;
}

static long __bus_clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	struct bus_clk *bus_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);
	bus_clk = to_bus_clk(clk);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return ret;
}

static unsigned long __ref_clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return ret;
}

static unsigned long __pll_clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	struct pll_clk *pll_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return ret;
}

static unsigned long __pll_chnl_clk_set_rate(struct clk *clk,
					     unsigned long rate)
{
	long ret = 0;
	struct pll_chnl_clk *pll_chnl_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);

	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);

	return ret;
}

static long __clk_set_rate(struct clk *clk, unsigned long rate)
{
	long ret = 0;
	clk_dbg("%s - %s\n", __func__, clk->name);
	switch (clk->clk_type) {
	case CLK_TYPE_CCU:
		ret = __ccu_clk_set_rate(clk, rate);
		break;

	case CLK_TYPE_PERI:
		ret = __peri_clk_set_rate(clk, rate);
		break;

	case CLK_TYPE_BUS:
		ret = __bus_clk_set_rate(clk, rate);
		break;

	case CLK_TYPE_REF:
		ret = __ref_clk_set_rate(clk, rate);
		break;

	case CLK_TYPE_PLL:
		ret = __pll_clk_set_rate(clk, rate);
		break;

	case CLK_TYPE_PLL_CHNL:
		ret = __pll_chnl_clk_set_rate(clk, rate);
		break;

	default:
		clk_dbg("%s - %s: unknown clk_type\n", __func__, clk->name);
		if (clk->ops && clk->ops->set_rate)
			ret = clk->ops->set_rate(clk, rate);
		else {
			clk_dbg
			    ("%s - %s: unknown clk_type & func ptr == NULL \n",
			     __func__, clk->name);
		}
		break;
	}
	return ret;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret;

	if (IS_ERR_OR_NULL(clk) || !clk->ops || !clk->ops->set_rate)
		return -EINVAL;

	clk_lock(clk, &flags);
	ret = __clk_set_rate(clk, rate);
	clk_unlock(clk, &flags);

	return ret;
}

EXPORT_SYMBOL(clk_set_rate);

int clk_get_usage(struct clk *clk)
{
	unsigned long flags;
	int ret;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	clk_lock(clk, &flags);
	ret = clk->use_cnt;
	clk_unlock(clk, &flags);

	return ret;
}

EXPORT_SYMBOL(clk_get_usage);

static int clk_is_enabled(struct clk *clk)
{
	return (!!clk->use_cnt);
}

#ifdef CONFIG_KONA_PI_MGR
int clk_dfs_request_update(struct clk *clk, u32 action, u32 param)
{
	struct clk_dfs *clk_dfs;
	struct pi_mgr_dfs_node *dfs_node = NULL;
	struct dfs_rate_thold *thold = NULL;

	if (clk->clk_type == CLK_TYPE_PERI) {
		struct peri_clk *peri_clk;
		peri_clk = to_peri_clk(clk);
		clk_dfs = peri_clk->clk_dfs;
		if (clk_dfs)
			dfs_node = &peri_clk->clk_dfs->dfs_node;
	} else if (clk->clk_type == CLK_TYPE_BUS) {
		struct bus_clk *bus_clk;
		bus_clk = to_bus_clk(clk);
		clk_dfs = bus_clk->clk_dfs;
		if (clk_dfs)
			dfs_node = &bus_clk->clk_dfs->dfs_node;
	} else
		BUG();

	BUG_ON(!clk_dfs || !dfs_node);

	switch (clk_dfs->dfs_policy) {
	case CLK_DFS_POLICY_STATE:
		if (action == CLK_STATE_CHANGE) {
			if (param) {	/*enable ? */

				pi_mgr_dfs_request_update_ex(dfs_node,
							     clk_dfs->
							     policy_param,
							     (clk_dfs->
							      policy_param ==
							      PI_MGR_DFS_MIN_VALUE)
							     ?
							     PI_MGR_DFS_WIEGHTAGE_NONE
							     : clk_dfs->
							     opp_weightage
							     [clk_dfs->
							      policy_param]);
			} else {
				pi_mgr_dfs_request_update_ex(dfs_node,
							     PI_MGR_DFS_MIN_VALUE,
							     PI_MGR_DFS_WIEGHTAGE_NONE);
			}
		}
		break;

	case CLK_DFS_POLICY_RATE:
		if (action == CLK_STATE_CHANGE && param == 0) {
			pi_mgr_dfs_request_update_ex(dfs_node,
						     PI_MGR_DFS_MIN_VALUE,
						     PI_MGR_DFS_WIEGHTAGE_NONE);

		} else {
			u32 rate = 0, inx;
			if (action == CLK_STATE_CHANGE)	/*enable */
				rate = __clk_get_rate(clk);
			else if (action == CLK_RATE_CHANGE) {
				if (!clk_is_enabled(clk))
					return 0;
				rate = param;
			} else
				BUG();

			thold = (struct dfs_rate_thold *)clk_dfs->policy_param;
			for (inx = 0; inx < PI_OPP_MAX; inx++) {
				if (rate <= thold[inx].rate_thold ||
				    thold[inx].rate_thold == -1) {
					break;
				}

			}
			BUG_ON(inx == PI_OPP_MAX);
			pi_mgr_dfs_request_update_ex(dfs_node, thold[inx].opp,
						     (thold[inx].opp ==
						      PI_MGR_DFS_MIN_VALUE) ?
						     PI_MGR_DFS_WIEGHTAGE_NONE :
						     clk_dfs->
						     opp_weightage[thold[inx].
								   opp]);

		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#endif /*CONFIG_KONA_PI_MGR */

int ccu_init_state_save_buf(struct ccu_clk *ccu_clk)
{
	int ret = 0;
	int i;

	struct ccu_state_save *ccu_state_save = ccu_clk->ccu_state_save;
	if (!ccu_state_save)
		return ret;
	ccu_state_save->num_reg = 0;
	for (i = 0; i < ccu_state_save->reg_set_count; i++) {
		BUG_ON(ccu_state_save->reg_save[i].offset_end <
		       ccu_state_save->reg_save[i].offset_start);
		ccu_state_save->num_reg +=
		    (ccu_state_save->reg_save[i].offset_end -
		     ccu_state_save->reg_save[i].offset_start +
		     sizeof(u32)) / sizeof(u32);
	}
	clk_dbg("%s:num_reg = %d\n", __func__, ccu_state_save->num_reg);

	/*Set save flag to false by default */
	if (!ret)
		ccu_state_save->save_buf[ccu_state_save->num_reg] = 0;
	return ret;
}

int clk_register(struct clk_lookup *clk_lkup, int num_clks)
{
	int ret = 0;
	int i;

	for (i = 0; i < num_clks; i++) {
		clkdev_add(&clk_lkup[i]);
		clk_dbg("clock registered - %s\n", clk_lkup[i].clk->name);
	}
	for (i = 0; i < num_clks; i++) {
		/*Init per-ccu spin lock */
		if (clk_lkup[i].clk->clk_type == CLK_TYPE_CCU) {
			struct ccu_clk *ccu_clk = to_ccu_clk(clk_lkup[i].clk);
			spin_lock_init(&ccu_clk->lock);
		}
		ret |= clk_init(clk_lkup[i].clk);
		if (ret)
			pr_info("%s: clk %s init failed !!!\n", __func__,
				clk_lkup[i].clk->name);
	}
	return ret;
}

EXPORT_SYMBOL(clk_register);

int ccu_reset_write_access_enable(struct ccu_clk *ccu_clk, int enable)
{
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->rst_write_access)
		return -EINVAL;
	return ccu_clk->ccu_ops->rst_write_access(ccu_clk, enable);
}

EXPORT_SYMBOL(ccu_reset_write_access_enable);

/*CCU reset access functions */
static int ccu_rst_write_access_enable(struct ccu_clk *ccu_clk, int enable)
{
	u32 reg_val = 0;

	reg_val = CLK_WR_ACCESS_PASSWORD << CLK_WR_PASSWORD_SHIFT;
	if (enable) {
		if (ccu_clk->rst_write_access_en_count++ != 0)
			return 0;
		reg_val |= CLK_WR_ACCESS_EN;
	} else if (ccu_clk->rst_write_access_en_count == 0
		   || --ccu_clk->rst_write_access_en_count != 0)
		return 0;
	writel(reg_val,
	       ccu_clk->ccu_reset_mgr_base + ccu_clk->reset_wr_access_offset);

	reg_val =
	    readl(ccu_clk->ccu_reset_mgr_base +
		  ccu_clk->reset_wr_access_offset);
	clk_dbg("rst mgr access %s: reg value: %08x\n",
		enable ? "enabled" : "disabled", reg_val);

	return 0;
}

int ccu_write_access_enable(struct ccu_clk *ccu_clk, int enable)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->write_access)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->write_access(ccu_clk, enable);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_write_access_enable);

int ccu_policy_engine_resume(struct ccu_clk *ccu_clk, int load_type)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops ||
	    !ccu_clk->ccu_ops->policy_engine_resume)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->policy_engine_resume(ccu_clk, load_type);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_policy_engine_resume);

int ccu_policy_engine_stop(struct ccu_clk *ccu_clk)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops ||
	    !ccu_clk->ccu_ops->policy_engine_stop)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->policy_engine_stop(ccu_clk);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_policy_engine_stop);

int ccu_set_policy_ctrl(struct ccu_clk *ccu_clk, int pol_ctrl_id, int action)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops ||
	    !ccu_clk->ccu_ops->set_policy_ctrl)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->set_policy_ctrl(ccu_clk, pol_ctrl_id, action);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_set_policy_ctrl);

int ccu_int_enable(struct ccu_clk *ccu_clk, int int_type, int enable)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops ||
	    !ccu_clk->ccu_ops->int_enable)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->int_enable(ccu_clk, int_type, enable);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_int_enable);

int ccu_int_status_clear(struct ccu_clk *ccu_clk, int int_type)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops ||
	    !ccu_clk->ccu_ops->int_status_clear)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->int_status_clear(ccu_clk, int_type);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_int_status_clear);

int ccu_set_freq_policy(struct ccu_clk *ccu_clk, int policy_id, int freq_id)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->set_freq_policy)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->set_freq_policy(ccu_clk, policy_id, freq_id);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_set_freq_policy);

int ccu_get_freq_policy(struct ccu_clk *ccu_clk, int policy_id)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->get_freq_policy)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->get_freq_policy(ccu_clk, policy_id);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_get_freq_policy);

int ccu_set_peri_voltage(struct ccu_clk *ccu_clk, int peri_volt_id, u8 voltage)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->set_peri_voltage)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret =
	    ccu_clk->ccu_ops->set_peri_voltage(ccu_clk, peri_volt_id, voltage);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_set_peri_voltage);

int ccu_set_voltage(struct ccu_clk *ccu_clk, int volt_id, u8 voltage)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->set_voltage)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->set_voltage(ccu_clk, volt_id, voltage);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_set_voltage);

int ccu_get_voltage(struct ccu_clk *ccu_clk, int freq_id)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->get_voltage)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->get_voltage(ccu_clk, freq_id);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_get_voltage);

int ccu_set_active_policy(struct ccu_clk *ccu_clk, u32 policy)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->set_active_policy)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->set_active_policy(ccu_clk, policy);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;

}

EXPORT_SYMBOL(ccu_set_active_policy);

int ccu_get_active_policy(struct ccu_clk *ccu_clk)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->get_active_policy)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->get_active_policy(ccu_clk);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}

EXPORT_SYMBOL(ccu_get_active_policy);

int ccu_save_state(struct ccu_clk *ccu_clk, int save)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->save_state)
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->save_state(ccu_clk, save);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}
EXPORT_SYMBOL(ccu_save_state);

int ccu_get_dbg_bus_status(struct ccu_clk *ccu_clk)
{
	int ret;

	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->get_dbg_bus_status
		|| !CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN))
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->get_dbg_bus_status(ccu_clk);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}
EXPORT_SYMBOL(ccu_get_dbg_bus_status);

int ccu_set_dbg_bus_sel(struct ccu_clk *ccu_clk, u32 sel)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->set_dbg_bus_sel
		|| !CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN))
		return -EINVAL;
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->set_dbg_bus_sel(ccu_clk, sel);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}
EXPORT_SYMBOL(ccu_set_dbg_bus_sel);

int ccu_get_dbg_bus_sel(struct ccu_clk *ccu_clk)
{
	int ret;
	if (IS_ERR_OR_NULL(ccu_clk) || !ccu_clk->ccu_ops
	    || !ccu_clk->ccu_ops->get_dbg_bus_sel
		|| !CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN))
		return -EINVAL;

	CCU_ACCESS_EN(ccu_clk, 1);
	ret = ccu_clk->ccu_ops->get_dbg_bus_sel(ccu_clk);
	CCU_ACCESS_EN(ccu_clk, 0);
	return ret;
}
EXPORT_SYMBOL(ccu_get_dbg_bus_sel);

int ccu_print_sleep_prevent_clks(struct clk *clk)
{
	struct ccu_clk *ccu_clk;
	struct clk *clk_iter;
	int use_cnt;
	int sleep_prevent = 0;

	if (!clk || (clk->clk_type != CLK_TYPE_CCU))
		return -EINVAL;
	ccu_clk = to_ccu_clk(clk);
	list_for_each_entry(clk_iter, &ccu_clk->clk_list, list) {
		use_cnt = clk_get_usage(clk_iter);
		switch (clk_iter->clk_type) {
		case CLK_TYPE_REF:
			/**
			 * ignore reference clks as they dont prevent
			 * retention
			 */
			sleep_prevent = 0;
			break;
		case CLK_TYPE_PLL:
		case CLK_TYPE_PLL_CHNL:
		case CLK_TYPE_CORE:
			if (use_cnt && !CLK_FLG_ENABLED(clk_iter, AUTO_GATE))
				sleep_prevent = 1;
			break;
		case CLK_TYPE_BUS:
			sleep_prevent = (CLK_FLG_ENABLED(clk_iter,
					NOTIFY_STATUS_TO_CCU) &&
				!CLK_FLG_ENABLED(clk_iter, AUTO_GATE));
			break;
		case CLK_TYPE_PERI:
			sleep_prevent = !CLK_FLG_ENABLED(clk_iter,
					DONOT_NOTIFY_STATUS_TO_CCU);
			break;
		default:
			break;
		}
		if (use_cnt && sleep_prevent)
      //      pr_info("%20s %10d", clk_iter->name, clk_iter->use_cnt);
      printk("%20s %10d", clk_iter->name, clk_iter->use_cnt);
	}
	return 0;
}
EXPORT_SYMBOL(ccu_print_sleep_prevent_clks);


/*CCU access functions */
static int ccu_clk_write_access_enable(struct ccu_clk *ccu_clk, int enable)
{
	u32 reg_val = 0;

	reg_val = CLK_WR_ACCESS_PASSWORD << CLK_WR_PASSWORD_SHIFT;

	if (enable) {
		if (ccu_clk->write_access_en_count++ != 0)
			return 0;
		reg_val |= CLK_WR_ACCESS_EN;
	} else if (ccu_clk->write_access_en_count == 0
		   || --ccu_clk->write_access_en_count != 0)
		return 0;
	writel(reg_val, CCU_WR_ACCESS_REG(ccu_clk));

	return 0;
}

static int ccu_clk_policy_engine_resume(struct ccu_clk *ccu_clk, int load_type)
{
	u32 reg_val = 0;
	u32 insurance = 10000;

	if (ccu_clk->pol_engine_dis_cnt == 0)
		return 0;	/*Already in running state ?? */

	else if (--ccu_clk->pol_engine_dis_cnt != 0)
		return 0;	/*disable count is non-zero */

	/*Set trigger */
	reg_val = readl(CCU_POLICY_CTRL_REG(ccu_clk));
	if (load_type == CCU_LOAD_ACTIVE)
		reg_val |= CCU_POLICY_CTL_ATL << CCU_POLICY_CTL_GO_ATL_SHIFT;
	else
		reg_val &= ~(CCU_POLICY_CTL_ATL << CCU_POLICY_CTL_GO_ATL_SHIFT);
	reg_val |= CCU_POLICY_CTL_GO_TRIG << CCU_POLICY_CTL_GO_SHIFT;

	writel(reg_val, CCU_POLICY_CTRL_REG(ccu_clk));

	while ((readl(CCU_POLICY_CTRL_REG(ccu_clk)) & CCU_POLICY_CTL_GO_MASK)
	       && insurance) {
		udelay(1);
		insurance--;
	}
	BUG_ON(insurance == 0);

	reg_val = readl(CCU_POLICY_CTRL_REG(ccu_clk));
	if ((load_type == CCU_LOAD_TARGET)
	    && (reg_val & CCU_POLICY_CTL_TGT_VLD_MASK)) {
		reg_val |=
		    CCU_POLICY_CTL_TGT_VLD << CCU_POLICY_CTL_TGT_VLD_SHIFT;
		writel(reg_val, CCU_POLICY_CTRL_REG(ccu_clk));
	}

	return 0;
}

static int ccu_clk_policy_engine_stop(struct ccu_clk *ccu_clk)
{
	u32 reg_val = 0;
	u32 insurance = 10000;

	if (ccu_clk->pol_engine_dis_cnt++ != 0)
		return 0;	/*Already in disabled state */

	reg_val = (CCU_POLICY_OP_EN << CCU_POLICY_CONFIG_EN_SHIFT);
	writel(reg_val, CCU_LVM_EN_REG(ccu_clk));
	while ((readl(CCU_LVM_EN_REG(ccu_clk)) & CCU_POLICY_CONFIG_EN_MASK) &&
	       insurance) {
		udelay(1);
		insurance--;
	}
	BUG_ON(insurance == 0);

	return 0;
}

static int ccu_clk_set_policy_ctrl(struct ccu_clk *ccu_clk, int pol_ctrl_id,
				   int action)
{
	u32 reg_val = 0;
	u32 shift;

	switch (pol_ctrl_id) {
	case POLICY_CTRL_GO:
		shift = CCU_POLICY_CTL_GO_SHIFT;
		break;
	case POLICY_CTRL_GO_AC:
		shift = CCU_POLICY_CTL_GO_AC_SHIFT;
		break;
	case POLICY_CTRL_GO_ATL:
		shift = CCU_POLICY_CTL_GO_ATL_SHIFT;
		break;
	case POLICY_CTRL_TGT_VLD:
		shift = CCU_POLICY_CTL_TGT_VLD_SHIFT;
		break;
	default:
		return -EINVAL;
	}
	reg_val = readl(CCU_POLICY_CTRL_REG(ccu_clk));
	reg_val |= action << shift;

	writel(reg_val, CCU_POLICY_CTRL_REG(ccu_clk));
	return 0;
}

static int ccu_clk_int_enable(struct ccu_clk *ccu_clk, int int_type, int enable)
{
	u32 reg_val = 0;
	u32 shift;

	if (int_type == ACT_INT)
		shift = CCU_ACT_INT_SHIFT;
	else if (int_type == TGT_INT)
		shift = CCU_TGT_INT_SHIFT;
	else
		return -EINVAL;

	reg_val = readl(CCU_INT_EN_REG(ccu_clk));

	if (enable)
		reg_val |= (CCU_INT_EN << shift);
	else
		reg_val &= ~(CCU_INT_EN << shift);

	writel(reg_val, CCU_INT_EN_REG(ccu_clk));
	return 0;
}

static int ccu_clk_int_status_clear(struct ccu_clk *ccu_clk, int int_type)
{
	u32 reg_val = 0;
	u32 shift;

	if (int_type == ACT_INT)
		shift = CCU_ACT_INT_SHIFT;
	else if (int_type == TGT_INT)
		shift = CCU_TGT_INT_SHIFT;
	else
		return -EINVAL;

	reg_val = readl(CCU_INT_STATUS_REG(ccu_clk));
	reg_val |= (CCU_INT_STATUS_CLR << shift);
	writel(reg_val, CCU_INT_STATUS_REG(ccu_clk));

	return 0;
}

static int ccu_clk_set_freq_policy(struct ccu_clk *ccu_clk, int policy_id,
				   int freq_id)
{
	u32 reg_val = 0;
	u32 shift;

	clk_dbg("%s:%s ccu , freq_id = %d policy_id = %d\n", __func__,
		ccu_clk->clk.name, freq_id, policy_id);

	if (freq_id >= ccu_clk->freq_count)
		return -EINVAL;

	switch (policy_id) {
	case CCU_POLICY0:
		shift = CCU_FREQ_POLICY0_SHIFT;
		break;
	case CCU_POLICY1:
		shift = CCU_FREQ_POLICY1_SHIFT;
		break;
	case CCU_POLICY2:
		shift = CCU_FREQ_POLICY2_SHIFT;
		break;
	case CCU_POLICY3:
		shift = CCU_FREQ_POLICY3_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	reg_val = readl(CCU_POLICY_FREQ_REG(ccu_clk));
	clk_dbg("%s: reg_val:%08x shift:%d\n", __func__, reg_val, shift);
	reg_val &= ~(CCU_FREQ_POLICY_MASK << shift);

	reg_val |= freq_id << shift;

	ccu_write_access_enable(ccu_clk, true);
	ccu_policy_engine_stop(ccu_clk);

	writel(reg_val, CCU_POLICY_FREQ_REG(ccu_clk));
	ccu_policy_engine_resume(ccu_clk,
				 ccu_clk->clk.
				 flags & CCU_TARGET_LOAD ? CCU_LOAD_TARGET :
				 CCU_LOAD_ACTIVE);
	ccu_write_access_enable(ccu_clk, false);

	clk_dbg("%s:%s ccu OK\n", __func__, ccu_clk->clk.name);
	return 0;
}

static int ccu_clk_get_freq_policy(struct ccu_clk *ccu_clk, int policy_id)
{
	u32 shift, reg_val;

	switch (policy_id) {
	case CCU_POLICY0:
		shift = CCU_FREQ_POLICY0_SHIFT;
		break;
	case CCU_POLICY1:
		shift = CCU_FREQ_POLICY1_SHIFT;
		break;
	case CCU_POLICY2:
		shift = CCU_FREQ_POLICY2_SHIFT;
		break;
	case CCU_POLICY3:
		shift = CCU_FREQ_POLICY3_SHIFT;
		break;
	default:
		return CCU_FREQ_INVALID;

	}
	reg_val = readl(CCU_POLICY_FREQ_REG(ccu_clk));
	clk_dbg("%s: reg_val:%08x shift:%d\n", __func__, reg_val, shift);

	return ((reg_val >> shift) & CCU_FREQ_POLICY_MASK);
}

static int ccu_clk_set_peri_voltage(struct ccu_clk *ccu_clk, int peri_volt_id,
				    u8 voltage)
{

	u32 shift, reg_val;

	if (peri_volt_id == VLT_NORMAL) {
		shift = CCU_PERI_VLT_NORM_SHIFT;
		ccu_clk->volt_peri[0] = voltage & CCU_PERI_VLT_MASK;
	} else if (peri_volt_id == VLT_HIGH) {
		shift = CCU_PERI_VLT_HIGH_SHIFT;
		ccu_clk->volt_peri[1] = voltage & CCU_PERI_VLT_MASK;
	} else
		return -EINVAL;

	reg_val = readl(CCU_VLT_PERI_REG(ccu_clk));
	reg_val = (reg_val & ~(CCU_PERI_VLT_MASK << shift)) |
	    ((voltage & CCU_PERI_VLT_MASK) << shift);

	writel(reg_val, CCU_VLT_PERI_REG(ccu_clk));

	return 0;
}

static int ccu_clk_set_voltage(struct ccu_clk *ccu_clk, int volt_id, u8 voltage)
{
	u32 shift, reg_val;
	u32 reg_addr;

	if (volt_id >= ccu_clk->freq_count)
		return -EINVAL;

	ccu_clk->freq_volt[volt_id] = voltage & CCU_VLT_MASK;
	switch (volt_id) {
	case CCU_VLT0:
		shift = CCU_VLT0_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT1:
		shift = CCU_VLT1_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT2:
		shift = CCU_VLT2_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT3:
		shift = CCU_VLT3_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT4:
		shift = CCU_VLT4_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT5:
		shift = CCU_VLT5_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT6:
		shift = CCU_VLT6_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT7:
		shift = CCU_VLT7_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	default:
		return -EINVAL;
	}
	reg_val = readl(reg_addr);
	reg_val = (reg_val & ~(CCU_VLT_MASK << shift)) |
	    ((voltage & CCU_VLT_MASK) << shift);

	writel(reg_val, reg_addr);

	return 0;
}

static int ccu_clk_get_voltage(struct ccu_clk *ccu_clk, int freq_id)
{
	u32 shift, reg_val;
	u32 reg_addr;
	int volt_id;

	/*Ideally we should compare against ccu_clk->freq_count,
	   but anyways allowing read for all 8 freq Ids. */
	if (freq_id >= 8)
		return -EINVAL;

	switch (freq_id) {
	case CCU_VLT0:
		shift = CCU_VLT0_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT1:
		shift = CCU_VLT1_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT2:
		shift = CCU_VLT2_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT3:
		shift = CCU_VLT3_SHIFT;
		reg_addr = CCU_VLT0_3_REG(ccu_clk);
		break;
	case CCU_VLT4:
		shift = CCU_VLT4_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT5:
		shift = CCU_VLT5_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT6:
		shift = CCU_VLT6_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	case CCU_VLT7:
		shift = CCU_VLT7_SHIFT;
		reg_addr = CCU_VLT4_7_REG(ccu_clk);
		break;
	default:
		return -EINVAL;
	}
	reg_val = readl(reg_addr);
	volt_id = (reg_val & (CCU_VLT_MASK << shift)) >> shift;

	return volt_id;
}

static int ccu_policy_dbg_get_act_freqid(struct ccu_clk *ccu_clk)
{
	u32 reg_val;

	reg_val = readl(ccu_clk->ccu_clk_mgr_base + ccu_clk->policy_dbg_offset);
	reg_val =
	    (reg_val >> ccu_clk->
	     policy_dbg_act_freq_shift) & CCU_POLICY_DBG_FREQ_MASK;

	return (int)reg_val;
}

static int ccu_policy_dbg_get_act_policy(struct ccu_clk *ccu_clk)
{
	u32 reg_val;

	reg_val = readl(ccu_clk->ccu_clk_mgr_base + ccu_clk->policy_dbg_offset);
	reg_val =
	    (reg_val >> ccu_clk->
	     policy_dbg_act_policy_shift) & CCU_POLICY_DBG_POLICY_MASK;

	return (int)reg_val;
}

static int ccu_clk_set_active_policy(struct ccu_clk *ccu_clk, u32 policy)
{
	ccu_clk->active_policy = policy;
	return 0;
}

static int ccu_clk_get_active_policy(struct ccu_clk *ccu_clk)
{
#ifdef CONFIG_DEBUG_FS
	return ccu_policy_dbg_get_act_policy(ccu_clk);
#else
	return ccu_clk->active_policy;
#endif

}

/*Default function to save/restore CCU state
Caller should make sure that PI is in enabled state */
static int ccu_clk_save_state(struct ccu_clk *ccu_clk, int save)
{
	int ret = 0;
	int i, j;
	struct reg_save *reg_save;
	u32 buf_inx = 0;
	u32 reg_val;
	struct clk *clk = &ccu_clk->clk;
	struct ccu_state_save *ccu_state_save = ccu_clk->ccu_state_save;

	clk_dbg("%s: CCU: %s save = %d\n", __func__, clk->name, save);
	BUG_ON(!ccu_state_save);
	reg_save = ccu_state_save->reg_save;

	if (save) {
		for (i = 0; i < ccu_state_save->reg_set_count; i++, reg_save++) {

			for (j = reg_save->offset_start;
			     j <= reg_save->offset_end; j += 4) {
				reg_val = readl(CCU_REG_ADDR(ccu_clk, j));
				clk_dbg("%s:save - off = %x,val = %x\n",
					__func__, j, reg_val);
				ccu_state_save->save_buf[buf_inx++] = reg_val;
			}
		}
		BUG_ON(buf_inx != ccu_state_save->num_reg);
		/*Set save_buf[num_reg] to 1 to indicate that buf entries are valid */
		ccu_state_save->save_buf[buf_inx] = 1;
	} else {		/*Restore */

		/*Error if the contxt buffer is not having valid data */
		BUG_ON(ccu_state_save->save_buf[ccu_state_save->num_reg] == 0);

		/* enable write access */
		ccu_write_access_enable(ccu_clk, true);
		/*stop policy engine */
		ccu_policy_engine_stop(ccu_clk);

		/*Re-init CCU */
		if (clk->ops && clk->ops->init)
			ret = clk->ops->init(clk);

		for (i = 0; i < ccu_state_save->reg_set_count; i++, reg_save++) {
			for (j = reg_save->offset_start;
			     j <= reg_save->offset_end; j += 4) {
				reg_val = ccu_state_save->save_buf[buf_inx++];
				clk_dbg("%s:restore - off = %x,val = %x\n",
					__func__, j, reg_val);
				writel(reg_val, CCU_REG_ADDR(ccu_clk, j));
				clk_dbg("%s:restore - off = %x,nweval = %x\n",
					__func__, j,
					readl(CCU_REG_ADDR(ccu_clk, j)));
			}
		}
		BUG_ON(buf_inx != ccu_state_save->num_reg);
		/*Set save_buf[num_reg] to 0 to indicate that buf entries are restored */
		ccu_state_save->save_buf[buf_inx] = 0;
		/*Resume polic engine */
		ccu_policy_engine_resume(ccu_clk,
					 ccu_clk->clk.
					 flags & CCU_TARGET_LOAD ?
					 CCU_LOAD_TARGET : CCU_LOAD_ACTIVE);
		/* disable write access */
		ccu_write_access_enable(ccu_clk, false);
	}
	clk_dbg("%s: done\n", __func__);
	return ret;
}


static int ccu_clk_get_dbg_bus_status(struct ccu_clk *ccu_clk)
{
	u32 reg;
	BUG_ON(!ccu_clk ||
			!CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN));
	reg = readl(CCU_DBG_BUS_REG(ccu_clk));
	return (reg & CCU_DBG_BUS_STATUS_MASK) >>
				CCU_DBG_BUS_STATUS_SHIFT;
}
static int ccu_clk_set_dbg_bus_sel(struct ccu_clk *ccu_clk, u32 sel)
{
	u32 reg;
	BUG_ON(!ccu_clk ||
			!CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN));
	ccu_write_access_enable(ccu_clk, true);
	reg = readl(CCU_DBG_BUS_REG(ccu_clk));
	reg &= ~CCU_DBG_BUS_SEL_MASK;
	reg |= (sel << CCU_DBG_BUS_SEL_SHIFT) &
				CCU_DBG_BUS_SEL_MASK;
	writel(reg, CCU_DBG_BUS_REG(ccu_clk));
	ccu_write_access_enable(ccu_clk, false);
	return 0;
}

static int ccu_clk_get_dbg_bus_sel(struct ccu_clk *ccu_clk)
{
	u32 reg;
	BUG_ON(!ccu_clk ||
			!CLK_FLG_ENABLED(&ccu_clk->clk, CCU_DBG_BUS_EN));
	reg = readl(CCU_DBG_BUS_REG(ccu_clk));
	reg &= CCU_DBG_BUS_SEL_MASK;
	return (int)((reg & CCU_DBG_BUS_SEL_MASK) >>
					CCU_DBG_BUS_SEL_SHIFT);
}

struct ccu_clk_ops gen_ccu_ops = {
	.write_access = ccu_clk_write_access_enable,
	.rst_write_access = ccu_rst_write_access_enable,
	.policy_engine_resume = ccu_clk_policy_engine_resume,
	.policy_engine_stop = ccu_clk_policy_engine_stop,
	.set_policy_ctrl = ccu_clk_set_policy_ctrl,
	.int_enable = ccu_clk_int_enable,
	.int_status_clear = ccu_clk_int_status_clear,
	.set_freq_policy = ccu_clk_set_freq_policy,
	.get_freq_policy = ccu_clk_get_freq_policy,
	.set_peri_voltage = ccu_clk_set_peri_voltage,
	.set_voltage = ccu_clk_set_voltage,
	.get_voltage = ccu_clk_get_voltage,
	.set_active_policy = ccu_clk_set_active_policy,
	.get_active_policy = ccu_clk_get_active_policy,
	.save_state = ccu_clk_save_state,
	.get_dbg_bus_status = ccu_clk_get_dbg_bus_status,
	.set_dbg_bus_sel = ccu_clk_set_dbg_bus_sel,
	.get_dbg_bus_sel = ccu_clk_get_dbg_bus_sel,
};

/*Generic ccu ops functions*/

static int ccu_clk_enable(struct clk *clk, int enable)
{
	int ret = 0;
	clk_dbg("%s enable: %d, ccu name:%s\n", __func__, enable, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	return ret;
}

static int ccu_clk_init(struct clk *clk)
{
	struct ccu_clk *ccu_clk;
	int inx;
	u32 reg_val;

	clk_dbg("%s - %s\n", __func__, clk->name);
	BUG_ON(clk->clk_type != CLK_TYPE_CCU);

	ccu_clk = to_ccu_clk(clk);

	/* enable write access */
	ccu_write_access_enable(ccu_clk, true);
	/*stop policy engine */
	ccu_policy_engine_stop(ccu_clk);

	/*Enabel ALL policy mask by default --  TBD- SHOULD WE DO THIS ???? */
	reg_val = CCU_POLICY_MASK_ENABLE_ALL_MASK;

	for (inx = CCU_POLICY0; inx <= CCU_POLICY3; inx++) {
		if (ccu_clk->policy_mask1_offset)
			writel(reg_val,
			       (CCU_POLICY_MASK1_REG(ccu_clk) + 4 * inx));

		if (ccu_clk->policy_mask2_offset)
			writel(reg_val,
			       (CCU_POLICY_MASK2_REG(ccu_clk) + 4 * inx));
	}

	BUG_ON(ccu_clk->freq_count > MAX_CCU_FREQ_COUNT);
	/*Init voltage table */
	for (inx = 0; inx < ccu_clk->freq_count; inx++) {
		ccu_set_voltage(ccu_clk, inx, ccu_clk->freq_volt[inx]);
	}
	/*PROC ccu doea not have the PERI voltage registers */
	if (ccu_clk->vlt_peri_offset != 0) {
		/*Init peri voltage table  */
		for (inx = 0; inx < MAX_CCU_PERI_VLT_COUNT; inx++) {
			ccu_set_peri_voltage(ccu_clk, inx,
					     ccu_clk->volt_peri[inx]);
		}
	}
	if (ccu_clk->policy_freq_offset != 0) {
		/*Init freq policy */
		for (inx = 0; inx < MAX_CCU_POLICY_COUNT; inx++) {
			BUG_ON(ccu_clk->freq_policy[inx] >=
			       ccu_clk->freq_count);
			ccu_set_freq_policy(ccu_clk, inx,
					    ccu_clk->freq_policy[inx]);
		}
	}
	/*Set ATL & AC */
	if (clk->flags & CCU_TARGET_LOAD) {
		if (clk->flags & CCU_TARGET_AC)
			ccu_set_policy_ctrl(ccu_clk, POLICY_CTRL_GO_AC,
					    CCU_AUTOCOPY_ON);
		ccu_policy_engine_resume(ccu_clk, CCU_LOAD_TARGET);
	} else
		ccu_policy_engine_resume(ccu_clk, CCU_LOAD_ACTIVE);
	/* disable write access */
	ccu_write_access_enable(ccu_clk, false);

	return 0;
}

struct gen_clk_ops gen_ccu_clk_ops = {
	.init = ccu_clk_init,
	.enable = ccu_clk_enable,
};

int peri_clk_set_policy_mask(struct peri_clk *peri_clk, int policy_id, int mask)
{
	u32 reg_val;
	u32 policy_offset = 0;

	clk_dbg("%s\n", __func__);
	if (!peri_clk->ccu_clk) {
		BUG_ON(1);
		return -EINVAL;
	}
	if (peri_clk->mask_set == 1) {
		policy_offset = peri_clk->ccu_clk->policy_mask1_offset;
	} else if (peri_clk->mask_set == 2) {
		policy_offset = peri_clk->ccu_clk->policy_mask2_offset;
	} else
		return -EINVAL;

	if (!policy_offset)
		return -EINVAL;

	policy_offset = policy_offset + (4 * policy_id);

	clk_dbg("%s offset: %08x, mask: %08x, bit_mask: %d\n", __func__,
		policy_offset, mask, peri_clk->policy_bit_mask);
	reg_val = readl(CCU_REG_ADDR(peri_clk->ccu_clk, policy_offset));
	if (mask)
		reg_val =
		    SET_BIT_USING_MASK(reg_val, peri_clk->policy_bit_mask);
	else
		reg_val =
		    RESET_BIT_USING_MASK(reg_val, peri_clk->policy_bit_mask);

	clk_dbg("%s writing %08x to %08x\n", __func__, reg_val,
		peri_clk->ccu_clk->ccu_clk_mgr_base + policy_offset);
	writel(reg_val, CCU_REG_ADDR(peri_clk->ccu_clk, policy_offset));

	return 0;
}
EXPORT_SYMBOL(peri_clk_set_policy_mask);

int peri_clk_get_policy_mask(struct peri_clk *peri_clk, int policy_id)
{
	u32 policy_offset = 0;
	u32 reg_val;

	if (!peri_clk->ccu_clk) {
		BUG_ON(1);
		return -EINVAL;
	}
	BUG_ON(policy_id < CCU_POLICY0 || policy_id > CCU_POLICY3);
	if (peri_clk->mask_set == 1) {
		if (!peri_clk->ccu_clk->policy_mask1_offset)
			return -EINVAL;
		policy_offset = peri_clk->ccu_clk->policy_mask1_offset;
	} else if (peri_clk->mask_set == 2) {
		if (!peri_clk->ccu_clk->policy_mask2_offset)
			return -EINVAL;
		policy_offset = peri_clk->ccu_clk->policy_mask2_offset;
	} else
		return -EINVAL;

	policy_offset = policy_offset + 4 * policy_id;

	reg_val = readl(CCU_REG_ADDR(peri_clk->ccu_clk, policy_offset));
	return GET_BIT_USING_MASK(reg_val, peri_clk->policy_bit_mask);

}
EXPORT_SYMBOL(peri_clk_get_policy_mask);

static int peri_clk_get_gating_ctrl(struct peri_clk *peri_clk)
{
	u32 reg_val;

	if (!peri_clk->clk_gate_offset || !peri_clk->gating_sel_mask)
		return -EINVAL;

	BUG_ON(!peri_clk->ccu_clk);
	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, peri_clk->gating_sel_mask);
}

static int peri_clk_set_gating_ctrl(struct peri_clk *peri_clk, int gating_ctrl)
{
	u32 reg_val;

	if (gating_ctrl != CLK_GATING_AUTO && gating_ctrl != CLK_GATING_SW)
		return -EINVAL;
	if (!peri_clk->clk_gate_offset || !peri_clk->gating_sel_mask)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	if (gating_ctrl == CLK_GATING_SW)
		reg_val =
		    SET_BIT_USING_MASK(reg_val, peri_clk->gating_sel_mask);
	else
		reg_val =
		    RESET_BIT_USING_MASK(reg_val, peri_clk->gating_sel_mask);

	writel(reg_val,
	       CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));

	return 0;
}

int peri_clk_set_hw_gating_ctrl(struct clk *clk, int gating_ctrl)
{
	int ret = 0;
	struct peri_clk *peri_clk;
	if (clk->clk_type != CLK_TYPE_PERI) {
		BUG_ON(1);
		return -EPERM;
	}
	peri_clk = to_peri_clk(clk);
	ret = peri_clk_set_gating_ctrl(peri_clk, gating_ctrl);

	return (ret);
}
EXPORT_SYMBOL(peri_clk_set_hw_gating_ctrl);

int peri_clk_get_pll_select(struct peri_clk *peri_clk)
{
	u32 reg_val;

	if (!peri_clk->clk_div.pll_select_offset
	    || !peri_clk->clk_div.pll_select_mask)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR
		  (peri_clk->ccu_clk, peri_clk->clk_div.pll_select_offset));

	return GET_VAL_USING_MASK_SHIFT(reg_val,
					peri_clk->clk_div.pll_select_mask,
					peri_clk->clk_div.pll_select_shift);
}
EXPORT_SYMBOL(peri_clk_get_pll_select);

int peri_clk_set_pll_select(struct peri_clk *peri_clk, int source)
{
	u32 reg_val;
	if (!peri_clk->clk_div.pll_select_offset ||
	    !peri_clk->clk_div.pll_select_mask
	    || source >= peri_clk->src_clk.count)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR
		  (peri_clk->ccu_clk, peri_clk->clk_div.pll_select_offset));
	reg_val =
	    SET_VAL_USING_MASK_SHIFT(reg_val, peri_clk->clk_div.pll_select_mask,
				     peri_clk->clk_div.pll_select_shift,
				     source);
	writel(reg_val,
	       CCU_REG_ADDR(peri_clk->ccu_clk,
			    peri_clk->clk_div.pll_select_offset));

	return 0;
}
EXPORT_SYMBOL(peri_clk_set_pll_select);

int peri_clk_hyst_enable(struct peri_clk *peri_clk, int enable, int delay)
{
	u32 reg_val;

	if (!peri_clk->clk_gate_offset || !peri_clk->hyst_val_mask
	    || !peri_clk->hyst_en_mask)
		return -EINVAL;

	if (enable) {
		if (delay != CLK_HYST_LOW && delay != CLK_HYST_HIGH)
			return -EINVAL;
	}

	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));

	if (enable) {
		reg_val = SET_BIT_USING_MASK(reg_val, peri_clk->hyst_en_mask);
		if (delay == CLK_HYST_HIGH)
			reg_val =
			    SET_BIT_USING_MASK(reg_val,
					       peri_clk->hyst_val_mask);
		else
			reg_val =
			    RESET_BIT_USING_MASK(reg_val,
						 peri_clk->hyst_val_mask);
	} else
		reg_val = RESET_BIT_USING_MASK(reg_val, peri_clk->hyst_en_mask);

	writel(reg_val,
	       CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	return 0;
}

EXPORT_SYMBOL(peri_clk_hyst_enable);

static int peri_clk_get_gating_status(struct peri_clk *peri_clk)
{
	u32 reg_val;

	BUG_ON(!peri_clk->ccu_clk);
	if (!peri_clk->clk_gate_offset || !peri_clk->stprsts_mask)
		return -EINVAL;
	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, peri_clk->stprsts_mask);
}

static int peri_clk_get_enable_bit(struct peri_clk *peri_clk)
{
	u32 reg_val;

	BUG_ON(!peri_clk->ccu_clk);
	if (!peri_clk->clk_gate_offset || !peri_clk->clk_en_mask)
		return -EINVAL;
	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, peri_clk->clk_en_mask);
}

static int peri_clk_set_voltage_lvl(struct peri_clk *peri_clk, int voltage_lvl)
{
	u32 reg_val;

	if (!peri_clk->clk_gate_offset || !peri_clk->volt_lvl_mask)
		return -EINVAL;
	if (voltage_lvl != VLT_NORMAL && voltage_lvl != VLT_HIGH)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	if (voltage_lvl == VLT_HIGH)
		reg_val = SET_BIT_USING_MASK(reg_val, peri_clk->volt_lvl_mask);
	else
		reg_val =
		    RESET_BIT_USING_MASK(reg_val, peri_clk->volt_lvl_mask);

	writel(reg_val,
	       CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));

	return 0;
}

static int peri_clk_enable(struct clk *clk, int enable)
{
	u32 reg_val;
	int ret = 0;
	struct peri_clk *peri_clk;
	int insurance;
	clk_dbg("%s:%d, clock name: %s \n", __func__, enable, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);

	BUG_ON(!peri_clk->ccu_clk || (peri_clk->clk_gate_offset == 0));

	if (clk->flags & AUTO_GATE || !peri_clk->clk_en_mask) {
		clk_dbg("%s:%s: is auto gated or no enable bit\n", __func__,
			clk->name);
		return 0;
	}

	/*enable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, true);

	reg_val =
	    readl(CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));
	clk_dbg("%s, Before change clk_gate reg value: %08x  \n", __func__,
		reg_val);
	if (enable)
		reg_val = reg_val | peri_clk->clk_en_mask;
	else
		reg_val = reg_val & ~peri_clk->clk_en_mask;
	clk_dbg("%s, writing %08x to clk_gate reg\n", __func__, reg_val);
	writel(reg_val,
	       CCU_REG_ADDR(peri_clk->ccu_clk, peri_clk->clk_gate_offset));

	clk_dbg("%s:%s clk before stprsts start\n", __func__, clk->name);
	insurance = 0;
	if (enable) {
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (peri_clk->ccu_clk,
				   peri_clk->clk_gate_offset));
			insurance++;
		} while (!(GET_BIT_USING_MASK(reg_val, peri_clk->stprsts_mask))
			 && insurance < 1000);
	} else {
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (peri_clk->ccu_clk,
				   peri_clk->clk_gate_offset));
			insurance++;
		} while ((GET_BIT_USING_MASK(reg_val, peri_clk->stprsts_mask))
			 && insurance < 1000);
	}
	WARN_ON(insurance >= 1000);
	/* disable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, false);
	if (insurance >= 1000) {
		clk_dbg("%s:%s Insurance failed\n",
				__func__, clk->name);
		return -EINVAL;
	}

	clk_dbg("%s:%s clk after stprsts start\n", __func__, clk->name);
	clk_dbg("%s, %s is %s..!\n", __func__, clk->name,
		enable ? "enabled" : "disabled");

	clk_dbg
	    ("*************%s: peri clock %s count after %s : %d ***************\n",
	     __func__, clk->name, enable ? "enable" : "disable", clk->use_cnt);

	return 0;
}

static u32 compute_rate(u32 rate, u32 div, u32 dither, u32 max_dither,
			u32 pre_div)
{
	u32 res = rate;
	res /= (pre_div + 1);
	/* rate/(X + 1 + Y / 2^n)
	   = (rate*2^n) / (2^n(X+1) + Y)
	   ==> 2^n = max_dither+1 */
	clk_dbg
	    ("%s:src_rate = %d,div = %d, dither = %d,Max_dither = %d,pre_div = %d\n",
	     __func__, rate, div, dither, max_dither, pre_div);
	res =
	    ((res / 100) * (max_dither + 1)) / (((max_dither + 1) * (div + 1)) +
						dither);
	clk_dbg("%s:result = %d\n", __func__, res);
	return res * 100;

}

static u32 peri_clk_calculate_div(struct peri_clk *peri_clk, u32 rate, u32 *div,
				  int *pre_div, int *src_clk_inx)
{
	u32 s, src = 0;
	u32 d, div_int = 0;
	u32 d_d, div_frac = 0;
	u32 pd, pre_d = 0;
	u32 diff = 0xFFFFFFFF;
	u32 temp_rate, temp_diff;
	u32 new_rate = 0;
	u32 src_clk_rate;

	u32 max_div = 0;
	u32 max_diether = 0;
	u32 max_pre_div = 0;
	struct clk_div *clk_div = &peri_clk->clk_div;
	struct src_clk *src_clk = &peri_clk->src_clk;

	if (clk_div->div_offset && clk_div->div_mask)
		max_div = clk_div->div_mask >> clk_div->div_shift;
	max_div = max_div >> clk_div->diether_bits;
	max_diether = ~(0xFFFFFFFF << clk_div->diether_bits);

	if (clk_div->pre_div_offset && clk_div->pre_div_mask)
		max_pre_div = clk_div->pre_div_mask >> clk_div->pre_div_shift;

	for (s = 0; s < src_clk->count; s++) {
		d = 0;
		d_d = 0;
		pd = 0;

		BUG_ON(src_clk->clk[s]->ops == NULL ||
		       src_clk->clk[s]->ops->get_rate == NULL);
		src_clk_rate = src_clk->clk[s]->ops->get_rate(src_clk->clk[s]);

		if (rate > src_clk_rate)
			continue;

		else if (src_clk_rate == rate) {
			src = s;
			div_int = d;
			div_frac = d_d;
			pre_d = pd;
			new_rate = rate;
			diff = 0;
			break;
		}

		for (; d <= max_div; d++) {
			d_d = 0;
			pd = 0;

			temp_rate =
			    compute_rate(src_clk_rate, d, d_d, max_diether, pd);
			if (temp_rate == rate) {
				src = s;
				div_int = d;
				div_frac = d_d;
				pre_d = pd;
				new_rate = rate;
				diff = 0;
				goto exit;
			}

			temp_diff = abs(temp_rate - rate);
			if (temp_diff < diff) {
				diff = temp_diff;
				src = s;
				div_int = d;
				div_frac = d_d;
				pre_d = pd;
				new_rate = temp_rate;
			} else if (temp_rate < rate && temp_diff > diff)
				break;

			for (; pd <= max_pre_div; pd++) {
				d_d = 0;

				temp_rate =
				    compute_rate(src_clk_rate, d, d_d,
						 max_diether, pd);
				if (temp_rate == rate) {
					src = s;
					div_int = d;
					div_frac = d_d;
					pre_d = pd;
					new_rate = rate;
					diff = 0;
					goto exit;
				}

				temp_diff = abs(temp_rate - rate);
				if (temp_diff < diff) {
					diff = temp_diff;
					src = s;
					div_int = d;
					div_frac = d_d;
					pre_d = pd;
					new_rate = temp_rate;
				} else if (temp_rate < rate && temp_diff > diff)
					break;

				for (d_d = 1; d_d <= max_diether; d_d++) {
					temp_rate =
					    compute_rate(src_clk_rate, d, d_d,
							 max_diether, pd);
					if (temp_rate == rate) {
						src = s;
						div_int = d;
						div_frac = d_d;
						pre_d = pd;
						new_rate = rate;
						diff = 0;
						goto exit;
					}

					temp_diff = abs(temp_rate - rate);
					if (temp_diff < diff) {
						diff = temp_diff;
						src = s;
						div_int = d;
						div_frac = d_d;
						pre_d = pd;
						new_rate = temp_rate;
					} else if (temp_rate < rate
						   && temp_diff > diff)
						break;

				}

			}

		}
	}

exit:

	if (div) {
		*div = div_int;
		clk_dbg("div: %08x\n", *div);
		if (max_diether)
			*div = ((*div) << clk_div->diether_bits) | div_frac;
		clk_dbg("div: %08x, div_frac:%08x\n", *div, div_frac);
	}
	if (pre_div)
		*pre_div = pre_d;

	if (src_clk_inx)
		*src_clk_inx = src;

	return new_rate;

}

static int peri_clk_set_rate(struct clk *clk, u32 rate)
{
	struct peri_clk *peri_clk;
	u32 new_rate, reg_val;
	u32 div, pre_div, src;
	struct clk_div *clk_div;
	int insurance;
	int ret = 0;

	if (clk->clk_type != CLK_TYPE_PERI) {
		clk_dbg("%s : %s - Clk type is not peri\n",
				__func__, clk->name);
		return -EPERM;
	}

	peri_clk = to_peri_clk(clk);

	clk_dbg("%s : %s\n", __func__, clk->name);

	if (CLK_FLG_ENABLED(clk, RATE_FIXED)) {
		clk_dbg("%s : %s - Error...fixed rate clk\n",
			__func__, clk->name);
		return -EINVAL;

	}
	/*Clock should be in enabled state to set the rate,.
	   trigger won't work otherwise
	 */
	__peri_clk_enable(clk);
	clk_div = &peri_clk->clk_div;
	new_rate = peri_clk_calculate_div(peri_clk, rate, &div, &pre_div, &src);

	if (abs(rate - new_rate) > CLK_RATE_MAX_DIFF) {
		pr_info("%s : %s - rate(%d) not supported; nearest: %d\n",
			__func__, clk->name, rate, new_rate);
		/* Disable clock to compensate enable call before set rate */
		__peri_clk_disable(clk);
		return -EINVAL;
	}
	clk_dbg
	    ("%s clock name %s, src_rate %u sel %d div %u pre_div %u new_rate %u\n",
	     __func__, clk->name, peri_clk->src_clk.clk[src]->rate, src, div,
	     pre_div, new_rate);

	/* enable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, true);

	/*Write DIV */
	reg_val = readl(CCU_REG_ADDR(peri_clk->ccu_clk, clk_div->div_offset));
	reg_val =
	    SET_VAL_USING_MASK_SHIFT(reg_val, clk_div->div_mask,
				     clk_div->div_shift, div);
	clk_dbg
	    ("reg_val: %08x, div_offset:%08x, div_mask:%08x, div_shift:%08x, div:%08x\n",
	     reg_val, clk_div->div_offset, clk_div->div_mask,
	     clk_div->div_shift, div);
	writel(reg_val, CCU_REG_ADDR(peri_clk->ccu_clk, clk_div->div_offset));

	if (clk_div->pre_div_offset && clk_div->pre_div_mask) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (peri_clk->ccu_clk, clk_div->pre_div_offset));
		reg_val =
		    SET_VAL_USING_MASK_SHIFT(reg_val, clk_div->pre_div_mask,
					     clk_div->pre_div_shift, pre_div);
		writel(reg_val,
		       CCU_REG_ADDR(peri_clk->ccu_clk,
				    clk_div->pre_div_offset));
	}
	/*set the source clock selected */
	peri_clk_set_pll_select(peri_clk, src);

	clk_dbg("Before trigger clock \n");
	if (clk_div->div_trig_offset && clk_div->div_trig_mask) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (peri_clk->ccu_clk, clk_div->div_trig_offset));
		clk_dbg
		    ("DIV: tigger offset: %08x, reg_value: %08x trig_mask:%08x\n",
		     clk_div->div_trig_offset, reg_val, clk_div->div_trig_mask);
		reg_val = SET_BIT_USING_MASK(reg_val, clk_div->div_trig_mask);
		writel(reg_val,
		       CCU_REG_ADDR(peri_clk->ccu_clk,
				    clk_div->div_trig_offset));
		insurance = 0;
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (peri_clk->ccu_clk,
				   clk_div->div_trig_offset));
			clk_dbg("reg_val: %08x, trigger bit: %08x\n", reg_val,
				GET_BIT_USING_MASK(reg_val,
						   clk_div->div_trig_mask));
			insurance++;
		}
		while ((GET_BIT_USING_MASK(reg_val, clk_div->div_trig_mask))
		       && insurance < 1000);
		WARN_ON(insurance >= 1000);
		if (insurance >= 1000) {
			ccu_write_access_enable(peri_clk->ccu_clk, false);
			clk_dbg("%s : %s - Insurance failed\n", __func__,
					clk->name);
			__peri_clk_disable(clk);
			return -EINVAL;
		}
	}
	if (clk_div->prediv_trig_offset && clk_div->prediv_trig_mask) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (peri_clk->ccu_clk, clk_div->prediv_trig_offset));
		clk_dbg
		    ("PERDIV: tigger offset: %08x, reg_value: %08x trig_mask:%08x\n",
		     clk_div->div_trig_offset, reg_val, clk_div->div_trig_mask);
		reg_val =
		    SET_BIT_USING_MASK(reg_val, clk_div->prediv_trig_mask);
		writel(reg_val,
		       CCU_REG_ADDR(peri_clk->ccu_clk,
				    clk_div->prediv_trig_offset));
		insurance = 0;
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (peri_clk->ccu_clk,
				   clk_div->prediv_trig_offset));
			clk_dbg("reg_val: %08x, trigger bit: %08x\n", reg_val,
				GET_BIT_USING_MASK(reg_val,
						   clk_div->prediv_trig_mask));
			insurance++;
		}
		while ((GET_BIT_USING_MASK(reg_val, clk_div->prediv_trig_mask))
		       && insurance < 1000);
		WARN_ON(insurance >= 1000);
		if (insurance >= 1000) {
			ccu_write_access_enable(peri_clk->ccu_clk, false);
			clk_dbg("%s : %s - Insurance failed\n", __func__,
					clk->name);
			__peri_clk_disable(clk);
			return -EINVAL;
		}
	}
	/* disable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, false);
	/* Disable clock to compensate enable call before set rate */
	__peri_clk_disable(clk);

	clk_dbg("clock set rate done \n");
	return 0;
}

static int peri_clk_init(struct clk *clk)
{
	struct peri_clk *peri_clk;
	struct src_clk *src_clks;
	int inx;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);
	peri_clk = to_peri_clk(clk);

	BUG_ON(peri_clk->ccu_clk == NULL);

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	/* enable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, true);

	/*Init dependent clocks .... */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s dep clock %s init \n", __func__,
			clk->dep_clks[inx]->name);
		__clk_init(clk->dep_clks[inx]);
	}
	/*Init source clocks */
	/*enable/disable src clk */
	BUG_ON(!PERI_SRC_CLK_VALID(peri_clk)
	       && peri_clk->clk_div.pll_select_offset);

	if (PERI_SRC_CLK_VALID(peri_clk)) {
		src_clks = &peri_clk->src_clk;
		for (inx = 0; inx < src_clks->count; inx++) {
			clk_dbg("%s src clock %s init \n", __func__,
				src_clks->clk[inx]->name);
			__clk_init(src_clks->clk[inx]);
		}
		/*set the default src clock */
		BUG_ON(peri_clk->src_clk.src_inx >= peri_clk->src_clk.count);
		peri_clk_set_pll_select(peri_clk, peri_clk->src_clk.src_inx);
	}

	peri_clk_set_voltage_lvl(peri_clk, VLT_NORMAL);
	peri_clk_hyst_enable(peri_clk, HYST_ENABLE & clk->flags,
			     (clk->
			      flags & HYST_HIGH) ? CLK_HYST_HIGH :
			     CLK_HYST_LOW);

	if (clk->flags & AUTO_GATE)
		peri_clk_set_gating_ctrl(peri_clk, CLK_GATING_AUTO);
	else
		peri_clk_set_gating_ctrl(peri_clk, CLK_GATING_SW);

	clk_dbg("%s: before setting the mask\n", __func__);
	/*This is temporary, if PM initializes the policy mask of each clock then
	 * this can be removed. */
	/*stop policy engine */
	ccu_policy_engine_stop(peri_clk->ccu_clk);
	peri_clk_set_policy_mask(peri_clk, CCU_POLICY0,
				 peri_clk->policy_mask_init[0]);
	peri_clk_set_policy_mask(peri_clk, CCU_POLICY1,
				 peri_clk->policy_mask_init[1]);
	peri_clk_set_policy_mask(peri_clk, CCU_POLICY2,
				 peri_clk->policy_mask_init[2]);
	peri_clk_set_policy_mask(peri_clk, CCU_POLICY3,
				 peri_clk->policy_mask_init[3]);
	/*start policy engine */
	ccu_policy_engine_resume(peri_clk->ccu_clk, CCU_LOAD_ACTIVE);

	BUG_ON(CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)
	       && CLK_FLG_ENABLED(clk, DISABLE_ON_INIT));

	if (CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)) {
		__peri_clk_enable(clk);
	}

	else if (CLK_FLG_ENABLED(clk, DISABLE_ON_INIT)) {
		if (clk->ops->enable) {
			clk->ops->enable(clk, 0);
		}
	}

	/* Disable write access */
	ccu_write_access_enable(peri_clk->ccu_clk, false);

	clk_dbg
	    ("*************%s: peri clock %s count after init %d **************\n",
	     __func__, clk->name, clk->use_cnt);

	return 0;
}

static unsigned long peri_clk_round_rate(struct clk *clk, unsigned long rate)
{
	u32 new_rate;
	struct peri_clk *peri_clk;
	struct clk_div *clk_div = NULL;

	if (clk->clk_type != CLK_TYPE_PERI)
		return -EPERM;

	peri_clk = to_peri_clk(clk);

	clk_div = &peri_clk->clk_div;
	if (clk_div == NULL)
		return -EPERM;

	new_rate = peri_clk_calculate_div(peri_clk, rate, NULL, NULL, NULL);
	clk_dbg("%s:rate = %d\n", __func__, new_rate);

	return new_rate;
}

static unsigned long peri_clk_get_rate(struct clk *clk)
{
	struct peri_clk *peri_clk;
	int sel = -1;
	u32 div = 0, pre_div = 0;
	u32 reg_val = 0;
	u32 max_diether;
	struct clk_div *clk_div;
	u32 parent_rate, dither = 0;
	struct clk *src_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_PERI);

	peri_clk = to_peri_clk(clk);

	if (CLK_FLG_ENABLED(clk, RATE_FIXED)) {
		clk_dbg("%s : %s - fixed rate clk...\n", __func__, clk->name);
		return clk->rate;

	}

	clk_div = &peri_clk->clk_div;
	if (clk_div->div_offset && clk_div->div_mask) {
		reg_val =
		    readl(CCU_REG_ADDR(peri_clk->ccu_clk, clk_div->div_offset));
		clk_dbg("div_offset:%08x reg_val:%08x \n", clk_div->div_offset,
			reg_val);
		div =
		    GET_VAL_USING_MASK_SHIFT(reg_val, clk_div->div_mask,
					     clk_div->div_shift);
	}
	if (clk_div->diether_bits) {
		clk_dbg("div:%u, dither mask: %08x", div,
			~(0xFFFFFFFF << clk_div->diether_bits));
		dither = div & ~(0xFFFFFFFF << clk_div->diether_bits);
		div = div >> clk_div->diether_bits;
		clk_dbg("dither: %u div : %u\n", dither, div);
	}
	if (clk_div->pre_div_offset && clk_div->pre_div_mask) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (peri_clk->ccu_clk, clk_div->pre_div_offset));
		pre_div =
		    GET_VAL_USING_MASK_SHIFT(reg_val, clk_div->pre_div_mask,
					     clk_div->pre_div_shift);
		clk_dbg("pre div : %u\n", pre_div);
	}
	if (clk_div->pll_select_offset && clk_div->pll_select_mask) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (peri_clk->ccu_clk, clk_div->pll_select_offset));
		sel =
		    GET_VAL_USING_MASK_SHIFT(reg_val, clk_div->pll_select_mask,
					     clk_div->pll_select_shift);
		clk_dbg("pll_sel : %u\n", sel);
	}

	BUG_ON(sel >= peri_clk->src_clk.count);
	/*For clocks which doesnt have PLL select value, sel will be -1 */
	if (sel >= 0)
		peri_clk->src_clk.src_inx = sel;
	src_clk = GET_PERI_SRC_CLK(peri_clk);
	BUG_ON(!src_clk || !src_clk->ops || !src_clk->ops->get_rate);
	parent_rate = src_clk->ops->get_rate(src_clk);
	max_diether = ~(0xFFFFFFFF << clk_div->diether_bits);

	clk->rate =
	    compute_rate(parent_rate, div, dither, max_diether, pre_div);

	clk_dbg
	    ("%s clock name %s, src_rate %u sel %d div %u pre_div %u dither %u rate %u\n",
	     __func__, clk->name, peri_clk->src_clk.clk[sel]->rate, sel, div,
	     pre_div, dither, clk->rate);
	return clk->rate;
}

static int peri_clk_reset(struct clk *clk)
{
	u32 reg_val;
	struct peri_clk *peri_clk;

	if (clk->clk_type != CLK_TYPE_PERI) {
		BUG_ON(1);
		return -EPERM;
	}

	peri_clk = to_peri_clk(clk);
	clk_dbg("%s -- %s to be reset\n", __func__, clk->name);

	BUG_ON(!peri_clk->ccu_clk);
	if (!peri_clk->soft_reset_offset || !peri_clk->clk_reset_mask)
		return -EPERM;

	CCU_ACCESS_EN(peri_clk->ccu_clk, 1);

	/* enable write access */
	ccu_reset_write_access_enable(peri_clk->ccu_clk, true);

	reg_val =
	    readl(peri_clk->ccu_clk->ccu_reset_mgr_base +
		  peri_clk->soft_reset_offset);
	clk_dbg("reset offset: %08x, reg_val: %08x\n",
		(peri_clk->ccu_clk->ccu_reset_mgr_base +
		 peri_clk->soft_reset_offset), reg_val);
	reg_val = reg_val & ~peri_clk->clk_reset_mask;
	clk_dbg("writing reset value: %08x\n", reg_val);
	writel(reg_val,
	       peri_clk->ccu_clk->ccu_reset_mgr_base +
	       peri_clk->soft_reset_offset);

	udelay(10);

	reg_val = reg_val | peri_clk->clk_reset_mask;
	clk_dbg("writing reset release value: %08x\n", reg_val);

	writel(reg_val,
	       peri_clk->ccu_clk->ccu_reset_mgr_base +
	       peri_clk->soft_reset_offset);

	ccu_reset_write_access_enable(peri_clk->ccu_clk, false);

	CCU_ACCESS_EN(peri_clk->ccu_clk, 0);

	return 0;
}

struct gen_clk_ops gen_peri_clk_ops = {
	.init = peri_clk_init,
	.enable = peri_clk_enable,
	.set_rate = peri_clk_set_rate,
	.get_rate = peri_clk_get_rate,
	.round_rate = peri_clk_round_rate,
	.reset = peri_clk_reset,
};

int bus_clk_get_gating_ctrl(struct bus_clk *bus_clk)
{
	u32 reg_val;

	if (!bus_clk->clk_gate_offset || !bus_clk->gating_sel_mask)
		return -EINVAL;

	BUG_ON(!bus_clk->ccu_clk);
	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, bus_clk->gating_sel_mask);
}

EXPORT_SYMBOL(bus_clk_get_gating_ctrl);

static int bus_clk_set_gating_ctrl(struct bus_clk *bus_clk, int gating_ctrl)
{
	u32 reg_val;

	if (!bus_clk->clk_gate_offset || !bus_clk->gating_sel_mask)
		return -EINVAL;

	if (gating_ctrl != CLK_GATING_AUTO && gating_ctrl != CLK_GATING_SW)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	if (CLK_GATING_SW == gating_ctrl)
		reg_val = SET_BIT_USING_MASK(reg_val, bus_clk->gating_sel_mask);
	else
		reg_val =
		    RESET_BIT_USING_MASK(reg_val, bus_clk->gating_sel_mask);
	writel(reg_val,
	       CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));

	return 0;
}

static int bus_clk_get_gating_status(struct bus_clk *bus_clk)
{
	u32 reg_val;

	BUG_ON(!bus_clk->ccu_clk);
	if (!bus_clk->clk_gate_offset || !bus_clk->stprsts_mask)
		return -EINVAL;
	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, bus_clk->stprsts_mask);
}

static int bus_clk_get_enable_bit(struct bus_clk *bus_clk)
{
	u32 reg_val;

	BUG_ON(!bus_clk->ccu_clk);
	if (!bus_clk->clk_gate_offset || !bus_clk->clk_en_mask)
		return -EINVAL;
	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, bus_clk->clk_en_mask);
}

static int bus_clk_hyst_enable(struct bus_clk *bus_clk, int enable, int delay)
{
	u32 reg_val;

	if (!bus_clk->clk_gate_offset || !bus_clk->hyst_val_mask
	    || !bus_clk->hyst_en_mask)
		return -EINVAL;

	if (enable) {
		if (delay != CLK_HYST_LOW && delay != CLK_HYST_HIGH)
			return -EINVAL;
	}

	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));

	if (enable) {
		reg_val = SET_BIT_USING_MASK(reg_val, bus_clk->hyst_en_mask);
		if (delay == CLK_HYST_HIGH)
			reg_val =
			    SET_BIT_USING_MASK(reg_val, bus_clk->hyst_val_mask);
		else
			reg_val =
			    RESET_BIT_USING_MASK(reg_val,
						 bus_clk->hyst_val_mask);
	} else
		reg_val = RESET_BIT_USING_MASK(reg_val, bus_clk->hyst_en_mask);

	writel(reg_val,
	       CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	return 0;
}

/* bus clocks */
static int bus_clk_enable(struct clk *clk, int enable)
{
	struct bus_clk *bus_clk;
	u32 reg_val;
	int insurance;
	int ret = 0;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);

	clk_dbg("%s -- %s to be %s\n", __func__, clk->name,
		enable ? "enabled" : "disabled");

	bus_clk = to_bus_clk(clk);

	if ((bus_clk->clk_gate_offset == 0) || (bus_clk->clk_en_mask == 0))
		return -EPERM;

	if (clk->flags & AUTO_GATE) {
		clk_dbg("%s:%s: is auto gated\n", __func__, clk->name);
		return 0;
	}

	/* enable write access */
	ccu_write_access_enable(bus_clk->ccu_clk, true);

	reg_val =
	    readl(CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));
	clk_dbg("gate offset: %08x, reg_val: %08x, enable:%u\n",
		bus_clk->clk_gate_offset, reg_val, enable);
	if (enable)
		reg_val = reg_val | bus_clk->clk_en_mask;
	else
		reg_val = reg_val & ~bus_clk->clk_en_mask;
	clk_dbg("%s, writing %08x to clk_gate reg %08x\n", __func__, reg_val,
		(bus_clk->ccu_clk->ccu_clk_mgr_base +
		 bus_clk->clk_gate_offset));
	writel(reg_val,
	       CCU_REG_ADDR(bus_clk->ccu_clk, bus_clk->clk_gate_offset));

	clk_dbg("%s:%s clk before stprsts start\n", __func__, clk->name);
	insurance = 0;
	if (enable) {
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (bus_clk->ccu_clk, bus_clk->clk_gate_offset));
			insurance++;
		} while (!(GET_BIT_USING_MASK(reg_val, bus_clk->stprsts_mask))
			 && insurance < 1000);

	} else {
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (bus_clk->ccu_clk, bus_clk->clk_gate_offset));
			insurance++;
		} while ((GET_BIT_USING_MASK(reg_val, bus_clk->stprsts_mask))
			 && insurance < 1000);
	}
	WARN_ON(insurance >= 1000);

	/* disable write access */
	ccu_write_access_enable(bus_clk->ccu_clk, false);
	if (insurance >= 1000) {
		clk_dbg("%s:%s Insurance failed\n", __func__, clk->name);
		return -EINVAL;
	}

	clk_dbg("%s:%s clk after stprsts start\n", __func__, clk->name);
	clk_dbg("%s -- %s is %s\n", __func__, clk->name,
		enable ? "enabled" : "disabled");

	clk_dbg
	    ("*************%s: bus clock %s count after %s : %d ***************\n",
	     __func__, clk->name, enable ? "enable" : "disable", clk->use_cnt);
	return 0;
}

static unsigned long bus_clk_get_rate(struct clk *c)
{
	struct bus_clk *bus_clk = to_bus_clk(c);
	struct ccu_clk *ccu_clk;
	int current_policy;
	int freq_id;

	BUG_ON(!bus_clk->ccu_clk);
	ccu_clk = bus_clk->ccu_clk;

	if (bus_clk->freq_tbl_index == -1) {
		if (!bus_clk->src_clk || !bus_clk->src_clk->ops
		    || !bus_clk->src_clk->ops->get_rate) {
			clk_dbg
			    ("This bus clock freq depends on internal dividers\n");
			c->rate = 0;
			goto ret;
		}
		c->rate = bus_clk->src_clk->ops->get_rate(bus_clk->src_clk);
		goto ret;
	}
	current_policy = ccu_get_active_policy(ccu_clk);

	freq_id = ccu_get_freq_policy(ccu_clk, current_policy);
	if (freq_id < 0)
		return 0;
	BUG_ON(freq_id >= MAX_CCU_FREQ_COUNT);

	clk_dbg("current_policy: %d freq_id %d freq_tbl_index :%d\n",
		current_policy, freq_id, bus_clk->freq_tbl_index);
	c->rate = ccu_clk->freq_tbl[freq_id][bus_clk->freq_tbl_index];
ret:
	clk_dbg("clock rate: %ld\n", (long int)c->rate);

	return c->rate;
}

static int bus_clk_init(struct clk *clk)
{
	struct bus_clk *bus_clk;
	int inx;

	BUG_ON(clk->clk_type != CLK_TYPE_BUS);

	bus_clk = to_bus_clk(clk);
	BUG_ON(bus_clk->ccu_clk == NULL);

	clk_dbg("%s - %s\n", __func__, clk->name);

	/* Enable write access */
	ccu_write_access_enable(bus_clk->ccu_clk, true);

	clk_dbg("%s init dep clks -- %s\n", __func__, clk->name);
	/*Init dependent clocks, if any */
	for (inx = 0; inx < MAX_DEP_CLKS && clk->dep_clks[inx]; inx++) {
		clk_dbg("%s Dependant clock %s init \n", __func__,
			clk->dep_clks[inx]->name);
		__clk_init(clk->dep_clks[inx]);
	}

	clk_dbg("%s init src clks -- %s\n", __func__, clk->name);
	/*Init src clk, if any */
	if (bus_clk->src_clk) {
		clk_dbg("%s src clock %s init \n", __func__,
			bus_clk->src_clk->name);
		__clk_init(bus_clk->src_clk);
	}

	if (bus_clk->hyst_val_mask)
		bus_clk_hyst_enable(bus_clk, HYST_ENABLE & clk->flags,
				    (clk->
				     flags & HYST_HIGH) ? CLK_HYST_HIGH :
				    CLK_HYST_LOW);

	if (clk->flags & AUTO_GATE)
		bus_clk_set_gating_ctrl(bus_clk, CLK_GATING_AUTO);
	else
		bus_clk_set_gating_ctrl(bus_clk, CLK_GATING_SW);

	BUG_ON(CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)
	       && CLK_FLG_ENABLED(clk, DISABLE_ON_INIT));

	if (CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)) {
		__bus_clk_enable(clk);
	}
	if (CLK_FLG_ENABLED(clk, DISABLE_ON_INIT)) {
		if (clk->ops->enable) {
			clk->ops->enable(clk, 0);
		}
	}

	/* Disable write access */
	ccu_write_access_enable(bus_clk->ccu_clk, false);
	clk_dbg("%s init complete\n", clk->name);
	clk_dbg
	    ("*************%s: bus clock %s count after init %d ***************\n",
	     __func__, clk->name, clk->use_cnt);

	return 0;
}

static int bus_clk_reset(struct clk *clk)
{
	struct bus_clk *bus_clk;
	u32 reg_val;

	if (clk->clk_type != CLK_TYPE_BUS) {
		BUG_ON(1);
		return -EPERM;
	}
	clk_dbg("%s -- %s to be reset\n", __func__, clk->name);

	bus_clk = to_bus_clk(clk);

	BUG_ON(!bus_clk->ccu_clk);
	if (!bus_clk->soft_reset_offset || !bus_clk->clk_reset_mask)
		return -EPERM;

	CCU_ACCESS_EN(bus_clk->ccu_clk, 1);

	/* enable write access */
	ccu_reset_write_access_enable(bus_clk->ccu_clk, true);

	reg_val =
	    readl(bus_clk->ccu_clk->ccu_reset_mgr_base +
		  bus_clk->soft_reset_offset);

	clk_dbg("reset offset: %08x, reg_val: %08x\n",
		(bus_clk->ccu_clk->ccu_clk_mgr_base +
		 bus_clk->soft_reset_offset), reg_val);
	reg_val = reg_val & ~bus_clk->clk_reset_mask;
	clk_dbg("writing reset val: %08x\n", reg_val);
	writel(reg_val,
	       bus_clk->ccu_clk->ccu_reset_mgr_base +
	       bus_clk->soft_reset_offset);

	udelay(10);

	reg_val = reg_val | bus_clk->clk_reset_mask;
	clk_dbg("writing reset release val: %08x\n", reg_val);
	writel(reg_val,
	       bus_clk->ccu_clk->ccu_reset_mgr_base +
	       bus_clk->soft_reset_offset);

	/* disable write access */
	ccu_reset_write_access_enable(bus_clk->ccu_clk, false);

	CCU_ACCESS_EN(bus_clk->ccu_clk, 0);

	return 0;
}

struct gen_clk_ops gen_bus_clk_ops = {
	.init = bus_clk_init,
	.enable = bus_clk_enable,
	.get_rate = bus_clk_get_rate,
	.reset = bus_clk_reset,
};

static int ref_clk_get_gating_status(struct ref_clk *ref_clk)
{
	u32 reg_val;

	BUG_ON(!ref_clk->ccu_clk);
	if (!ref_clk->clk_gate_offset || !ref_clk->stprsts_mask)
		return -EINVAL;
	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));
	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, ref_clk->stprsts_mask);
}

static int ref_clk_get_enable_bit(struct ref_clk *ref_clk)
{
	u32 reg_val;

	BUG_ON(!ref_clk->ccu_clk);
	if (!ref_clk->clk_gate_offset || !ref_clk->clk_en_mask)
		return -EINVAL;
	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));
	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, ref_clk->clk_en_mask);
}

static int ref_clk_get_gating_ctrl(struct ref_clk *ref_clk)
{
	u32 reg_val;

	if (!ref_clk->clk_gate_offset || !ref_clk->gating_sel_mask)
		return -EINVAL;

	BUG_ON(!ref_clk->ccu_clk);
	CCU_ACCESS_EN(ref_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));
	CCU_ACCESS_EN(ref_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, ref_clk->gating_sel_mask);
}

static int ref_clk_set_gating_ctrl(struct ref_clk *ref_clk, int gating_ctrl)
{
	u32 reg_val;

	if (gating_ctrl != CLK_GATING_AUTO && gating_ctrl != CLK_GATING_SW)
		return -EINVAL;
	if (!ref_clk->clk_gate_offset || !ref_clk->gating_sel_mask)
		return -EINVAL;

	reg_val =
	    readl(CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));
	if (gating_ctrl == CLK_GATING_SW)
		reg_val = SET_BIT_USING_MASK(reg_val, ref_clk->gating_sel_mask);
	else
		reg_val =
		    RESET_BIT_USING_MASK(reg_val, ref_clk->gating_sel_mask);

	writel(reg_val,
	       CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));

	return 0;
}

static int ref_clk_hyst_enable(struct ref_clk *ref_clk, int enable, int delay)
{
	u32 reg_val;

	if (!ref_clk->clk_gate_offset || !ref_clk->hyst_val_mask
	    || !ref_clk->hyst_en_mask)
		return -EINVAL;

	if (enable) {
		if (delay != CLK_HYST_LOW && delay != CLK_HYST_HIGH)
			return -EINVAL;
	}

	reg_val =
	    readl(CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));

	if (enable) {
		reg_val = SET_BIT_USING_MASK(reg_val, ref_clk->hyst_en_mask);
		if (delay == CLK_HYST_HIGH)
			reg_val =
			    SET_BIT_USING_MASK(reg_val, ref_clk->hyst_val_mask);
		else
			reg_val =
			    RESET_BIT_USING_MASK(reg_val,
						 ref_clk->hyst_val_mask);
	} else
		reg_val = RESET_BIT_USING_MASK(reg_val, ref_clk->hyst_en_mask);

	writel(reg_val,
	       CCU_REG_ADDR(ref_clk->ccu_clk, ref_clk->clk_gate_offset));
	return 0;
}

/* reference clocks */
unsigned long ref_clk_get_rate(struct clk *clk)
{
	if (clk->clk_type != CLK_TYPE_REF)
		return -EPERM;
	return clk->rate;
}

static int ref_clk_init(struct clk *clk)
{
	struct ref_clk *ref_clk;

	BUG_ON(clk->clk_type != CLK_TYPE_REF);
	ref_clk = to_ref_clk(clk);
	BUG_ON(ref_clk->ccu_clk == NULL);

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	/* enable write access */
	ccu_write_access_enable(ref_clk->ccu_clk, true);
	if (ref_clk_get_gating_status(ref_clk) == 1)
		clk->use_cnt = 1;

	ref_clk_hyst_enable(ref_clk, HYST_ENABLE & clk->flags,
			    (clk->
			     flags & HYST_HIGH) ? CLK_HYST_HIGH : CLK_HYST_LOW);

	if (clk->flags & AUTO_GATE)
		ref_clk_set_gating_ctrl(ref_clk, CLK_GATING_AUTO);
	else
		ref_clk_set_gating_ctrl(ref_clk, CLK_GATING_SW);

	clk_dbg("%s: before setting the mask\n", __func__);

	BUG_ON(CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)
	       && CLK_FLG_ENABLED(clk, DISABLE_ON_INIT));
	if (CLK_FLG_ENABLED(clk, ENABLE_ON_INIT)) {
		__ref_clk_enable(clk);
	}

	else if (CLK_FLG_ENABLED(clk, DISABLE_ON_INIT)) {
		if (clk->ops->enable) {
			clk->ops->enable(clk, 0);
		}
	}

	/* Disable write access */
	ccu_write_access_enable(ref_clk->ccu_clk, false);

	return 0;
}

static int ref_clk_enable(struct clk *c, int enable)
{
	return 0;
}

struct gen_clk_ops gen_ref_clk_ops = {
	.init = ref_clk_init,
	.enable = ref_clk_enable,
	.get_rate = ref_clk_get_rate,
};

static u32 compute_pll_vco_rate(u32 ndiv_int, u32 nfrac, u32 frac_div, u32 pdiv)
{
	unsigned long xtal = clock_get_xtal();
	u64 temp;
	/*
	   vco_rate = 26Mhz*(ndiv_int + ndiv_frac/frac_div)/pdiv
	   = 26(*ndiv_int*frac_div + ndiv_frac)/(pdiv*frac_div)
	 */
	//clk_dbg("%s:pdiv = %x, nfrac = %x ndiv_int = %x\n", __func__, pdiv, nfrac, ndiv_int);
	temp = ((u64)(ndiv_int * frac_div + nfrac) * xtal);

	//clk_dbg("%s: temp = %llu\n",__func__,temp);
	do_div(temp, pdiv * frac_div);

	//clk_dbg("%s: after div temp = %llu\n",__func__,temp);
	return (unsigned long)temp;
}

static unsigned long compute_pll_vco_div(struct pll_clk *pll_clk, u32 rate,
					 u32 *pdiv, u32 *ndiv_int, u32 *nfrac)
{
	u32 max_ndiv;
	u32 frac_div;
	u32 _pdiv = 1;
	u32 _ndiv_int, _nfrac;
	u32 temp_rate;
	u32 new_rate;
	unsigned long xtal = clock_get_xtal();
	u64 temp_frac;

	max_ndiv = pll_clk->ndiv_int_max;
	frac_div = 1 + (pll_clk->ndiv_frac_mask >> pll_clk->ndiv_frac_shift);

	_ndiv_int = rate / xtal;	/*pdiv = 1 */

	if (_ndiv_int > max_ndiv)
		_ndiv_int = max_ndiv;

	temp_frac = ((u64)rate - (u64)_ndiv_int * xtal) * frac_div;
	do_div(temp_frac, xtal);

	_nfrac = (u32)temp_frac;

	_nfrac &= (frac_div - 1);

	temp_rate = compute_pll_vco_rate(_ndiv_int, _nfrac, frac_div, _pdiv);

	if (temp_rate != rate) {
		for (; _nfrac < frac_div; _nfrac++) {
			temp_rate =
			    compute_pll_vco_rate(_ndiv_int, _nfrac, frac_div,
						 _pdiv);
			if (temp_rate > rate) {
				u32 temp =
				    compute_pll_vco_rate(_ndiv_int, _nfrac - 1,
							 frac_div, _pdiv);
				if (abs(temp_rate - rate) > abs(rate - temp))
					_nfrac--;
				break;
			}
		}
	}

	new_rate = compute_pll_vco_rate(_ndiv_int, _nfrac, frac_div, _pdiv);

	if (_ndiv_int == max_ndiv)
		_ndiv_int = 0;

	if (ndiv_int)
		*ndiv_int = _ndiv_int;

	if (nfrac)
		*nfrac = _nfrac;

	if (pdiv)
		*pdiv = _pdiv;
	return new_rate;
}

static unsigned long pll_clk_round_rate(struct clk *clk, unsigned long rate)
{
	u32 new_rate;
	struct pll_clk *pll_clk;

	if (clk->clk_type != CLK_TYPE_PLL)
		return -EPERM;

	pll_clk = to_pll_clk(clk);

	new_rate = compute_pll_vco_div(pll_clk, rate, NULL, NULL, NULL);

	clk_dbg("%s:rate = %d\n", __func__, new_rate);

	return new_rate;
}

static unsigned long pll_clk_get_rate(struct clk *clk)
{
	struct pll_clk *pll_clk;
	u32 reg_val = 0;
	u32 ndiv_int, nfrac, pdiv;
	u32 frac_div;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);

	pll_clk = to_pll_clk(clk);

	if (CLK_FLG_ENABLED(clk, RATE_FIXED)) {
		clk_dbg("%s : %s - fixed rate clk...\n", __func__, clk->name);
		return clk->rate;
	}
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_frac_offset));
	nfrac = (reg_val & pll_clk->ndiv_frac_mask) >> pll_clk->ndiv_frac_shift;

	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_pdiv_offset));
	pdiv = (reg_val & pll_clk->pdiv_mask) >> pll_clk->pdiv_shift;
	ndiv_int =
	    (reg_val & pll_clk->ndiv_int_mask) >> pll_clk->ndiv_int_shift;

	if (pdiv == 0)
		pdiv = pll_clk->pdiv_max;
	if (ndiv_int == 0)
		ndiv_int = pll_clk->ndiv_int_max;

	frac_div = 1 + (pll_clk->ndiv_frac_mask >> pll_clk->ndiv_frac_shift);
	return compute_pll_vco_rate(ndiv_int, nfrac, frac_div, pdiv);
}

static int pll_clk_set_rate(struct clk *clk, u32 rate)
{
	struct pll_clk *pll_clk;
	u32 new_rate, reg_val;
	u32 pll_cfg_ctrl = 0;
	int insurance;
	u32 ndiv_int, nfrac, pdiv;
	int inx, ret = 0;
	struct pll_cfg_ctrl_info *cfg_ctrl;
	if (clk->clk_type != CLK_TYPE_PLL) {
		clk_dbg("%s : %s Clock type is not PLL\n",
				__func__, clk->name);
		return -EPERM;
	}

	pll_clk = to_pll_clk(clk);

	clk_dbg("%s : %s\n", __func__, clk->name);

	if (CLK_FLG_ENABLED(clk, RATE_FIXED)) {
		clk_dbg("%s : %s - Error...fixed rate clk\n",
			__func__, clk->name);
		return -EINVAL;

	}
	new_rate = compute_pll_vco_div(pll_clk, rate, &pdiv, &ndiv_int, &nfrac);

	if (abs(new_rate - rate) > 100) {
		clk_dbg("%s : %s - rate(%d) not supported\n",
			__func__, clk->name, rate);
		return -EINVAL;
	}

	/* enable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, true);

	/*Write pll_cfg_ctrl */
	if (pll_clk->cfg_ctrl_info && pll_clk->cfg_ctrl_info->thold_count) {
		cfg_ctrl = pll_clk->cfg_ctrl_info;
		for (inx = 0; inx < cfg_ctrl->thold_count; inx++) {
			if (cfg_ctrl->vco_thold[inx] == PLL_VCO_RATE_MAX
			    || new_rate < cfg_ctrl->vco_thold[inx]) {
				pll_cfg_ctrl = cfg_ctrl->pll_config_value[inx];
				pll_cfg_ctrl <<= cfg_ctrl->pll_cfg_ctrl_shift;
				pll_cfg_ctrl &= cfg_ctrl->pll_cfg_ctrl_mask;
				break;
			}
		}
		if (inx != pll_clk->cfg_ctrl_info->thold_count) {
			writel(pll_cfg_ctrl,
			       CCU_REG_ADDR(pll_clk->ccu_clk,
					    cfg_ctrl->pll_cfg_ctrl_offset));
		}
	}
	/*Write nfrac */
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_frac_offset));
	reg_val &= ~pll_clk->ndiv_frac_mask;
	reg_val |= nfrac << pll_clk->ndiv_frac_shift;
	writel(reg_val,
	       CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_frac_offset));

	/*write nint & pdiv */
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_pdiv_offset));
	reg_val &= ~(pll_clk->pdiv_mask | pll_clk->ndiv_int_mask);
	reg_val |= (pdiv << pll_clk->pdiv_shift)
	    | (ndiv_int << pll_clk->ndiv_int_shift);
	writel(reg_val,
	       CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_pdiv_offset));

	reg_val =
	    readl(CCU_REG_ADDR
		  (pll_clk->ccu_clk, pll_clk->soft_post_resetb_offset));
	reg_val |= pll_clk->soft_post_resetb_mask;
	writel(reg_val,
	       CCU_REG_ADDR(pll_clk->ccu_clk,
			    pll_clk->soft_post_resetb_offset));

	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->soft_resetb_offset));
	reg_val |= pll_clk->soft_resetb_mask;
	writel(reg_val,
	       CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->soft_resetb_offset));

	/*Loop for lock bit only if the
	   - PLL is AUTO GATED or
	   - PLL is enabled */
	reg_val = readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pwrdwn_offset));
	if (clk->flags & AUTO_GATE || ((reg_val & pll_clk->pwrdwn_mask) == 0)) {
		insurance = 0;
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (pll_clk->ccu_clk, pll_clk->pll_lock_offset));
			insurance++;
		} while (!(GET_BIT_USING_MASK(reg_val, pll_clk->pll_lock))
			 && insurance < 1000);
		WARN_ON(insurance >= 1000);
		if (insurance >= 1000) {
			ccu_write_access_enable(pll_clk->ccu_clk, false);
			clk_dbg("%s : %s - Insurance failed\n",
					__func__, clk->name);
			return -EINVAL;
		}
	}

	/* disable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, false);
	clk_dbg("clock set rate done\n");
	return 0;
}

static int pll_clk_enable(struct clk *clk, int enable)
{
	u32 reg_val;
	struct pll_clk *pll_clk;
	int insurance, ret = 0;
	clk_dbg("%s:%d, clock name: %s \n", __func__, enable, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	BUG_ON(!pll_clk->ccu_clk || (pll_clk->pll_ctrl_offset == 0));

	if (clk->flags & AUTO_GATE || !pll_clk->pwrdwn_mask) {
		clk_dbg("%s:%s: is auto gated or no enable bit\n", __func__,
			clk->name);
		return 0;
	}

	/*enable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, true);

	if (pll_clk->idle_pwrdwn_sw_ovrride_mask != 0) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->pll_ctrl_offset));
		clk_dbg("%s, Before change pll_ctrl reg value: %08x  \n",
			__func__, reg_val);
		/*Return if sw_override bit is set */
		if (GET_BIT_USING_MASK
		    (reg_val, pll_clk->idle_pwrdwn_sw_ovrride_mask)) {
			clk_dbg("%s : %s - Software override\n", __func__,
					clk->name);
			ccu_write_access_enable(pll_clk->ccu_clk, false);
			return 0;
		}
	}
	if (enable) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->pwrdwn_offset));
		reg_val &= ~pll_clk->pwrdwn_mask;
		clk_dbg("%s, writing %08x to pwrdwn reg\n", __func__, reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pwrdwn_offset));

		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->soft_post_resetb_offset));
		reg_val |= pll_clk->soft_post_resetb_mask;
		clk_dbg("%s, writing %08x to soft_post_resetb reg\n", __func__,
			reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_clk->ccu_clk,
				    pll_clk->soft_post_resetb_offset));

		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->soft_resetb_offset));
		reg_val |=
		    pll_clk->soft_post_resetb_mask | pll_clk->soft_resetb_mask;
		clk_dbg("%s, writing %08x to soft_resetb reg\n", __func__,
			reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_clk->ccu_clk,
				    pll_clk->soft_resetb_offset));

	} else {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->pwrdwn_offset));
		reg_val = reg_val | pll_clk->pwrdwn_mask;
		clk_dbg("%s, writing %08x to pwrdwn reg\n", __func__, reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pwrdwn_offset));
	}

	if (enable) {
		insurance = 0;
		do {
			udelay(1);
			reg_val =
			    readl(CCU_REG_ADDR
				  (pll_clk->ccu_clk, pll_clk->pll_lock_offset));
			insurance++;
		} while (!(GET_BIT_USING_MASK(reg_val, pll_clk->pll_lock))
			 && insurance < 1000);
		WARN_ON(insurance >= 1000);
		if (insurance >= 1000) {
			ccu_write_access_enable(pll_clk->ccu_clk, false);
			clk_dbg("%s : %s - Insurance failed\n", __func__,
					clk->name);
			return -EINVAL;
		}

	}

	clk_dbg("%s, %s is %s..!\n", __func__, clk->name,
		enable ? "enabled" : "disabled");
	/* disable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, false);

	clk_dbg
	    ("*************%s: pll clock %s count after %s : %d ***************\n",
	     __func__, clk->name, enable ? "enable" : "disable", clk->use_cnt);

	return 0;
}

static int pll_clk_init(struct clk *clk)
{
	struct pll_clk *pll_clk;
	u32 reg_val;

	BUG_ON(clk->clk_type != CLK_TYPE_PLL);
	pll_clk = to_pll_clk(clk);

	BUG_ON(pll_clk->ccu_clk == NULL);

	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	/* enable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, true);

	if (pll_clk->idle_pwrdwn_sw_ovrride_mask != 0) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_clk->ccu_clk, pll_clk->pll_ctrl_offset));
		if (clk->flags & AUTO_GATE) {
			reg_val |= pll_clk->idle_pwrdwn_sw_ovrride_mask;
		} else {
			reg_val &= ~pll_clk->idle_pwrdwn_sw_ovrride_mask;
		}
		writel(reg_val,
		       CCU_REG_ADDR(pll_clk->ccu_clk,
				    pll_clk->pll_ctrl_offset));
	}
	if (clk->flags & INIT_PLL_OFFSET_CFG) {
		writel(pll_clk->pll_offset_cfg_val,
		CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pll_offset_offset));
	}
	/* Disable write access */
	ccu_write_access_enable(pll_clk->ccu_clk, false);

	clk_dbg
	    ("*************%s: peri clock %s count after init %d **************\n",
	     __func__, clk->name, clk->use_cnt);

	return 0;
}

static int pll_clk_get_lock_status(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->pll_lock_offset || !pll_clk->pll_lock)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pll_lock_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, pll_clk->pll_lock);
}

static int pll_clk_get_pdiv(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->ndiv_pdiv_offset || !pll_clk->pdiv_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_pdiv_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_VAL_USING_MASK_SHIFT(reg_val, pll_clk->pdiv_mask,
					pll_clk->pdiv_shift);
}

static int pll_clk_get_ndiv_int(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->ndiv_pdiv_offset || !pll_clk->ndiv_int_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_pdiv_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_VAL_USING_MASK_SHIFT(reg_val, pll_clk->ndiv_int_mask,
					pll_clk->ndiv_int_shift);
}

static int pll_clk_get_ndiv_frac(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->ndiv_frac_offset || !pll_clk->ndiv_frac_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->ndiv_frac_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_VAL_USING_MASK_SHIFT(reg_val, pll_clk->ndiv_frac_mask,
					pll_clk->ndiv_frac_shift);
}

static int pll_clk_get_idle_pwrdwn_sw_ovrride(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->pll_ctrl_offset || !pll_clk->idle_pwrdwn_sw_ovrride_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pll_ctrl_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val,
				  pll_clk->idle_pwrdwn_sw_ovrride_mask);
}

static int pll_clk_get_pwrdwn(struct pll_clk *pll_clk)
{
	u32 reg_val;

	BUG_ON(!pll_clk->ccu_clk);
	if (!pll_clk->pwrdwn_offset || !pll_clk->pwrdwn_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_clk->ccu_clk, 1);
	reg_val = readl(CCU_REG_ADDR(pll_clk->ccu_clk, pll_clk->pwrdwn_offset));
	CCU_ACCESS_EN(pll_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, pll_clk->pwrdwn_mask);
}

struct gen_clk_ops gen_pll_clk_ops = {
	.init = pll_clk_init,
	.enable = pll_clk_enable,
	.set_rate = pll_clk_set_rate,
	.get_rate = pll_clk_get_rate,
	.round_rate = pll_clk_round_rate,
};

static int pll_chnl_clk_enable(struct clk *clk, int enable)
{
	u32 reg_val;
	struct pll_chnl_clk *pll_chnl_clk;
	clk_dbg("%s:%d, clock name: %s \n", __func__, enable, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	/*enable write access */
	ccu_write_access_enable(pll_chnl_clk->ccu_clk, true);

	if (enable) {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_chnl_clk->ccu_clk,
			   pll_chnl_clk->pll_enableb_offset));
		reg_val &= ~pll_chnl_clk->out_en_mask;
		clk_dbg("%s, writing %08x to pll_enableb_offset reg\n",
			__func__, reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
				    pll_chnl_clk->pll_enableb_offset));

		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_chnl_clk->ccu_clk,
			   pll_chnl_clk->pll_load_ch_en_offset));
		reg_val |= pll_chnl_clk->load_en_mask;
		clk_dbg("%s, writing %08x to pll_load_ch_en_offset reg\n",
			__func__, reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
				    pll_chnl_clk->pll_load_ch_en_offset));

	} else {
		reg_val =
		    readl(CCU_REG_ADDR
			  (pll_chnl_clk->ccu_clk,
			   pll_chnl_clk->pll_enableb_offset));
		reg_val = reg_val | pll_chnl_clk->out_en_mask;
		clk_dbg("%s, writing %08x to pll_enableb_offset reg\n",
			__func__, reg_val);
		writel(reg_val,
		       CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
				    pll_chnl_clk->pll_enableb_offset));
	}

	/* disable write access */
	ccu_write_access_enable(pll_chnl_clk->ccu_clk, false);

	clk_dbg
	    ("*************%s: pll channrl clock %s count after %s : %d ***************\n",
	     __func__, clk->name, enable ? "enable" : "disable", clk->use_cnt);

	return 0;
}

static unsigned long pll_chnl_clk_round_rate(struct clk *clk,
					     unsigned long rate)
{
	u32 new_rate;
	u32 vco_rate;
	u32 mdiv;
	struct pll_chnl_clk *pll_chnl_clk;

	if (clk->clk_type != CLK_TYPE_PLL_CHNL)
		return 0;

	pll_chnl_clk = to_pll_chnl_clk(clk);

	vco_rate = __pll_clk_get_rate(&pll_chnl_clk->pll_clk->clk);

	if (vco_rate < rate || rate == 0)
		return 0;

	mdiv = vco_rate / rate;

	if (mdiv > pll_chnl_clk->mdiv_max)
		mdiv = pll_chnl_clk->mdiv_max;
	new_rate = vco_rate / mdiv;

	return new_rate;
}

static unsigned long pll_chnl_clk_get_rate(struct clk *clk)
{
	struct pll_chnl_clk *pll_chnl_clk;
	u32 mdiv;
	u32 reg_val;
	u32 vco_rate;
	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);

	pll_chnl_clk = to_pll_chnl_clk(clk);

	reg_val =
	    readl(CCU_REG_ADDR
		  (pll_chnl_clk->ccu_clk, pll_chnl_clk->cfg_reg_offset));
	mdiv = (reg_val & pll_chnl_clk->mdiv_mask) >> pll_chnl_clk->mdiv_shift;
	if (mdiv == 0)
		mdiv = pll_chnl_clk->mdiv_max;
	vco_rate = __pll_clk_get_rate(&pll_chnl_clk->pll_clk->clk);
	return (vco_rate / mdiv);
}

static int pll_chnl_clk_set_rate(struct clk *clk, u32 rate)
{
	u32 reg_val;
	u32 vco_rate;
	u32 mdiv;
	struct pll_chnl_clk *pll_chnl_clk;

	if (clk->clk_type != CLK_TYPE_PLL_CHNL)
		return 0;

	clk_dbg("%s : %s\n", __func__, clk->name);

	pll_chnl_clk = to_pll_chnl_clk(clk);

	vco_rate = __pll_clk_get_rate(&pll_chnl_clk->pll_clk->clk);

	if (vco_rate < rate || rate == 0) {
		clk_dbg("%s : invalid rate : %d\n", __func__, rate);
		return -EINVAL;
	}

	mdiv = vco_rate / rate;

	if (mdiv == 0)
		mdiv++;
	if (abs(rate - vco_rate / mdiv) > abs(rate - vco_rate / (mdiv + 1)))
		mdiv++;
	if (mdiv > pll_chnl_clk->mdiv_max)
		mdiv = pll_chnl_clk->mdiv_max;

	if (abs(rate - vco_rate / mdiv) > 100) {
		clk_dbg("%s : invalid rate : %d\n", __func__, rate);
		return -EINVAL;
	}

	/* enable write access */
	ccu_write_access_enable(pll_chnl_clk->ccu_clk, true);

	/*Write mdiv */
	if (mdiv == pll_chnl_clk->mdiv_max)
		mdiv = 0;

	reg_val =
	    readl(CCU_REG_ADDR
		  (pll_chnl_clk->ccu_clk, pll_chnl_clk->cfg_reg_offset));
	reg_val &= ~pll_chnl_clk->mdiv_mask;
	reg_val |= mdiv << pll_chnl_clk->mdiv_shift;
	writel(reg_val,
	       CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
			    pll_chnl_clk->cfg_reg_offset));

	reg_val =
	    readl(CCU_REG_ADDR
		  (pll_chnl_clk->ccu_clk, pll_chnl_clk->pll_load_ch_en_offset));
	reg_val |= pll_chnl_clk->load_en_mask;
	writel(reg_val,
	       CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
			    pll_chnl_clk->pll_load_ch_en_offset));

	/* disable write access */
	ccu_write_access_enable(pll_chnl_clk->ccu_clk, false);

	clk_dbg("clock set rate done \n");
	return 0;
}

static int pll_chnl_clk_init(struct clk *clk)
{
	struct pll_chnl_clk *pll_chnl_clk;
	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_PLL_CHNL);
	pll_chnl_clk = to_pll_chnl_clk(clk);

	BUG_ON(!pll_chnl_clk->ccu_clk ||
	       !pll_chnl_clk->pll_clk || !pll_chnl_clk->cfg_reg_offset);
	return 0;
}

static int pll_chnl_clk_get_mdiv(struct pll_chnl_clk *pll_chnl_clk)
{
	u32 reg_val;

	BUG_ON(!pll_chnl_clk->ccu_clk);
	if (!pll_chnl_clk->cfg_reg_offset || !pll_chnl_clk->mdiv_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
	reg_val = readl(CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
				     pll_chnl_clk->cfg_reg_offset));
	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);

	return GET_VAL_USING_MASK_SHIFT(reg_val, pll_chnl_clk->mdiv_mask,
					pll_chnl_clk->mdiv_shift);
}

static int pll_chnl_clk_get_enb_clkout(struct pll_chnl_clk *pll_chnl_clk)
{
	u32 reg_val;

	BUG_ON(!pll_chnl_clk->ccu_clk);
	if (!pll_chnl_clk->pll_enableb_offset || !pll_chnl_clk->out_en_mask)
		return -EINVAL;
	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 1);
	reg_val = readl(CCU_REG_ADDR(pll_chnl_clk->ccu_clk,
				     pll_chnl_clk->pll_enableb_offset));
	CCU_ACCESS_EN(pll_chnl_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, pll_chnl_clk->out_en_mask);
}

struct gen_clk_ops gen_pll_chnl_clk_ops = {
	.init = pll_chnl_clk_init,
	.enable = pll_chnl_clk_enable,
	.set_rate = pll_chnl_clk_set_rate,
	.get_rate = pll_chnl_clk_get_rate,
	.round_rate = pll_chnl_clk_round_rate,
};

static unsigned long core_clk_get_rate(struct clk *clk)
{
	struct core_clk *core_clk;
	u32 freq_id;
	u32 pll_chnl;
	BUG_ON(clk->clk_type != CLK_TYPE_CORE);

	core_clk = to_core_clk(clk);
	 /**/
	    freq_id =
	    ccu_get_freq_policy(core_clk->ccu_clk, core_clk->active_policy);

	if (freq_id < core_clk->num_pre_def_freq) {
		return core_clk->pre_def_freq[freq_id];
	}
	pll_chnl = freq_id - core_clk->num_pre_def_freq;

	BUG_ON(pll_chnl >= core_clk->num_chnls);

	return __pll_chnl_clk_get_rate(&core_clk->pll_chnl_clk[pll_chnl]->clk);
}

/**
 * core_clk_freq_scale - "old * mult / div" calculation for large values (32-bit-arch safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 *
 *    new = old * mult / div
 */
static inline unsigned long core_clk_freq_scale(unsigned long old, u_int div,
						u_int mult)
{
#if BITS_PER_LONG == 32

	u64 result = ((u64)old) * ((u64)mult);
	do_div(result, div);
	return (unsigned long)result;

#elif BITS_PER_LONG == 64

	unsigned long result = old * ((u64)mult);
	result /= div;
	return result;

#endif
};

static int core_clk_set_rate(struct clk *clk, u32 rate)
{
	u32 vco_rate;
	u32 div;
	struct core_clk *core_clk;
	static u32 l_p_j_ref = 0;
	static u32 l_p_j_ref_freq = 0;
	int ret;

	if (clk->clk_type != CLK_TYPE_CORE || rate == 0)
		return -EINVAL;

	clk_dbg("%s : %s\n", __func__, clk->name);

	core_clk = to_core_clk(clk);
	vco_rate = __pll_clk_get_rate(&core_clk->pll_clk->clk);
	div = vco_rate / rate;
	if (l_p_j_ref == 0 && (clk->flags & UPDATE_LPJ)) {
		BUG_ON(!clk->ops->get_rate);
#ifdef CONFIG_SMP
		l_p_j_ref = per_cpu(cpu_data, 0).loops_per_jiffy;
#else
		l_p_j_ref = loops_per_jiffy;
#endif
		l_p_j_ref_freq = clk->ops->get_rate(clk) / 1000;
		clk_dbg("l_p_j_ref = %d, l_p_j_ref_freq = %x\n", l_p_j_ref,
			l_p_j_ref_freq);
	}

	if (div < 2 || rate * div != vco_rate) {
		__pll_clk_set_rate(&core_clk->pll_clk->clk, rate * 2);
		vco_rate = __pll_clk_get_rate(&core_clk->pll_clk->clk);
		div = vco_rate / rate;
	}

	ret =
	    __pll_chnl_clk_set_rate(&core_clk->
				    pll_chnl_clk[core_clk->num_chnls - 1]->clk,
				    rate);
	if (!ret && (clk->flags & UPDATE_LPJ)) {
#ifdef CONFIG_SMP
		int i;
		for_each_online_cpu(i) {
			per_cpu(cpu_data, i).loops_per_jiffy =
			    core_clk_freq_scale(l_p_j_ref, l_p_j_ref_freq,
						rate / 1000);
		}
#endif
		loops_per_jiffy = core_clk_freq_scale(l_p_j_ref,
						      l_p_j_ref_freq,
						      rate / 1000);
		clk_dbg("loops_per_jiffy = %lu, rate = %u\n", loops_per_jiffy,
			rate);
	}
	return ret;
}

static int core_clk_init(struct clk *clk)
{
	struct core_clk *core_clk;
	clk_dbg("%s, clock name: %s \n", __func__, clk->name);

	BUG_ON(clk->clk_type != CLK_TYPE_CORE);
	core_clk = to_core_clk(clk);

	BUG_ON(!core_clk->ccu_clk ||
	       !core_clk->pll_clk || !core_clk->num_chnls);
	INIT_LIST_HEAD(&clk->list);
	list_add(&clk->list, &core_clk->ccu_clk->clk_list);

	return 0;
}

static int core_clk_reset(struct clk *clk)
{
	u32 reg_val;
	struct core_clk *core_clk;

	if (clk->clk_type != CLK_TYPE_CORE) {
		BUG_ON(1);
		return -EPERM;
	}
	core_clk = to_core_clk(clk);
	clk_dbg("%s -- %s to be reset\n", __func__, clk->name);

	BUG_ON(!core_clk->ccu_clk);
	if (!core_clk->soft_reset_offset || !core_clk->clk_reset_mask)
		return -EPERM;

	/* enable write access */
	ccu_reset_write_access_enable(core_clk->ccu_clk, true);

	reg_val =
	    readl(core_clk->ccu_clk->ccu_reset_mgr_base +
		  core_clk->soft_reset_offset);
	clk_dbg("reset offset: %08x, reg_val: %08x\n",
		(core_clk->ccu_clk->ccu_reset_mgr_base +
		 core_clk->soft_reset_offset), reg_val);
	reg_val = reg_val & ~core_clk->clk_reset_mask;
	clk_dbg("writing reset value: %08x\n", reg_val);
	writel(reg_val,
	       core_clk->ccu_clk->ccu_reset_mgr_base +
	       core_clk->soft_reset_offset);
	//core should have reset here. below code wont be executed.
	udelay(10);

	reg_val = reg_val | core_clk->clk_reset_mask;
	clk_dbg("writing reset release value: %08x\n", reg_val);

	writel(reg_val,
	       core_clk->ccu_clk->ccu_reset_mgr_base +
	       core_clk->soft_reset_offset);

	ccu_reset_write_access_enable(core_clk->ccu_clk, false);

	return 0;
}

int core_clk_get_gating_status(struct core_clk *core_clk)
{
	u32 reg_val;

	BUG_ON(!core_clk->ccu_clk);
	if (!core_clk->clk_gate_offset || !core_clk->stprsts_mask)
		return -EINVAL;
	CCU_ACCESS_EN(core_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(core_clk->ccu_clk, core_clk->clk_gate_offset));
	CCU_ACCESS_EN(core_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, core_clk->stprsts_mask);
}

static int core_clk_get_gating_ctrl(struct core_clk *core_clk)
{
	u32 reg_val;

	if (!core_clk->clk_gate_offset || !core_clk->gating_sel_mask)
		return -EINVAL;

	BUG_ON(!core_clk->ccu_clk);
	CCU_ACCESS_EN(core_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(core_clk->ccu_clk, core_clk->clk_gate_offset));
	CCU_ACCESS_EN(core_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, core_clk->gating_sel_mask);
}

static int core_clk_get_enable_bit(struct core_clk *core_clk)
{
	u32 reg_val;

	BUG_ON(!core_clk->ccu_clk);
	if (!core_clk->clk_gate_offset || !core_clk->clk_en_mask)
		return -EINVAL;
	CCU_ACCESS_EN(core_clk->ccu_clk, 1);
	reg_val =
	    readl(CCU_REG_ADDR(core_clk->ccu_clk, core_clk->clk_gate_offset));
	CCU_ACCESS_EN(core_clk->ccu_clk, 0);

	return GET_BIT_USING_MASK(reg_val, core_clk->clk_en_mask);
}

struct gen_clk_ops gen_core_clk_ops = {
	.init = core_clk_init,
	.set_rate = core_clk_set_rate,
	.get_rate = core_clk_get_rate,
	.reset = core_clk_reset,
};

#ifdef CONFIG_DEBUG_FS

__weak int debug_bus_mux_sel(int mux_sel, int mux_param)
{
	return 0;
}

__weak int set_clk_idle_debug_mon(int clk_idle, int db_sel)
{
	return 0;
}

__weak int set_clk_monitor_debug(int mon_select, int debug_bus)
{
	return 0;
}

__weak int clock_monitor_enable(struct clk *clk, int monitor)
{
	return 0;
}
__weak int set_ccu_dbg_bus_mux(struct ccu_clk *ccu_clk, int mux_sel,
			int mux_param)
{
	return 0;
}


static int clk_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t set_clk_idle_debug(struct file *file, char const __user *buf,
				  size_t count, loff_t *offset)
{
	u32 len = 0;
	int db_sel = 0;
	int clk_idle = 0;
	char input_str[100];

	if (count > sizeof(input_str))
		len = sizeof(input_str);
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
	sscanf(input_str, "%d%d", &clk_idle, &db_sel);
	set_clk_idle_debug_mon(clk_idle, db_sel);
	return count;
}

static struct file_operations clock_idle_debug_fops = {
	.open = clk_debugfs_open,
	.write = set_clk_idle_debug,
};

static ssize_t set_clk_mon_debug(struct file *file, char const __user *buf,
				 size_t count, loff_t *offset)
{
	u32 len = 0;
	int db_sel = 0;
	int mon_sel = 0;
	char input_str[100];

	if (count > 100)
		len = 100;
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;
	sscanf(input_str, "%d%d", &mon_sel, &db_sel);
	clk_dbg("%s:Clock to be monitored on %s\n", __func__,
		mon_sel ? "DEBUG_BUS_GPIO" : "CAMCS_PIN");
	set_clk_monitor_debug(mon_sel, db_sel);
	return count;
}

static struct file_operations clock_mon_debug_fops = {
	.open = clk_debugfs_open,
	.write = set_clk_mon_debug,
};

static int clk_mon_enable(void *data, u64 val)
{
	struct clk *clock = data;
	if (val == 0 || val == 1)
		clock_monitor_enable(clock, val);
	else
		clk_dbg("Invalid value \n");

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clock_mon_fops, NULL, clk_mon_enable, "%llu\n");

static int clk_debug_get_rate(void *data, u64 *val)
{
	struct clk *clock = data;
	*val = clk_get_rate(clock);
	return 0;
}

static int clk_debug_set_rate(void *data, u64 val)
{
	struct clk *clock = data;
	clk_set_rate(clock, val);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clock_rate_fops, clk_debug_get_rate,
			clk_debug_set_rate, "%llu\n");

static int ccu_debug_get_freqid(void *data, u64 *val)
{
	struct clk *clock = data;
	struct ccu_clk *ccu_clk;
	int freq_id;

	ccu_clk = to_ccu_clk(clock);
	freq_id = ccu_policy_dbg_get_act_freqid(ccu_clk);

	*val = freq_id;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ccu_freqid_fops, ccu_debug_get_freqid, NULL, "%llu\n");

static int ccu_debug_wr_en_get(void *data, u64 *val)
{
	struct clk *clock = data;
	struct ccu_clk *ccu_clk;
	BUG_ON(!clock);
	ccu_clk = to_ccu_clk(clock);
	*val = ccu_clk->write_access_en_count;
	return 0;
}

static int ccu_debug_wr_en_set(void *data, u64 val)
{
	struct clk *clock = data;
	struct ccu_clk *ccu_clk;

	BUG_ON(!clock);
	ccu_clk = to_ccu_clk(clock);

	if (val)
		ccu_write_access_enable(ccu_clk, true);
	else
		ccu_write_access_enable(ccu_clk, false);
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ccu_wr_en_fops, ccu_debug_wr_en_get, ccu_debug_wr_en_set
		, "%llu\n");




static ssize_t ccu_debug_get_dbg_bus_status(struct file *file,
					char __user *user_buf,
				size_t count, loff_t *ppos)
{
	u32 len = 0;
	struct ccu_clk *ccu_clk;
	char out_str[20];
	int status;
	struct clk *clk = file->private_data;

	BUG_ON(clk == NULL);
	ccu_clk = to_ccu_clk(clk);
	memset(out_str, 0, sizeof(out_str));
	status = ccu_get_dbg_bus_status(ccu_clk);
	if (status == -EINVAL)
		len += snprintf(out_str+len, sizeof(out_str)-len,
			"error!!\n");
	else
		len += snprintf(out_str+len, sizeof(out_str)-len,
			"%x\n", (u32)status);

	return simple_read_from_buffer(user_buf, count, ppos,
			out_str, len);
}

static ssize_t ccu_debug_set_dbg_bus_sel(struct file *file,
				  char const __user *buf, size_t count,
				  loff_t *offset)
{
	struct clk *clk = file->private_data;
	struct ccu_clk *ccu_clk;
	u32 len = 0;
	char input_str[10];
	u32 sel = 0, mux = 0, mux_parm = 0;

	BUG_ON(clk == NULL);
	ccu_clk = to_ccu_clk(clk);

	memset(input_str, 0, ARRAY_SIZE(input_str));
	if (count > ARRAY_SIZE(input_str))
		len = ARRAY_SIZE(input_str);
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;

	sscanf(&input_str[0], "%x%x%x", &sel, &mux, &mux_parm);
	set_ccu_dbg_bus_mux(ccu_clk, mux, mux_parm);
	ccu_set_dbg_bus_sel(ccu_clk, sel);
	return count;
}

static const struct file_operations ccu_dbg_bus_fops = {
	.open = clk_debugfs_open,
	.write = ccu_debug_set_dbg_bus_sel,
	.read = ccu_debug_get_dbg_bus_status,
};
static int ccu_debug_get_policy(void *data, u64 *val)
{
	struct clk *clock = data;
	struct ccu_clk *ccu_clk;
	int policy;

	ccu_clk = to_ccu_clk(clock);
	policy = ccu_policy_dbg_get_act_policy(ccu_clk);

	*val = policy;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(ccu_policy_fops, ccu_debug_get_policy, NULL, "%llu\n");

static int clock_status_show(struct seq_file *seq, void *p)
{
	struct clk *c = seq->private;
	struct peri_clk *peri_clk;
	struct bus_clk *bus_clk;
	struct ref_clk *ref_clk;
	struct pll_clk *pll_clk;
	struct pll_chnl_clk *pll_chnl_clk;
	struct core_clk *core_clk;
	int enabled = 0;

	switch (c->clk_type) {
	case CLK_TYPE_PERI:
		peri_clk = to_peri_clk(c);
		enabled = peri_clk_get_gating_status(peri_clk);
		break;
	case CLK_TYPE_BUS:
		bus_clk = to_bus_clk(c);
		enabled = bus_clk_get_gating_status(bus_clk);
		break;
	case CLK_TYPE_REF:
		ref_clk = to_ref_clk(c);
		enabled = ref_clk_get_gating_status(ref_clk);
		break;
	case CLK_TYPE_PLL:
		pll_clk = to_pll_clk(c);
		enabled = pll_clk_get_lock_status(pll_clk);
		break;
	case CLK_TYPE_PLL_CHNL:
		pll_chnl_clk = to_pll_chnl_clk(c);
		/*0= divider outputs enabled, 1= divider outputs disabled
		 * So inverting to display status.
		 */
		enabled = !pll_chnl_clk_get_enb_clkout(pll_chnl_clk);
		break;
	case CLK_TYPE_CORE:
		core_clk = to_core_clk(c);
		enabled = core_clk_get_gating_status(core_clk);
		break;
	case CLK_TYPE_CCU:
		if (c->use_cnt > 0)
			enabled = 1;
		break;
	default:
		enabled = -1;
	}
	if (enabled < 0)
		seq_printf(seq, "-1\n");
	else
		seq_printf(seq, "%d\n", enabled);
	return 0;
}

static int fops_clock_status_show_open(struct inode *inode, struct file *file)
{
	return single_open(file, clock_status_show, inode->i_private);
}

static const struct file_operations clock_status_show_fops = {
	.open = fops_clock_status_show_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int clk_debug_reset(void *data, u64 val)
{
	struct clk *clock = data;
	if (val == 1)		/*reset and release the clock from reset */
		clk_reset(clock);
	else
		clk_dbg("Invalid value \n");

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clock_reset_fops, NULL, clk_debug_reset, "%llu\n");

static int clk_debug_set_enable(void *data, u64 val)
{
	struct clk *clock = data;
	if (val == 1)
		clk_enable(clock);
	else if (val == 0)
		clk_disable(clock);
	else
		clk_dbg("Invalid value \n");

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(clock_enable_fops, NULL, clk_debug_set_enable,
			"%llu\n");

static int print_ref_clock_params(struct seq_file *seq, struct clk *temp_clk)
{
	int status, auto_gate, enable_bit;
	struct ref_clk *ref_clk;
	ref_clk = to_ref_clk(temp_clk);
	status = ref_clk_get_gating_status(ref_clk);
	if (status < 0)
		status = -1;
	auto_gate = ref_clk_get_gating_ctrl(ref_clk);
	if (auto_gate < 0)
		auto_gate = -1;
	enable_bit = ref_clk_get_enable_bit(ref_clk);
	if (enable_bit < 0)
		enable_bit = -1;
	seq_printf(seq,
		   "Ref clock:%20s\t\tenable_bit:%d\t\tStatus:%d\t\tuse count:%d\t\tGating:%d\n",
		   temp_clk->name, enable_bit, status, temp_clk->use_cnt,
		   auto_gate);

	return 0;
}

static int print_peri_clock_params(struct seq_file *seq, struct clk *temp_clk)
{
	int status, auto_gate, sleep_prev, enable_bit;
	struct peri_clk *peri_clk;

	peri_clk = to_peri_clk(temp_clk);
	status = peri_clk_get_gating_status(peri_clk);
	if (status < 0)
		status = -1;
	auto_gate = peri_clk_get_gating_ctrl(peri_clk);
	if (auto_gate < 0)
		auto_gate = -1;
	enable_bit = peri_clk_get_enable_bit(peri_clk);
	if (enable_bit < 0)
		enable_bit = -1;
	sleep_prev = !CLK_FLG_ENABLED(temp_clk, DONOT_NOTIFY_STATUS_TO_CCU);
	seq_printf(seq,
		   "Peri clock:%20s\t\tenable_bit:%d\t\tStatus:%d\t\tuse count:%d\t\tGating:%d\t\tprevents_retn:%s\n",
		   temp_clk->name, enable_bit, status, temp_clk->use_cnt,
		   auto_gate, sleep_prev ? "YES" : "NO");

	return 0;
}

static int print_bus_clock_params(struct seq_file *seq, struct clk *temp_clk)
{
	int status, auto_gate, sleep_prev, enable_bit;
	struct bus_clk *bus_clk;

	bus_clk = to_bus_clk(temp_clk);
	status = bus_clk_get_gating_status(bus_clk);
	if (status < 0)
		status = -1;
	auto_gate = bus_clk_get_gating_ctrl(bus_clk);
	if (auto_gate < 0)
		auto_gate = -1;
	enable_bit = bus_clk_get_enable_bit(bus_clk);
	if (enable_bit < 0)
		enable_bit = -1;
	sleep_prev = CLK_FLG_ENABLED(temp_clk, NOTIFY_STATUS_TO_CCU)
	    && !CLK_FLG_ENABLED(temp_clk, AUTO_GATE);
	seq_printf(seq,
		   "Bus clock:%20s\t\tenable_bit:%d\t\tStatus:%d\t\tuse count:%d\t\tGating:%d\t\tprevents_retn:%s\n",
		   temp_clk->name, enable_bit, status, temp_clk->use_cnt,
		   auto_gate, sleep_prev ? "YES" : "NO");

	return 0;

}

static int print_pll_clock_params(struct seq_file *seq, struct clk *temp_clk)
{
	int lock, pdiv, ndiv_int, ndiv_frac, idle_pwrdwn_sw_ovrrid, pll_pwrdwn;
	struct pll_clk *pll_clk;

	pll_clk = to_pll_clk(temp_clk);
	lock = pll_clk_get_lock_status(pll_clk);
	if (lock < 0)
		lock = -1;
	pdiv = pll_clk_get_pdiv(pll_clk);
	if (pdiv < 0)
		pdiv = -1;
	ndiv_int = pll_clk_get_ndiv_int(pll_clk);
	if (ndiv_int < 0)
		ndiv_int = -1;
	ndiv_frac = pll_clk_get_ndiv_frac(pll_clk);
	if (ndiv_frac < 0)
		ndiv_frac = -1;
	idle_pwrdwn_sw_ovrrid = pll_clk_get_idle_pwrdwn_sw_ovrride(pll_clk);
	if (idle_pwrdwn_sw_ovrrid < 0)
		idle_pwrdwn_sw_ovrrid = -1;
	pll_pwrdwn = pll_clk_get_pwrdwn(pll_clk);
	if (pll_pwrdwn < 0)
		pll_pwrdwn = -1;

	seq_printf(seq,
		   "PLL clock:%20s\t\tLock:%d\t\tpdiv:%x\t\tndiv_int:%x\t\tndiv_frac:%x\t\tidle_pwrdwn_sw_ovrrid:%d\tpwr_dwn:%d\n",
		   temp_clk->name, lock, pdiv, ndiv_int, ndiv_frac,
		   idle_pwrdwn_sw_ovrrid, pll_pwrdwn);

	return 0;
}

static int print_pll_chnl_clock_params(struct seq_file *seq,
				       struct clk *temp_clk)
{
	int mdiv, out_enable;
	struct pll_chnl_clk *pll_chnl_clk;

	pll_chnl_clk = to_pll_chnl_clk(temp_clk);
	mdiv = pll_chnl_clk_get_mdiv(pll_chnl_clk);
	if (mdiv < 0)
		mdiv = -1;
	out_enable = pll_chnl_clk_get_enb_clkout(pll_chnl_clk);
	if (out_enable < 0)
		out_enable = -1;

	seq_printf(seq,
		   "PLL_chnl clock:%20s\t\tmdiv:%x\t\tclkout_enable:%d\t\t\n",
		   temp_clk->name, mdiv, out_enable);

	return 0;

}

static int print_core_clock_params(struct seq_file *seq, struct clk *temp_clk)
{
	int status, auto_gate, enable_bit;
	struct core_clk *core_clk;

	core_clk = to_core_clk(temp_clk);
	status = core_clk_get_gating_status(core_clk);
	if (status < 0)
		status = -1;
	auto_gate = core_clk_get_gating_ctrl(core_clk);
	if (auto_gate < 0)
		auto_gate = -1;
	enable_bit = core_clk_get_enable_bit(core_clk);
	if (enable_bit < 0)
		enable_bit = -1;

	seq_printf(seq,
		   "core clock:%20s\t\tenable_bit:%d\t\tStatus:%d\t\tGating:%d\t\t\n",
		   temp_clk->name, enable_bit, status, auto_gate);

	return 0;
}

static int ccu_clock_list_show(struct seq_file *seq, void *p)
{
	struct clk *clock = seq->private;
	struct ccu_clk *ccu_clk;
	struct clk *temp_clk;

	ccu_clk = to_ccu_clk(clock);
	list_for_each_entry(temp_clk, &ccu_clk->clk_list, list) {
		switch (temp_clk->clk_type) {
		case CLK_TYPE_REF:
			print_ref_clock_params(seq, temp_clk);
			break;

		case CLK_TYPE_PERI:
			print_peri_clock_params(seq, temp_clk);
			break;
		case CLK_TYPE_BUS:
			print_bus_clock_params(seq, temp_clk);
			break;
		case CLK_TYPE_PLL:
			print_pll_clock_params(seq, temp_clk);
			break;
		case CLK_TYPE_PLL_CHNL:
			print_pll_chnl_clock_params(seq, temp_clk);
			break;
		case CLK_TYPE_CORE:
			print_core_clock_params(seq, temp_clk);
			break;
		default:
			seq_printf(seq,
				   "clock:%20s\t\tuse count:%d\t\tGating:%d\n",
				   temp_clk->name, temp_clk->use_cnt,
				   CLK_FLG_ENABLED(temp_clk, AUTO_GATE));
			break;
		}
	}

	return 0;
}

static int fops_ccu_clock_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, ccu_clock_list_show, inode->i_private);
}

static const struct file_operations ccu_clock_list_fops = {
	.open = fops_ccu_clock_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int clk_parent_show(struct seq_file *seq, void *p)
{
	struct clk *clock = seq->private;
	struct peri_clk *peri_clk;
	struct bus_clk *bus_clk;
	struct ref_clk *ref_clk;
	struct pll_chnl_clk *pll_chnl_clk;
	struct core_clk *core_clk;
	switch (clock->clk_type) {
	case CLK_TYPE_PERI:
		peri_clk = to_peri_clk(clock);
		seq_printf(seq, "name   -- %s\n", clock->name);
		if ((peri_clk->src_clk.count > 0)
		    && (peri_clk->src_clk.src_inx < peri_clk->src_clk.count))
			seq_printf(seq, "parent -- %s\n",
				   peri_clk->src_clk.clk[peri_clk->src_clk.
							 src_inx]->name);
		else
			seq_printf(seq, "parent -- NULL\n");
		break;
	case CLK_TYPE_BUS:
		bus_clk = to_bus_clk(clock);
		seq_printf(seq, "name   -- %s\n", clock->name);
		if (bus_clk->freq_tbl_index < 0 && bus_clk->src_clk)
			seq_printf(seq, "parent -- %s\n",
				   bus_clk->src_clk->name);
		else
			seq_printf(seq, "parent derived from internal bus\n");
		break;
	case CLK_TYPE_REF:
		ref_clk = to_ref_clk(clock);
		seq_printf(seq, "name   -- %s\n", clock->name);
		if ((ref_clk->src_clk.count > 0)
		    && (ref_clk->src_clk.src_inx < ref_clk->src_clk.count))
			seq_printf(seq, "parent -- %s\n",
				   ref_clk->src_clk.clk[ref_clk->src_clk.
							src_inx]->name);
		else
			seq_printf(seq, "Derived from %s ccu\n",
				   ref_clk->ccu_clk->clk.name);
		break;
	case CLK_TYPE_PLL:
		seq_printf(seq, "PLL:  %s\n", clock->name);
		break;

	case CLK_TYPE_PLL_CHNL:
		pll_chnl_clk = to_pll_chnl_clk(clock);
		seq_printf(seq, "PLL:  %s; PLL channel:%s\n",
			   pll_chnl_clk->pll_clk->clk.name, clock->name);
		break;
	case CLK_TYPE_CORE:
		core_clk = to_core_clk(clock);
		seq_printf(seq, "PLL:  %s; core_clk:%s\n",
			   core_clk->pll_clk->clk.name, clock->name);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int fops_parent_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_parent_show, inode->i_private);
}

static const struct file_operations clock_parent_fops = {
	.open = fops_parent_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int clk_source_show(struct seq_file *seq, void *p)
{
	struct clk *clock = seq->private;
	struct peri_clk *peri_clk;
	struct bus_clk *bus_clk;
	struct ref_clk *ref_clk;
	struct pll_chnl_clk *pll_chnl_clk;
	struct core_clk *core_clk;
	switch (clock->clk_type) {
	case CLK_TYPE_PERI:
		peri_clk = to_peri_clk(clock);
		if (peri_clk->src_clk.count > 0) {
			int i;
			seq_printf(seq, "clock source for %s\n", clock->name);
			for (i = 0; i < peri_clk->src_clk.count; i++) {
				seq_printf(seq, "%d   %s\n", i,
					   peri_clk->src_clk.clk[i]->name);
			}
		} else
			seq_printf(seq, "no source for %s\n", clock->name);
		break;
	case CLK_TYPE_BUS:
		bus_clk = to_bus_clk(clock);
		if (bus_clk->freq_tbl_index < 0 && bus_clk->src_clk)
			seq_printf(seq, "source for %s is %s\n", clock->name,
				   bus_clk->src_clk->name);
		else
			seq_printf(seq, "%s derived from %s CCU\n", clock->name,
				   bus_clk->ccu_clk->clk.name);
		break;
	case CLK_TYPE_REF:
		ref_clk = to_ref_clk(clock);
		if ((ref_clk->src_clk.count > 0)
		    && (ref_clk->src_clk.src_inx < ref_clk->src_clk.count))
			seq_printf(seq, "parent -- %s\n",
				   ref_clk->src_clk.clk[ref_clk->src_clk.
							src_inx]->name);
		else
			seq_printf(seq, "%s derived from %s CCU\n", clock->name,
				   ref_clk->ccu_clk->clk.name);
		break;
	case CLK_TYPE_PLL:
		seq_printf(seq, "PLL: %s\n", clock->name);
		break;
	case CLK_TYPE_PLL_CHNL:
		pll_chnl_clk = to_pll_chnl_clk(clock);
		seq_printf(seq, "PLL: %s PLL Channel:%s\n",
			   pll_chnl_clk->pll_clk->clk.name, clock->name);
		break;
	case CLK_TYPE_CORE:
		core_clk = to_core_clk(clock);
		seq_printf(seq, "PLL: %s core_clk:%s\n",
			   core_clk->pll_clk->clk.name, clock->name);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int fops_source_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_source_show, inode->i_private);
}

static const struct file_operations clock_source_fops = {
	.open = fops_source_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __ccu_volt_id_update_for_freqid(struct clk *clk, u8 freq_id,
					   u8 volt_id)
{
	struct ccu_clk *ccu_clk;
	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	if (freq_id > 8 || volt_id > 0xF)
		return -EINVAL;
	if (freq_id > (ccu_clk->freq_count - 1))
		pr_info("invalid freq_id for this CCU\n");

	/* enable write access */
	ccu_write_access_enable(ccu_clk, true);
	/*stop policy engine */
	ccu_policy_engine_stop(ccu_clk);
	ccu_set_voltage(ccu_clk, freq_id, volt_id);

	/*Set ATL & AC */
	if (clk->flags & CCU_TARGET_LOAD) {
		if (clk->flags & CCU_TARGET_AC)
			ccu_set_policy_ctrl(ccu_clk, POLICY_CTRL_GO_AC,
					    CCU_AUTOCOPY_ON);
		ccu_policy_engine_resume(ccu_clk, CCU_LOAD_TARGET);
	} else
		ccu_policy_engine_resume(ccu_clk, CCU_LOAD_ACTIVE);

	/* disable write access */
	ccu_write_access_enable(ccu_clk, false);

	return 0;
}

int ccu_volt_id_update_for_freqid(struct clk *clk, u8 freq_id, u8 volt_id)
{
	int ret = 0;
	unsigned long flags;
	struct ccu_clk *ccu_clk;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;
	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);

	pr_info("%s - %s\n", __func__, clk->name);
	clk_lock(clk, &flags);
	CCU_ACCESS_EN(ccu_clk, 1);
	ret = __ccu_volt_id_update_for_freqid(clk, freq_id, volt_id);
	CCU_ACCESS_EN(ccu_clk, 0);
	clk_unlock(clk, &flags);

	return ret;
}

int ccu_volt_tbl_display(struct clk *clk, u8 *volt_tbl)
{
	int ret = 0;
	int freq_id;
	unsigned long flags;
	struct ccu_clk *ccu_clk;

	if (volt_tbl == NULL)
		return -EINVAL;
	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;
	BUG_ON(clk->clk_type != CLK_TYPE_CCU);
	ccu_clk = to_ccu_clk(clk);
	clk_lock(clk, &flags);
	CCU_ACCESS_EN(ccu_clk, 1);

	for (freq_id = 0; freq_id < 8; freq_id++) {
		/*if (freq_id > (ccu_clk->freq_count - 1))
		   pr_info("invalid freq_id for this CCU\n"); */
		volt_tbl[freq_id] = ccu_get_voltage(ccu_clk, freq_id);
	}

	CCU_ACCESS_EN(ccu_clk, 0);
	clk_unlock(clk, &flags);

	return ret;
}


static ssize_t ccu_volt_id_display(struct file *file, char __user *buf,
				   size_t len, loff_t *offset)
{
	u8 volt_tbl[8];
	int i, length = 0;
	static ssize_t total_len = 0;
	char out_str[400];
	char *out_ptr;
	struct clk *clk = file->private_data;

	/* This is to avoid the read getting called again and again. This is
	 * useful only if we have large chunk of data greater than PAGE_SIZE. we
	 * have only small chunk of data */
	if (total_len > 0) {
		total_len = 0;
		return 0;
	}

	memset(volt_tbl, 0, sizeof(volt_tbl));
	memset(out_str, 0, sizeof(out_str));
	out_ptr = &out_str[0];
	if (len < 400)
		return -EINVAL;

	if (ccu_volt_tbl_display(clk, volt_tbl))
		return -EINVAL;

	for (i = 0; i < 8; i++) {
		length = sprintf(out_ptr, "volt_tbl[%d]: %x\n", i, volt_tbl[i]);
		out_ptr += length;
		total_len += length;
	}

	if (copy_to_user(buf, out_str, total_len))
		return -EFAULT;

	return total_len;

}

static ssize_t ccu_volt_id_update(struct file *file,
				  char const __user *buf, size_t count,
				  loff_t *offset)
{
	struct clk *clk = file->private_data;
	u32 len = 0;
	char *str_ptr;
	u32 freq_id = 0xFFFF, volt_id = 0xFFFF;

	char input_str[10];

	memset(input_str, 0, 10);
	if (count > 10)
		len = 10;
	else
		len = count;

	if (copy_from_user(input_str, buf, len))
		return -EFAULT;

	str_ptr = &input_str[0];
	sscanf(str_ptr, "%x%x", &freq_id, &volt_id);
	if (freq_id == 0xFFFF || volt_id == 0xFFFF) {
		printk("invalid input\n");
		return count;
	}
	if (freq_id > 7 || volt_id > 0xF) {
		pr_info("Invalid param\n");
		return count;
	}
	ccu_volt_id_update_for_freqid(clk, (u8)freq_id, (u8)volt_id);
	return count;
}

static struct file_operations ccu_volt_tbl_update_fops = {
	.open = clk_debugfs_open,
	.write = ccu_volt_id_update,
	.read = ccu_volt_id_display,
};

static struct dentry *dent_clk_root_dir;
int __init clock_debug_init(void)
{
	/* create root clock dir /clock */
	dent_clk_root_dir = debugfs_create_dir("clock", 0);
	if (!dent_clk_root_dir)
		return -ENOMEM;
	if (!debugfs_create_u32("debug", S_IRUGO | S_IWUSR,
				dent_clk_root_dir, (int *)&clk_debug))
		return -ENOMEM;

	if (!debugfs_create_file("clk_idle_debug", S_IRUSR | S_IWUSR,
				 dent_clk_root_dir, NULL,
				 &clock_idle_debug_fops))
		return -ENOMEM;
	if (!debugfs_create_file("clk_mon_debug", S_IRUSR | S_IWUSR,
				 dent_clk_root_dir, NULL,
				 &clock_mon_debug_fops))
		return -ENOMEM;

	return 0;
}

int __init clock_debug_add_ccu(struct clk *c)
{
	#define DENT_COUNT 6
	struct ccu_clk *ccu_clk;
	int i = 0;
	struct dentry *dentry[DENT_COUNT] = {NULL};


	BUG_ON(!dent_clk_root_dir);
	ccu_clk = to_ccu_clk(c);

	ccu_clk->dent_ccu_dir = debugfs_create_dir(c->name, dent_clk_root_dir);
	if (!ccu_clk->dent_ccu_dir)
		goto err;

	dentry[i] = debugfs_create_file("clock_list",
					      S_IRUGO, ccu_clk->dent_ccu_dir, c,
					      &ccu_clock_list_fops);
	if (!dentry[i])
		goto err;

	dentry[++i] = debugfs_create_file("freq_id", S_IRUGO,
					  ccu_clk->dent_ccu_dir, c,
					  &ccu_freqid_fops);
	if (!dentry[i])
		goto err;

	dentry[++i] = debugfs_create_file("policy", S_IRUGO,
					  ccu_clk->dent_ccu_dir, c,
					  &ccu_policy_fops);
	if (!dentry[i])
		goto err;

	dentry[++i] = debugfs_create_u32("use_cnt", S_IRUGO,
					ccu_clk->dent_ccu_dir, &c->use_cnt);
	if (!dentry[i])
		goto err;
	dentry[++i] = debugfs_create_file("volt_id_update",
					    (S_IWUSR | S_IRUSR),
					    ccu_clk->dent_ccu_dir, c,
					    &ccu_volt_tbl_update_fops);
	if (!dentry[i])
		goto err;

	dentry[++i] = debugfs_create_file("wr_en", S_IWUSR|S_IRUSR,
					  ccu_clk->dent_ccu_dir, c,
					  &ccu_wr_en_fops);
	if (!dentry[i])
		goto err;

	if (CLK_FLG_ENABLED(c, CCU_DBG_BUS_EN)) {
		struct dentry *dent;
		dent = debugfs_create_file("dbg_bus", S_IWUSR|S_IRUGO,
					  ccu_clk->dent_ccu_dir, c,
					  &ccu_dbg_bus_fops);
	}

	return 0;
err:
	for (i = 0; i < DENT_COUNT && dentry[i]; i++)
		debugfs_remove(dentry[i]);
	debugfs_remove(ccu_clk->dent_ccu_dir);

	return -ENOMEM;
}

int __init clock_debug_add_clock(struct clk *c)
{
	struct dentry *dent_clk_dir = 0, *dent_rate = 0, *dent_enable = 0,
	    *dent_status = 0, *dent_flags = 0, *dent_use_cnt = 0, *dent_id = 0,
	    *dent_parent = 0, *dent_source = 0, *dent_ccu_dir = 0,
	    *dent_clk_mon = 0, *dent_reset = 0;
	struct peri_clk *peri_clk;
	struct pll_clk *pll_clk;
	struct core_clk *core_clk;
	struct pll_chnl_clk *pll_chnl_clk;
	struct bus_clk *bus_clk;
	struct ref_clk *ref_clk;
	switch (c->clk_type) {
	case CLK_TYPE_REF:
		ref_clk = to_ref_clk(c);
		dent_ccu_dir = ref_clk->ccu_clk->dent_ccu_dir;
		break;
	case CLK_TYPE_BUS:
		bus_clk = to_bus_clk(c);
		dent_ccu_dir = bus_clk->ccu_clk->dent_ccu_dir;
		break;
	case CLK_TYPE_PERI:
		peri_clk = to_peri_clk(c);
		dent_ccu_dir = peri_clk->ccu_clk->dent_ccu_dir;
		break;

	case CLK_TYPE_PLL:
		pll_clk = to_pll_clk(c);
		dent_ccu_dir = pll_clk->ccu_clk->dent_ccu_dir;
		break;

	case CLK_TYPE_PLL_CHNL:
		pll_chnl_clk = to_pll_chnl_clk(c);
		dent_ccu_dir = pll_chnl_clk->ccu_clk->dent_ccu_dir;
		break;

	case CLK_TYPE_CORE:
		core_clk = to_core_clk(c);
		dent_ccu_dir = core_clk->ccu_clk->dent_ccu_dir;
		break;
	default:
		return -EINVAL;
	}
	if (!dent_ccu_dir)
		return -ENOMEM;

	/* create root clock dir /clock/clk_a */
	dent_clk_dir = debugfs_create_dir(c->name, dent_ccu_dir);
	if (!dent_clk_dir)
		goto err;

	/* file /clock/clk_a/enable */
	dent_enable = debugfs_create_file("enable", S_IRUGO | S_IWUSR,
					  dent_clk_dir, c, &clock_enable_fops);
	if (!dent_enable)
		goto err;

	/* file /clock/clk_a/reset */
	dent_reset = debugfs_create_file("reset", S_IRUGO |
					 S_IWUSR, dent_clk_dir, c,
					 &clock_reset_fops);
	if (!dent_reset)
		goto err;

	/* file /clock/clk_a/status */
	dent_status = debugfs_create_file("status", S_IRUGO |
					  S_IWUSR, dent_clk_dir, c,
					  &clock_status_show_fops);
	if (!dent_status)
		goto err;

	/* file /clock/clk_a/rate */
	dent_rate = debugfs_create_file("rate", S_IRUGO | S_IWUSR,
					dent_clk_dir, c, &clock_rate_fops);
	if (!dent_rate)
		goto err;
	/* file /clock/clk_a/monitor */
	dent_clk_mon = debugfs_create_file("clk_mon", S_IRUGO,
					   dent_clk_dir, c, &clock_mon_fops);
	if (!dent_clk_mon)
		goto err;

	/* file /clock/clk_a/flags */
	dent_flags = debugfs_create_u32("flags", S_IRUGO |
					S_IWUSR, dent_clk_dir,
					(unsigned int *)&c->flags);
	if (!dent_flags)
		goto err;

	/* file /clock/clk_a/use_cnt */
	dent_use_cnt = debugfs_create_u32("use_cnt", S_IRUGO,
					  dent_clk_dir,
					  (unsigned int *)&c->use_cnt);
	if (!dent_use_cnt)
		goto err;

	/* file /clock/clk_a/id */
	dent_id = debugfs_create_u32("id", S_IRUGO, dent_clk_dir,
				     (unsigned int *)&c->id);
	if (!dent_id)
		goto err;

	/* file /clock/clk_a/parent */
	dent_parent = debugfs_create_file("parent", S_IRUGO,
					  dent_clk_dir, c, &clock_parent_fops);
	if (!dent_parent)
		goto err;

	/* file /clock/clk_a/source */
	dent_source = debugfs_create_file("source", S_IRUGO,
					  dent_clk_dir, c, &clock_source_fops);
	if (!dent_source)
		goto err;

	return 0;

err:
	debugfs_remove(dent_rate);
	debugfs_remove(dent_flags);
	debugfs_remove(dent_use_cnt);
	debugfs_remove(dent_id);
	debugfs_remove(dent_parent);
	debugfs_remove(dent_source);
	debugfs_remove(dent_clk_dir);
	return -ENOMEM;
}
#endif
