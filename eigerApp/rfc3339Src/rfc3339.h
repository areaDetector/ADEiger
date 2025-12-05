#pragma once

#include <regex>

#include <epicsTime.h>

namespace rfc3339 {

static const gm_tm_nano_sec ZERO = {
    .ansi_tm = {
        .tm_sec = 0,
        .tm_min = 0,
        .tm_hour = 0,
        .tm_mday = 0,
        .tm_mon = 0,
        .tm_year = 0,
        .tm_gmtoff = 0,
    },
    .nSec = 0,
};

inline const bool equals(const gm_tm_nano_sec & t1, const gm_tm_nano_sec & t2) {
    #define EQ(f) (t1.ansi_tm.tm_ ## f == t2.ansi_tm.tm_ ## f)
    return (
        EQ(year) && EQ(mon) && EQ(mday)
        && EQ(hour) && EQ(min) && EQ(sec)
        && EQ(gmtoff)
        && (t1.nSec == t2.nSec)
    );
    #undef EQ
}

//  Parse an RFC 3339 timestamp from a string representation into a RFC3339Timestamp,
//  with struct tm and long fractional components
//  Input:
//      ts_str: Timestamp as a string
//          Expected to follow CBOR RFC8949 Section 3.4.1 (Standard Date/Time String)
//          Expected format: RFC3339 https://www.rfc-editor.org/rfc/rfc3339.txt
//          Examples:
//             2025-12-05T12:34:56
//             2025-12-05T12:34:56.789
//             2025-12-05T12:34:56.789Z
//             2025-12-05T12:34:56.789-04:00
//  Returns:
//      A gm_tm_nano_sec structure with a struct tm and a nanoseconds component.
inline struct gm_tm_nano_sec parseRfc3339Timestamp(const char * ts_str)
{
    // Pattern for RFC 3339 date-time
    static const std::regex PATTERN(
        "(\\d\\d\\d\\d)-(\\d\\d)-(\\d\\d)"  // RFC 3339 full-date
        "[Tt ]"                             // Separator
        "(\\d\\d):(\\d\\d):(\\d\\d)"        // RFC 3399 time-hour ":" time-minute ":" time-second
        "(\\.\\d{1,9})?"                    // RFC 3399 time-secfrac
        "(Z|[+-]\\d\\d:\\d\\d)?",           // RFC 3399 time-offset
         std::regex::optimize
    );

    // Pattern for RFC 3389 time-numoffset
    static const std::regex OFFSET_PATTERN(
        "([+-])(\\d\\d):(\\d\\d)", std::regex::optimize
    );

    std::cmatch m;
    if (!std::regex_match(ts_str, m, PATTERN))
        return ZERO;

    // Parse matches as numbers
    int year = std::stoi(m[1].str());
    int month = std::stoi(m[2].str());
    int day = std::stoi(m[3].str());
    int hour = std::stoi(m[4].str());
    int minute = std::stoi(m[5].str());
    int second = std::stoi(m[6].str());
 
    // Fractional seconds, in thousands of seconds (skip first character in the match, a period '.')
    int fractional = m[7].length() == 0 ? 0 : std::stoi(m[7].str().c_str() + 1);
    size_t fractional_len = m[7].length() == 0 ? 0 : m[7].length() - 1;

    static const int SCALE[10] = {
        0, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1
    };

    fractional *= SCALE[fractional_len];

    // GMT offset, in seconds
    long gmt_offset_sec = 0;
    if (m[8].length() > 1) {
        std::string gmt_offset(m[8].str());
        std::cmatch o;
        if (!std::regex_match(gmt_offset.c_str(), o, OFFSET_PATTERN))
            return ZERO;

        int offset_sign = o[1].str().c_str()[0] == '-' ? -1 : 1;
        int offset_hours = std::stoi(o[2].str());
        int offset_minutes = std::stoi(o[3].str());

        gmt_offset_sec = offset_sign*(offset_hours*3600 + offset_minutes*60);
    }

    struct tm local_tm = {
        .tm_sec = second,
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = month - 1,    // Months since Jan
        .tm_year = year - 1900, // Years since 1900
    };

    // Convert "local" time to GMT (UTC)
    time_t gmt_time_t = timegm(&local_tm) - gmt_offset_sec;

    // Get back a broken down GMT representation
    struct tm *gmt_tm = gmtime(&gmt_time_t);
    if (!gmt_tm)
        return ZERO;

    return gm_tm_nano_sec {
        .ansi_tm = *gmt_tm,
        .nSec = static_cast<unsigned long>(fractional),
    };
}

}
