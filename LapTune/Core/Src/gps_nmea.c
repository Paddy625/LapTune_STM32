#include "gps_nmea.h"
#include <stddef.h>
#include <string.h>

static int hex(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static uint8_t number(const char *s, uint32_t max, uint32_t *value)
{
    uint32_t n = 0;
    if (*s == '\0') return 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        uint32_t digit = (uint32_t)(*s - '0');
        if (n > max / 10U || (n == max / 10U && digit > max % 10U)) return 0;
        n = n * 10U + digit;
    }
    *value = n;
    return 1;
}

static uint8_t pair(const char *s, uint8_t *value)
{
    if (s[0] < '0' || s[0] > '9' || s[1] < '0' || s[1] > '9') return 0;
    *value = (uint8_t)((s[0] - '0') * 10 + s[1] - '0');
    return 1;
}

static uint8_t utc(const char *s, GPS_Data *out)
{
    if (strlen(s) < 6U || !pair(s, &out->hour) || !pair(s+2, &out->minute) ||
        !pair(s+4, &out->second)) return 0;
    if (out->hour > 23U || out->minute > 59U || out->second > 60U) return 0;
    out->millisecond=0;
    if (s[6] == '\0') return 1;
    if (s[6] != '.' || s[7] == '\0') return 0;
    uint16_t factor=100;
    for (s += 7; *s; ++s) {
        if (*s < '0' || *s > '9') return 0;
        out->millisecond += (uint16_t)(*s-'0')*factor;
        factor/=10;
    }
    return 1;
}

static uint8_t date(const char *s, GPS_Data *out)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint8_t year;
    if (strlen(s) != 6U || !pair(s, &out->day) || !pair(s+2, &out->month) ||
        !pair(s+4, &year)) return 0;
    out->year = (year >= 80U ? 1900U : 2000U) + year;
    if (out->month < 1U || out->month > 12U) return 0;
    uint8_t limit = days[out->month-1U];
    if (out->month == 2U && out->year % 4U == 0U &&
        (out->year % 100U != 0U || out->year % 400U == 0U)) ++limit;
    return out->day >= 1U && out->day <= limit;
}

static uint8_t coordinate(const char *s, const char *hemi, uint8_t latitude,
                          int32_t *out)
{
    size_t degree_digits = latitude ? 2U : 3U;
    size_t integer_digits = degree_digits + 2U, len = strlen(s);
    uint32_t deg = 0, minutes = 0, fraction = 0, factor = 1000000U;
    if (len < integer_digits || strlen(hemi) != 1U) return 0;
    if (latitude ? (*hemi != 'N' && *hemi != 'S') : (*hemi != 'E' && *hemi != 'W')) return 0;
    for (size_t i = 0; i < integer_digits; ++i) {
        if (s[i] < '0' || s[i] > '9') return 0;
        if (i < degree_digits) deg = deg * 10U + (uint32_t)(s[i] - '0');
        else minutes = minutes * 10U + (uint32_t)(s[i] - '0');
    }
    if (len > integer_digits) {
        if (s[integer_digits] != '.' || len == integer_digits + 1U) return 0;
        for (size_t i = integer_digits + 1U; i < len; ++i) {
            if (s[i] < '0' || s[i] > '9') return 0;
            fraction += (uint32_t)(s[i] - '0') * factor;
            factor /= 10U;
        }
    }
    uint32_t max = latitude ? 90U : 180U;
    if (minutes >= 60U || deg > max || (deg == max && (minutes || fraction))) return 0;
    int32_t value = (int32_t)(deg * 10000000U + (minutes * 10000000U + fraction + 30U) / 60U);
    *out = (*hemi == 'S' || *hemi == 'W') ? -value : value;
    return 1;
}

static uint8_t altitude(const char *s, int32_t *out)
{
    uint8_t negative = *s == '-';
    if (*s == '-' || *s == '+') ++s;
    uint32_t metres = 0, fraction = 0, factor = 100U;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') {
        if (metres > 100000U) return 0;
        metres = metres * 10U + (uint32_t)(*s++ - '0');
    }
    if (metres > 1000000U) return 0;
    if (*s == '.') {
        ++s;
        if (*s == '\0') return 0;
        for (; *s; ++s) {
            if (*s < '0' || *s > '9') return 0;
            fraction += (uint32_t)(*s - '0') * factor;
            factor /= 10U;
        }
    }
    if (*s != '\0') return 0;
    int32_t mm = (int32_t)(metres * 1000U + fraction);
    *out = negative ? -mm : mm;
    return 1;
}

static uint8_t speed(const char *s, uint32_t *out)
{
    /* Unsigned knots, at most 9999.999999; validate any further decimal digits.
       1 knot = 1.852 km/h. Round the result to 0.001 km/h without float I/O. */
    uint32_t whole=0, fraction=0, factor=100000U;
    if (*s<'0' || *s>'9') return 0;
    while (*s>='0' && *s<='9') {
        uint32_t digit=(uint32_t)(*s++-'0');
        if (whole>(9999U-digit)/10U) return 0;
        whole=whole*10U+digit;
    }
    if (*s=='.') {
        if (*++s=='\0') return 0;
        for (;*s;++s) {
            if (*s<'0' || *s>'9') return 0;
            fraction+=(uint32_t)(*s-'0')*factor; factor/=10U;
        }
    }
    if (*s!='\0') return 0;
    uint64_t microknots=(uint64_t)whole*1000000U+fraction;
    *out=(uint32_t)((microknots*1852U+500000U)/1000000U);
    return 1;
}

void GPS_NMEA_Init(GPS_Data *data) { memset(data, 0, sizeof(*data)); }

uint8_t GPS_NMEA_Parse(GPS_Data *data, const char *line, uint32_t now)
{
    char body[GPS_NMEA_MAX_LENGTH];
    char *f[20];
    size_t len = 0, count = 0;
    uint8_t checksum = 0;
    if (line == NULL || *line++ != '$') return 0;
    while (line[len] != '\0' && line[len] != '*' && len < sizeof(body)-1U) {
        checksum ^= (uint8_t)line[len]; body[len] = line[len]; ++len;
    }
    if (line[len] != '*') return 0;
    const char *check = line + len + 1U;
    if (strlen(check) < 2U || hex(check[0]) < 0 || hex(check[1]) < 0 ||
        checksum != (uint8_t)((hex(check[0]) << 4) | hex(check[1]))) return 0;
    check += 2;
    if (*check == '\r') ++check;
    if (*check == '\n') ++check;
    if (*check != '\0') return 0;
    body[len] = '\0';
    f[count++] = body;
    for (size_t i = 0; i < len; ++i) if (body[i] == ',') {
        if (count == sizeof(f)/sizeof(f[0])) return 0;
        body[i] = '\0'; f[count++] = &body[i+1U];
    }
    if (strlen(f[0]) != 5U || (strncmp(f[0], "GP", 2) && strncmp(f[0], "GN", 2))) return 0;
    GPS_Data next = *data;
    if (!strcmp(f[0]+2, "RMC")) {
        if (count < 10U || strlen(f[2]) != 1U || (*f[2] != 'A' && *f[2] != 'V')) return 0;
        if (*f[2] == 'V') {
            next.position_valid = next.altitude_valid = next.datetime_valid = next.speed_valid = 0;
        } else {
            if (!utc(f[1], &next) || !date(f[9], &next) ||
                !coordinate(f[3], f[4], 1, &next.latitude_e7) ||
                !coordinate(f[5], f[6], 0, &next.longitude_e7)) return 0;
            next.position_valid = next.datetime_valid = 1;
            next.position_ms = next.datetime_ms = now;
            next.speed_valid=0;
            if (*f[7]!='\0') {
                if (!speed(f[7],&next.speed_millikmh)) return 0;
                next.speed_valid=1; next.speed_ms=now;
            }
        }
    } else if (!strcmp(f[0]+2, "GGA")) {
        uint32_t quality, satellites;
        if (count < 11U || !number(f[6], 8U, &quality) ||
            !number(f[7], 99U, &satellites)) return 0;
        next.fix_quality = (uint8_t)quality; next.satellites = (uint8_t)satellites;
        if (quality == 0U) {
            next.position_valid = next.altitude_valid = next.speed_valid = 0;
        } else {
            if (!coordinate(f[2], f[3], 1, &next.latitude_e7) ||
                !coordinate(f[4], f[5], 0, &next.longitude_e7)) return 0;
            next.position_valid = 1; next.position_ms = now;
            next.altitude_valid = 0;
            if (*f[9] != '\0') {
                if (strcmp(f[10], "M") || !altitude(f[9], &next.altitude_mm)) return 0;
                next.altitude_valid = 1; next.altitude_ms = now;
            }
        }
    } else return 0;
    next.connected = 1; next.sentence_ms = now;
    *data = next;
    return 1;
}

void GPS_NMEA_Expire(GPS_Data *data, uint32_t now)
{
    if ((uint32_t)(now-data->sentence_ms) >= GPS_FRESH_MS) {
        data->connected = 0; data->satellites = data->fix_quality = 0;
    }
    if ((uint32_t)(now-data->position_ms) >= GPS_FRESH_MS) data->position_valid = 0;
    if ((uint32_t)(now-data->altitude_ms) >= GPS_FRESH_MS) data->altitude_valid = 0;
    if ((uint32_t)(now-data->datetime_ms) >= GPS_FRESH_MS) data->datetime_valid = 0;
    if ((uint32_t)(now-data->speed_ms) >= GPS_FRESH_MS) data->speed_valid = 0;
}
