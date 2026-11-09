#pragma once
typedef struct { unsigned short year, month, day, hour, minute, second; unsigned int microsecond; } ScePspDateTime;
int sceRtcGetCurrentClockLocalTime(ScePspDateTime*);
