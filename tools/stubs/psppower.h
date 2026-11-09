#pragma once
#define PSP_POWER_TICK_SUSPEND 1
int scePowerSetClockFrequency(int,int,int); int scePowerIsBatteryExist(void); int scePowerGetBatteryLifePercent(void); int scePowerTick(int);
