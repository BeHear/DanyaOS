#ifndef DANYA_RTC_H
#define DANYA_RTC_H

#include "../include/types.h"

// CMOS/RTC registers
#define CMOS_REG_SECONDS    0x00
#define CMOS_REG_MINUTES    0x02
#define CMOS_REG_HOURS      0x04
#define CMOS_REG_WEEKDAY    0x06
#define CMOS_REG_DAY        0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_STATUS_A   0x0A
#define CMOS_REG_STATUS_B   0x0B
#define CMOS_REG_CENTURY    0x32

// Status Register A
#define CMOS_STAT_A_UPDATE_IN_PROGRESS  0x80

// Status Register B
#define CMOS_STAT_B_BCD         0x00  // default
#define CMOS_STAT_B_BINARY     0x04
#define CMOS_STAT_B_24HOUR     0x02

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
    uint8_t  weekday;     // 1=Sunday .. 7=Saturday
    uint16_t century;     // typically 20 or 19
} rtc_time_t;

// Init RTC
void rtc_init(void);

// Read current time from CMOS
// Returns 0 on success, -1 if read failed
int  rtc_read_time(rtc_time_t* out);

// Format time into buffer (like "2026-06-26 15:30:45")
void rtc_format_time(char* buf, size_t n, const rtc_time_t* t);

#endif
