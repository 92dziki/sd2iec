/* sd2iec - SD/MMC to Commodore serial bus interface/controller
   Copyright (C) 2007-2012  Ingo Korb <ingo@akana.de>

   Inspiration and low-level SD/MMC access based on code from MMC2IEC
     by Lars Pontoppidan et al., see sdcard.c|h and config.h.

   FAT filesystem access based on code from ChaN and Jim Brain, see ff.c|h.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License only.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   pcf8583.c: RTC support for PCF8583 chips

   This file implements the functions defined in rtc.h.

*/

#include <avr/pgmspace.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "i2c.h"
#include "uart.h"
#include "ustring.h"
#include "utils.h"
#include "time.h"
#include "rtc.h"

#define PCF8583_ADDR 0xa0

#define REG_CONTROL 0
#define REG_S100    1
#define REG_SECONDS 2
#define REG_MINUTES 3
#define REG_HOURS   4
#define REG_YDATE   5
#define REG_WMONTH  6
/* Use the first four ram bytes to store the year and its complement.       */
/* If the complement doesn't match the RTC contents are considered invalid. */
#define REG_YEAR1   16
#define REG_YEAR2   17
#define REG_YEARC1  18
#define REG_YEARC2  19

#define CTL_STOP_CLOCK  0x80
#define CTL_START_CLOCK 0

/* Default date/time if the RTC isn't preset or not set: 1982-08-31 00:00:00 */
PROGMEM struct tm defaultdate = {
  0, 0, 0, 31, 8, 82, 2
};

rtcstate_t rtc_state;

/* Read the current time from the RTC */
/* Will auto-adjust the stored year if required */
void read_rtc(struct tm *time) {
  union {
    uint8_t  bytes[5];
    uint16_t words[2];
  } tmp;

  /* Set to default value in case we abort */
  memcpy_P(time, &defaultdate, sizeof(struct tm));
  if (rtc_state != RTC_OK)
    return;

  if (i2c_read_registers(PCF8583_ADDR, REG_SECONDS, 5, &tmp))
    return;

  time->tm_sec  = bcd2int(tmp.bytes[0]);
  time->tm_min  = bcd2int(tmp.bytes[1]);
  time->tm_hour = bcd2int(tmp.bytes[2]);
  time->tm_mday = bcd2int(tmp.bytes[3] & 0b00111111);
  time->tm_mon  = bcd2int(tmp.bytes[4] & 0b00011111)-1;
  time->tm_wday = bcd2int(tmp.bytes[4] >> 5);

  /* Check for year rollover */
  tmp.bytes[4] = tmp.bytes[3] >> 6;
  i2c_read_registers(PCF8583_ADDR, REG_YEAR1, 4, &tmp);
  if ((tmp.words[0] & 3) != tmp.bytes[4]) {
    /* Year rolled over, increment until the lower two bits match */
    while ((tmp.words[0] & 3) != tmp.bytes[4]) tmp.words[0]++;
    tmp.words[1] = tmp.words[0] ^ 0xffffU;

    /* Store new year to RTC ram */
    i2c_write_registers(PCF8583_ADDR, REG_YEAR1, 4, &tmp);
  }

  time->tm_year = tmp.words[0]-1900;
}

/* Set the time in the RTC */
void set_rtc(struct tm *time) {
  union {
    uint8_t  bytes[5];
    uint16_t words[2];
  } tmp;

  if (rtc_state == RTC_NOT_FOUND)
    return;

  i2c_write_register(PCF8583_ADDR, REG_CONTROL, CTL_STOP_CLOCK);
  tmp.bytes[0] = int2bcd(time->tm_sec);
  tmp.bytes[1] = int2bcd(time->tm_min);
  tmp.bytes[2] = int2bcd(time->tm_hour);
  tmp.bytes[3] = int2bcd(time->tm_mday) | ((time->tm_year & 3) << 6);
  tmp.bytes[4] = int2bcd(time->tm_mon+1)| ((time->tm_wday    ) << 5);
  i2c_write_registers(PCF8583_ADDR, REG_SECONDS, 5, &tmp);
  tmp.words[0] = time->tm_year + 1900;
  tmp.words[1] = (time->tm_year + 1900) ^ 0xffffU;
  i2c_write_registers(PCF8583_ADDR, REG_YEAR1, 4, &tmp);
  i2c_write_register(PCF8583_ADDR, REG_CONTROL, CTL_START_CLOCK);
  rtc_state = RTC_OK;
}

void rtc_init(void) {
  uint8_t tmp[4];

  i2c_init();
  rtc_state = RTC_NOT_FOUND;
  uart_puts_P(PSTR("RTC "));
  if (i2c_write_register(PCF8583_ADDR, REG_CONTROL, CTL_START_CLOCK) ||
      i2c_read_registers(PCF8583_ADDR, REG_YEAR1, 4, tmp)) {
    uart_puts_P(PSTR("not found"));
  } else {
    if (tmp[0] == (tmp[2] ^ 0xff) &&
        tmp[1] == (tmp[3] ^ 0xff)) {
      rtc_state = RTC_OK;
      uart_puts_P(PSTR("ok"));

      /* Dummy RTC read to update the year if required */
      struct tm time;
      read_rtc(&time);
    } else {
      rtc_state = RTC_INVALID;
      uart_puts_P(PSTR("invalid"));
    }
  }
  uart_putcrlf();
}
