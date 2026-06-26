#include "rtc.h"
#include "../include/io.h"
#include "../libc/string.h"
#include "../drivers/vga.h"

static int rtc_initialized = 0;
static int rtc_binary_mode = 0;
static int rtc_24hour_mode = 1;

// Read a CMOS register (disables NMI during access)
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, (1 << 7) | reg);   // NMI disabled
    io_wait();
    uint8_t val = inb(0x71);
    outb(0x70, 0x0F);             // Restore NMI + reset index
    return val;
}

// Wait for CMOS update to finish (Status Register A bit 7 == 0)
static int cmos_wait_update(void) {
    for (int i = 0; i < 100000; i++) {
        uint8_t a = cmos_read(CMOS_REG_STATUS_A);
        if (!(a & CMOS_STAT_A_UPDATE_IN_PROGRESS)) return 0;
    }
    return -1; // timeout
}

// Convert BCD to binary
static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_init(void) {
    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    rtc_binary_mode = (status_b & CMOS_STAT_B_BINARY) ? 1 : 0;
    rtc_24hour_mode = (status_b & CMOS_STAT_B_24HOUR) ? 1 : 0;
    rtc_initialized = 1;
}

int rtc_read_time(rtc_time_t* out) {
    if (!rtc_initialized) return -1;
    if (!out) return -1;

    if (cmos_wait_update() != 0) return -1;

    // Read all time/date registers atomically
    uint8_t sec   = cmos_read(CMOS_REG_SECONDS);
    uint8_t min   = cmos_read(CMOS_REG_MINUTES);
    uint8_t hour  = cmos_read(CMOS_REG_HOURS);
    uint8_t wday  = cmos_read(CMOS_REG_WEEKDAY);
    uint8_t day   = cmos_read(CMOS_REG_DAY);
    uint8_t mon   = cmos_read(CMOS_REG_MONTH);
    uint8_t year  = cmos_read(CMOS_REG_YEAR);
    uint8_t cent  = cmos_read(CMOS_REG_CENTURY);

    // Convert from BCD if needed
    if (!rtc_binary_mode) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
        cent = bcd_to_bin(cent);

        if (!rtc_24hour_mode) {
            uint8_t pm = hour & 0x80;
            hour = bcd_to_bin(hour & 0x7F);
            if (pm && hour < 12) hour += 12;
            if (!pm && hour == 12) hour = 0;
        }
    } else {
        // Binary mode 12-hour check
        if (!rtc_24hour_mode) {
            uint8_t pm = hour & 0x80;
            hour &= 0x7F;
            if (pm && hour < 12) hour += 12;
            if (!pm && hour == 12) hour = 0;
        }
    }

    // Validate ranges
    if (mon < 1 || mon > 12) mon = 1;
    if (day < 1 || day > 31) day = 1;
    if (hour > 23) hour = 0;
    if (min > 59) min = 0;
    if (sec > 59) sec = 0;

    out->seconds = sec;
    out->minutes = min;
    out->hours   = hour;
    out->weekday = wday;
    out->day     = day;
    out->month   = mon;
    out->century = (cent > 0) ? (uint16_t)cent : 20;
    out->year    = (cent > 0) ? ((uint16_t)cent * 100 + year)
                              : (2000 + year);

    return 0;
}

void rtc_format_time(char* buf, size_t n, const rtc_time_t* t) {
    if (!buf || n == 0) return;
    if (!t) {
        snprintf(buf, n, "RTC unavailable");
        return;
    }
    snprintf(buf, n, "%04u-%02u-%02u %02u:%02u:%02u",
             (unsigned)t->year, (unsigned)t->month, (unsigned)t->day,
             (unsigned)t->hours, (unsigned)t->minutes, (unsigned)t->seconds);
}
