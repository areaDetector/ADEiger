#include "rfc3339.h"
#include "epicsTime.h"

#include <ctime>
#include <testMain.h>
#include <epicsUnitTest.h>

static inline const gm_tm_nano_sec make_gmt_ts(int year, int month, int day, int hour, int min, int sec, unsigned long nsec) {
    return gm_tm_nano_sec {
        .ansi_tm = {
            .tm_sec = sec,
            .tm_min = min,
            .tm_hour = hour,
            .tm_mday = day,
            .tm_mon = month - 1,
            .tm_year = year - 1900,
        },
        .nSec = nsec
    };
}

void testDiagMismatch(const char *ts_str, const gm_tm_nano_sec & expected_ts, const gm_tm_nano_sec & parsed_ts) {
    testDiag(
        "   Input: %s\n"
        "Expected: Y=%04d M=%02d D=%02d h=%02d m=%02d s=%02d off=%6ld ns=%09lu\n"
        "     got: Y=%04d M=%02d D=%02d h=%02d m=%02d s=%02d off=%6ld ns=%09lu",
        ts_str,
        expected_ts.ansi_tm.tm_year + 1900, expected_ts.ansi_tm.tm_mon + 1, expected_ts.ansi_tm.tm_mday,
        expected_ts.ansi_tm.tm_hour, expected_ts.ansi_tm.tm_min, expected_ts.ansi_tm.tm_sec,
        expected_ts.ansi_tm.tm_gmtoff, expected_ts.nSec,
        parsed_ts.ansi_tm.tm_year + 1900, parsed_ts.ansi_tm.tm_mon + 1, parsed_ts.ansi_tm.tm_mday,
        parsed_ts.ansi_tm.tm_hour, parsed_ts.ansi_tm.tm_min, parsed_ts.ansi_tm.tm_sec,
        parsed_ts.ansi_tm.tm_gmtoff, parsed_ts.nSec
    );
}

void testGoodTs(const char *ts_str, const gm_tm_nano_sec & expected_ts) {
    gm_tm_nano_sec parsed_ts = rfc3339::parseRfc3339Timestamp(ts_str);

    if (!testOk(rfc3339::equals(parsed_ts, expected_ts) , "(good) %s", ts_str))
        testDiagMismatch(ts_str, expected_ts, parsed_ts);
}

void testBadTs(const char *ts_str) {
    gm_tm_nano_sec parsed_ts = rfc3339::parseRfc3339Timestamp(ts_str);

    if (!testOk(rfc3339::equals(parsed_ts, rfc3339::ZERO) , "(bad)  %s", ts_str))
        testDiagMismatch(ts_str, rfc3339::ZERO, parsed_ts);
}

MAIN(rfc3339Test) {
    testPlan(12);

    //                                                         YYYY  MM  DD  hh  mm  ss  nsec
    testGoodTs("2025-01-23T12:34:56.789-04:00",    make_gmt_ts(2025,  1, 23, 16, 34, 56, 789000000));
    testGoodTs("2025-01-23t12:34:56.789-04:00",    make_gmt_ts(2025,  1, 23, 16, 34, 56, 789000000));
    testGoodTs("2025-01-23 12:34:56.789-04:00",    make_gmt_ts(2025,  1, 23, 16, 34, 56, 789000000));
    testGoodTs("2025-01-23 12:34:56.789+07:00",    make_gmt_ts(2025,  1, 23,  5, 34, 56, 789000000));
    testGoodTs("2025-01-23T12:34:56.789+05:30",    make_gmt_ts(2025,  1, 23,  7,  4, 56, 789000000));
    testGoodTs("2025-01-23 12:34:56.789Z",         make_gmt_ts(2025,  1, 23, 12, 34, 56, 789000000));
    testGoodTs("2025-01-23 12:34:56.789123Z",      make_gmt_ts(2025,  1, 23, 12, 34, 56, 789123000));
    testGoodTs("2025-01-23 12:34:56.789123456Z",   make_gmt_ts(2025,  1, 23, 12, 34, 56, 789123456));
    testGoodTs("2025-01-23 12:34:56Z",             make_gmt_ts(2025,  1, 23, 12, 34, 56, 0));
    testGoodTs("2025-01-23 01:23:45-06:00",        make_gmt_ts(2025,  1, 23,  7, 23, 45, 0));

    testBadTs("2025-01-23 12:34:56.78912345678Z");
    testBadTs("25-01-23T12:34:56.789-04:00");

    return testDone();
}
