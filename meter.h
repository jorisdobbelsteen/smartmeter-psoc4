/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2021, Joris Dobbelsteen. */

enum METER_POWER_STATE_T
{
    METER_POWER_STATE_ACTIVE,     // must call Meter_ProcessEvents
    METER_POWER_STATE_SLEEP,      // CPU sleep acceptable (e.g. hardware block must remain active)
    METER_POWER_STATE_DEEPSLEEP   // CPU deep-sleep acceptable
};

enum METER_POWER_STATE_T Meter_GetPowerState();  // more complicated

struct dsmr_data_t;

void Meter_Start();
//void Meter_Stop();  // No need found, always active
void Meter_ProcessEvents();

void Meter_SetReceivedDsmrHandler(void(*handler)(struct dsmr_data_t*));
