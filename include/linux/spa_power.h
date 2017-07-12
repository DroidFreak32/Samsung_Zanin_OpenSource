
/*
* Samsung Power and Charger.
*
* drivers/power/spa_power.c
*
* Drivers for samsung battery and charger.
* (based on spa.c and linear-power.c)
*
* Copyright (C) 2012, Samsung Electronics.
*
* This program is free software. You can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation
*/
#ifndef __SPA_POWER_H
#define __SPA_POWER_H
// +++ for header files

typedef enum
{
	SPA_EVT_CHARGER,
	SPA_EVT_EOC,
	SPA_EVT_TEMP,
	SPA_EVT_OVP,
	SPA_EVT_LOWBATT,
	SPA_EVT_RECOVER,
	SPA_EVT_CAPACITY,
	SPA_EVT_MAX,
} SPA_EVT_T;

#define ADC_RUNNING_AVG_SHIFT	4
#define ADC_RUNNING_AVG_SIZE	(1 << ADC_RUNNING_AVG_SHIFT)

// Update interval, descending
#define SPA_BATT_UPDATE_INTERVAL_INIT0 30000
#define SPA_BATT_UPDATE_INTERVAL_INIT1 1000
#define SPA_BATT_UPDATE_INTERVAL_INIT2 1000
#define SPA_BATT_UPDATE_INTERVAL_INIT3 1000
#define SPA_BATT_UPDATE_INTERVAL_INIT4 1000
#define SPA_BATT_UPDATE_INTERVAL_INIT5 1000
#define SPA_BATT_UPDATE_INTERVAL_INIT SPA_BATT_UPDATE_INTERVAL_INIT5

#define SPA_BATT_UPDATE_INTERVAL 30000
#define SPA_BATT_UPDATE_INTERVAL_WHILE_CHARGING 5000

// Init progress, descending steps.
enum
{
	SPA_INIT_PROGRESS_STEP0,
	SPA_INIT_PROGRESS_STEP1,
	SPA_INIT_PROGRESS_STEP2,
	SPA_INIT_PROGRESS_STEP3,
	SPA_INIT_PROGRESS_STEP4,
	SPA_INIT_PROGRESS_STEP5,
};

#define SPA_INIT_PROGRESS_START SPA_INIT_PROGRESS_STEP5 
#define SPA_INIT_PROGRESS_DONE SPA_INIT_PROGRESS_STEP0
#define SPA_INIT_PROGRESS_DURATION 10000 // 10 SECONDS

// For charging status, more detail 
enum
{
	SPA_STATUS_NONE, // no additional status.
	SPA_STATUS_SUSPEND_TEMP_COLD,
	SPA_STATUS_SUSPEND_TEMP_HOT,
	SPA_STATUS_SUSPEND_OVP,
	SPA_STATUS_FULL_RECHARGE,
	SPA_STATUS_FULL_FORCE,
	SPA_STATUS_VF_INVALID,
	SPA_STATUS_MAX,
};

typedef enum {
	CHARGE_TIMER_90MIN,
	CHARGE_TIMER_5HOUR,
	CHARGE_TIMER_6HOUR,
}charge_timer_t;

enum
{
	SPA_MACHINE_NONE,
	SPA_MACHINE_NORMAL,
	SPA_MACHINE_FULL_CHARGE_TIMER,
};

enum
{
	SPA_FAKE_CAP_NONE,
	SPA_FAKE_CAP_DEC,
	SPA_FAKE_CAP_INC,
};

/*
	to be checked
		low batt voltage
		recharge voltage
		suspend temperature
		battery information update cycle
*/

typedef struct 
{
	unsigned int phase; // discharging, charging, suspend, full charge and discharge, full charge and recharge, suspend
	unsigned int status;
} SPA_CHARGING_STATUS_T;

struct spa_temp_tb
{
	int adc;
	int temp;
};

struct spa_power_data
{
	unsigned char *charger_name;

	int suspend_temp_hot;
	int recovery_temp_hot;
	int suspend_temp_cold;
	int recovery_temp_cold;

	unsigned int eoc_current;
	unsigned int recharge_voltage;
	unsigned int charging_cur_usb;
	unsigned int charging_cur_wall;

	struct spa_temp_tb *batt_temp_tb;
	unsigned int batt_temp_tb_len;
};

struct spa_charger_info
{
	unsigned char *charger_name;
	unsigned int charger_type;
	unsigned int charging_current;
	unsigned int recharge_voltage;
	unsigned int eoc_current;
	unsigned int top_voltage;
	unsigned int lowbatt_voltage;
	unsigned int charge_expire_time;
	unsigned int times_expired;
};

struct spa_batt_info
{
	int temp;
	int temp_adc;
	char *type;
	unsigned int health;
	unsigned int capacity;
	unsigned int technology;
	unsigned int voltage;
	unsigned int vf_status;
	unsigned int update_interval;

	// for fake full capacity
	unsigned int fakemode;
	unsigned int fake_capacity;
};

struct container_info
{
	int container[ADC_RUNNING_AVG_SIZE];
	int prev_val;
	int avg;
	int sum;
	unsigned int index;
};

#if defined(SPA_DEBUG_FEATURE)
struct spa_dbg_simul
{
	unsigned char ctrl_dbg_switch;
	unsigned char dbg_level;
};
#endif

struct spa_power_desc
{
	// charging and charger information
	struct spa_charger_info charger_info;

	// battery information
	struct spa_batt_info batt_info;

	SPA_CHARGING_STATUS_T charging_status;

	// containers to take average
	struct container_info temp_reading;
	struct container_info volt_reading;

	struct delayed_work battery_work;
	struct delayed_work delayed_init_work;
	struct delayed_work fast_charging_work;

	// full charge timer
	struct delayed_work spa_expire_charge_work;

	struct workqueue_struct *spa_workqueue;
	struct wake_lock spa_wakelock;

	struct spa_power_data *pdata;

	struct
	{
		unsigned int temperature:1;
		unsigned int voltage:1;
		unsigned int reserved:30;
	} new_gathering;

	unsigned int lp_charging;
	unsigned int init_progress;
#if defined(SPA_DEBUG_FEATURE)
	unsigned char dbg_simul;
#endif
	unsigned char dbg_lvl;
};


// --- for header files

extern int spa_event_handler(int evt, void *data);

#endif
