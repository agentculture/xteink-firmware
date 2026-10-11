// Host tests for the once-a-day start routine (src/xteink/DailyRoutine.h): the
// 06:00 boundary under Asia/Jerusalem across the 2026 and 2027 DST transition
// days, the clock self-validation (fail closed), the boot gate, and the
// routine's step order over a fake port: every firmware branch still runs the
// book sync in the same start and reports the matching fw_outcome, an install
// reboots without writing last_run and cannot loop, and everything stays under
// the single wall-clock cap.
#include <ArduinoJson.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <regex>
#include <string>
#include <vector>

#include "xteink/DailyRoutine.h"
#include "xteink/SyncProtocol.h"

using namespace xteink;
using namespace xteink::daily;

namespace {

// UTC calendar -> epoch without any TZ involvement (days from civil).
int64_t utc(int y, const int m, const int d, const int hh = 0, const int mm = 0, const int ss = 0) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
  const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  return days * 86400 + hh * 3600 + mm * 60 + ss;
}

constexpr const char* kJerusalem = kDefaultRoutineTz;
constexpr int64_t kHour = 3600;

// Local wall time of `epoch` under kJerusalem: "YYYY-MM-DD HH:MM:SS".
std::string jerusalemLocal(const int64_t epoch) {
  const char* old = getenv("TZ");
  const std::string saved = old ? old : "";
  setenv("TZ", kJerusalem, 1);
  tzset();
  const auto t = static_cast<time_t>(epoch);
  struct tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  if (old) {
    setenv("TZ", saved.c_str(), 1);
  } else {
    unsetenv("TZ");
  }
  tzset();
  return buf;
}

bool fwOutcomeValid(const std::string& s) {
  static const std::regex re(R"(^(none|installed|failed:\S.*|deferred:\S.*)$)");
  return std::regex_match(s, re);
}

// --- fake device ---------------------------------------------------------------

class FakePort : public RoutinePort {
 public:
  // Inputs.
  bool key = true;
  int battery = 90;
  int64_t bootClock = 0;
  int64_t systemClock = 0;
  int64_t rtc = 0;
  int64_t floor = 0;
  int64_t sntp = 0;  // what syncClock() returns
  bool joinOk = true;
  OtaCheck ota;
  sync::Error installResult = sync::Error::None;
  sync::Outcome syncResult;
  std::string tz = kJerusalem;
  // Fake time each step costs.
  uint32_t joinCostMs = 3000;
  uint32_t checkCostMs = 1000;
  uint32_t installCostMs = 30000;
  uint32_t syncCostMs = 20000;
  // false: the join overruns its budget (a misbehaving radio).
  bool joinHonorsBudget = true;
  bool checkHonorsBudget = true;

  // State (NVS and radio).
  uint32_t now = 0xFFFFF000u;  // near the 49-day wrap on purpose
  bool connected = false;
  int64_t lastRunValue = 0;
  int64_t otaDayValue = 0;
  uint8_t otaCountValue = 0;
  Note savedNote = Note::None;
  bool rebooted = false;

  // Observations.
  std::vector<std::string> events;
  int joinCalls = 0;
  int syncCalls = 0;
  int installCalls = 0;
  int checkCalls = 0;
  int sntpCalls = 0;
  int setLastRunCalls = 0;
  uint32_t lastInstallBudget = 0;
  uint32_t lastCheckBudget = 0;
  uint32_t lastItemBudget = 0;
  uint32_t lastHardBudget = 0;
  uint32_t syncStartedAtElapsed = 0;
  uint32_t startMs = now;
  sync::RunFields syncFields;
  int64_t adopted = 0;

  uint32_t millis() override { return now; }
  bool provisioned() override { return key; }
  int batteryPercent() override { return battery; }
  int64_t bootClockEpoch() override { return bootClock; }
  int64_t systemClockEpoch() override { return systemClock; }
  int64_t rtcEpoch() override { return rtc; }
  int64_t timeFloor() override { return floor; }
  void adoptClock(const int64_t epoch) override {
    adopted = epoch;
    events.push_back("adopt");
  }
  int64_t syncClock(uint32_t budgetMs) override {
    ++sntpCalls;
    events.push_back("sntp");
    now += budgetMs < 500 ? budgetMs : 500;
    return sntp;
  }
  std::string timezone() override { return tz; }
  bool wifiConnected() override { return connected; }
  bool joinWifi(const uint32_t budgetMs) override {
    ++joinCalls;
    events.push_back("join");
    now += (joinCostMs < budgetMs || !joinHonorsBudget) ? joinCostMs : budgetMs;
    connected = joinOk;
    return joinOk;
  }
  void wifiOff() override {
    connected = false;
    events.push_back("wifi-off");
  }
  int64_t lastRun() override { return lastRunValue; }
  void setLastRun(const int64_t epoch) override {
    ++setLastRunCalls;
    lastRunValue = epoch;
    events.push_back("last_run");
  }
  int64_t otaDay() override { return otaDayValue; }
  void setOtaDay(const int64_t b) override { otaDayValue = b; }
  uint8_t otaCount() override { return otaCountValue; }
  void setOtaCount(const uint8_t n) override { otaCountValue = n; }
  Note note() override { return savedNote; }
  void setNote(const Note n) override { savedNote = n; }
  OtaCheck checkOta(const uint32_t budgetMs) override {
    ++checkCalls;
    lastCheckBudget = budgetMs;
    events.push_back("check");
    if (checkCostMs < budgetMs || !checkHonorsBudget) {
      now += checkCostMs;
      return ota;
    }
    now += budgetMs;  // the deadline cut the POST off: no answer
    return {sync::OtaDecision::None, sync::Error::Network};
  }
  sync::Error installOta(const uint32_t budgetMs) override {
    ++installCalls;
    lastInstallBudget = budgetMs;
    events.push_back("install");
    now += installCostMs < budgetMs ? installCostMs : budgetMs;
    return installResult;
  }
  void reboot() override {
    rebooted = true;
    events.push_back("reboot");
  }
  sync::Outcome runSync(const sync::RunFields& fields, const uint32_t itemBudgetMs,
                        const uint32_t hardBudgetMs) override {
    ++syncCalls;
    syncFields = fields;
    lastItemBudget = itemBudgetMs;
    lastHardBudget = hardBudgetMs;
    syncStartedAtElapsed = now - startMs;
    events.push_back("sync");
    now += syncCostMs < hardBudgetMs ? syncCostMs : hardBudgetMs;
    return syncResult;
  }

  // A trusted system clock at `epoch` (deep-sleep wake).
  void clockAt(const int64_t epoch) {
    bootClock = epoch;
    systemClock = epoch;
  }
  // The next boot: same NVS, fresh radio and observations.
  void nextBoot(const int64_t epoch) {
    clockAt(epoch);
    connected = false;
    rebooted = false;
    events.clear();
    joinCalls = syncCalls = installCalls = checkCalls = sntpCalls = setLastRunCalls = 0;
    startMs = now;
  }
};

FakePort dueDevice() {
  FakePort p;
  p.clockAt(utc(2026, 10, 11, 5, 0, 0));  // 08:00 IDT
  p.lastRunValue = utc(2026, 10, 10, 4, 0, 0);
  return p;
}

}  // namespace

// --- the 06:00 boundary on the DST transition days ---------------------------------

// The rule "IST-2IDT,M3.4.4/26,M10.5.0": DST starts on the day after the fourth
// Thursday of March at 02:00 (26:00 Thursday) and ends on the last Sunday of
// October at 02:00. 2026: Fri 03-27 and Sun 10-25. 2027: Fri 03-26 and Sun 10-31.
TEST(DailyBoundary, RuleTransitionDatesAreTheExpectedDays) {
  EXPECT_EQ(jerusalemLocal(utc(2026, 3, 26, 23, 59, 59)), "2026-03-27 01:59:59");
  EXPECT_EQ(jerusalemLocal(utc(2026, 3, 27, 0, 0, 0)), "2026-03-27 03:00:00");
  EXPECT_EQ(jerusalemLocal(utc(2026, 10, 24, 22, 59, 59)), "2026-10-25 01:59:59");
  EXPECT_EQ(jerusalemLocal(utc(2026, 10, 24, 23, 0, 0)), "2026-10-25 01:00:00");
  EXPECT_EQ(jerusalemLocal(utc(2027, 3, 25, 23, 59, 59)), "2027-03-26 01:59:59");
  EXPECT_EQ(jerusalemLocal(utc(2027, 3, 26, 0, 0, 0)), "2027-03-26 03:00:00");
  EXPECT_EQ(jerusalemLocal(utc(2027, 10, 30, 22, 59, 59)), "2027-10-31 01:59:59");
  EXPECT_EQ(jerusalemLocal(utc(2027, 10, 30, 23, 0, 0)), "2027-10-31 01:00:00");
}

struct DstDay {
  const char* name;
  int64_t boundary;      // 06:00 local on the transition day, in UTC
  int64_t previous;      // 06:00 local the day before
  int64_t hoursBetween;  // 23 in spring, 25 in autumn
};

class DailyDstTest : public ::testing::TestWithParam<DstDay> {};

TEST_P(DailyDstTest, BoundaryIsSixLocalOnTheTransitionDay) {
  const DstDay& d = GetParam();
  EXPECT_EQ(d.boundary - d.previous, d.hoursBetween * kHour) << d.name;
  EXPECT_EQ(jerusalemLocal(d.boundary).substr(11), "06:00:00") << d.name;
  EXPECT_EQ(jerusalemLocal(d.previous).substr(11), "06:00:00") << d.name;

  EXPECT_EQ(mostRecentBoundary(d.boundary, kJerusalem), d.boundary) << d.name;
  EXPECT_EQ(mostRecentBoundary(d.boundary - 1, kJerusalem), d.previous) << d.name;
  EXPECT_EQ(mostRecentBoundary(d.boundary + 12 * kHour, kJerusalem), d.boundary) << d.name;
  // 01:30 local, inside the night of the change: still the previous day.
  EXPECT_EQ(mostRecentBoundary(d.boundary - 4 * kHour - 30 * 60, kJerusalem), d.previous) << d.name;
}

TEST_P(DailyDstTest, DueFlipsExactlyAtSixLocal) {
  const DstDay& d = GetParam();
  const int64_t ranYesterday = d.previous + 30 * 60;  // 06:30 local the day before
  const int64_t justBefore = d.boundary - 1;
  EXPECT_FALSE(isDue(justBefore, ranYesterday, mostRecentBoundary(justBefore, kJerusalem))) << d.name;
  EXPECT_TRUE(isDue(d.boundary, ranYesterday, mostRecentBoundary(d.boundary, kJerusalem))) << d.name;
  // Ran this morning: not due again until tomorrow.
  const int64_t ranToday = d.boundary + 60;
  const int64_t evening = d.boundary + 15 * kHour;
  EXPECT_FALSE(isDue(evening, ranToday, mostRecentBoundary(evening, kJerusalem))) << d.name;
}

TEST_P(DailyDstTest, SecondStartOnTheSameLocalDayRunsNoRoutine) {
  const DstDay& d = GetParam();
  FakePort p;
  p.lastRunValue = d.previous + 60;
  p.clockAt(d.boundary + 30 * 60);  // 06:30 local
  const Result first = run(p, Trigger::PowerOn);
  ASSERT_EQ(first.status, Status::Ran) << d.name;
  EXPECT_EQ(first.boundary, d.boundary) << d.name;
  EXPECT_EQ(p.syncCalls, 1);

  p.nextBoot(d.boundary + 14 * kHour);  // 20:00 local, same day
  const Result second = run(p, Trigger::Wake);
  EXPECT_EQ(second.status, Status::NotDue) << d.name;
  EXPECT_EQ(p.syncCalls, 0) << d.name;
  EXPECT_EQ(p.joinCalls, 0) << d.name;
  EXPECT_EQ(p.checkCalls, 0) << d.name;
  EXPECT_EQ(p.setLastRunCalls, 0) << d.name;

  // Just before the next 06:00 local: still the same routine-day.
  const int64_t nextBoundary = mostRecentBoundary(d.boundary + 30 * kHour, kJerusalem);
  ASSERT_GT(nextBoundary, d.boundary);
  p.nextBoot(nextBoundary - 1);
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::NotDue) << d.name;
  p.nextBoot(nextBoundary);
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Ran) << d.name;
}

INSTANTIATE_TEST_SUITE_P(Jerusalem, DailyDstTest,
                         ::testing::Values(
                             // 06:00 IDT = 03:00Z; the day before 06:00 IST = 04:00Z.
                             DstDay{"2026-03-27", utc(2026, 3, 27, 3), utc(2026, 3, 26, 4), 23},
                             DstDay{"2026-10-25", utc(2026, 10, 25, 4), utc(2026, 10, 24, 3), 25},
                             DstDay{"2027-03-26", utc(2027, 3, 26, 3), utc(2027, 3, 25, 4), 23},
                             DstDay{"2027-10-31", utc(2027, 10, 31, 4), utc(2027, 10, 30, 3), 25}),
                         [](const auto& info) {
                           std::string n = info.param.name;
                           for (char& c : n) {
                             if (c == '-') c = '_';
                           }
                           return "d" + n;
                         });

TEST(DailyBoundary, NeverTouchesTheProcessTimezone) {
  // The render task formats the clock under TZ while setup() runs the routine.
  setenv("TZ", "UTC0", 1);
  tzset();
  mostRecentBoundary(utc(2026, 10, 11, 12), kJerusalem);
  ASSERT_NE(getenv("TZ"), nullptr);
  EXPECT_STREQ(getenv("TZ"), "UTC0");
  unsetenv("TZ");
  tzset();
  mostRecentBoundary(utc(2026, 10, 11, 12), kJerusalem);
  EXPECT_EQ(getenv("TZ"), nullptr);
}

// Every rule the clock settings ship (src/util/Timezones.cpp), plus forms the
// C library accepts that the table does not use.
const char* const kShippedZones[] = {
    "ACST-9:30ACDT,M10.1.0,M4.1.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3", "AKST9AKDT,M3.2.0,M11.1.0",
    "AST4ADT,M3.2.0,M11.1.0",         "AZOT1AZOST,M3.5.0/0,M10.5.0/1", "CET-1CEST,M3.5.0,M10.5.0/3",
    "CLT4CLST,M9.1.6/24,M4.1.6/24",   "CST6CDT,M3.2.0,M11.1.0",        "EET-2EEST,M3.5.0/3,M10.5.0/4",
    "EET-2EEST,M4.5.5/0,M10.5.4/24",  "EST5EDT,M3.2.0,M11.1.0",        "GMT0BST,M3.5.0/1,M10.5.0",
    "IST-2IDT,M3.4.4/26,M10.5.0",     "MST7MDT,M3.2.0,M11.1.0",        "NST3:30NDT,M3.2.0,M11.1.0",
    "NZST-12NZDT,M9.5.0,M4.1.0/3",    "PST8PDT,M3.2.0,M11.1.0",        "ACST-9:30",
    "AFT-4:30",                       "IST-5:30",                      "NPT-5:45",
    "LINT-14",                        "HST10",                         "UTC0",
    "UTC+05:30",                      "UTC-05:45",                     "UTC-0:30",
    "<+03>-3",                        "EST5EDT",                       "XST-1XDT-3,J60/1,300/-1",
};

int32_t libcOffset(const char* tz, const int64_t epoch) {
  setenv("TZ", tz, 1);
  tzset();
  const auto t = static_cast<time_t>(epoch);
  struct tm tm{};
  localtime_r(&t, &tm);
  return static_cast<int32_t>(tm.tm_gmtoff);
}

// The old implementation: mktime() under the rule.
int64_t libcBoundary(const char* tz, const int64_t nowUtc, const int hour) {
  setenv("TZ", tz, 1);
  tzset();
  const auto at = [](const time_t t, const int dayOffset, const int h) {
    struct tm tm{};
    localtime_r(&t, &tm);
    tm.tm_mday += dayOffset;
    tm.tm_hour = h;
    tm.tm_min = tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return static_cast<int64_t>(mktime(&tm));
  };
  int64_t b = at(static_cast<time_t>(nowUtc), 0, hour);
  if (b > nowUtc) b = at(static_cast<time_t>(nowUtc), -1, hour);
  return b;
}

TEST(DailyBoundary, OffsetsMatchTheCLibraryForEveryShippedZone) {
  for (const char* tz : kShippedZones) {
    int failures = 0;
    for (int64_t t = utc(2025, 12, 1); t < utc(2028, 2, 1) && failures < 3; t += 15 * 60) {
      int32_t mine = 0;
      ASSERT_TRUE(utcOffsetAt(tz, t, mine)) << tz;
      const int32_t want = libcOffset(tz, t);
      if (mine != want) {
        ++failures;
        ADD_FAILURE() << tz << " at " << t << ": " << mine << " != " << want;
      }
    }
  }
  unsetenv("TZ");
  tzset();
}

TEST(DailyBoundary, BoundaryMatchesTheCLibraryForEveryShippedZone) {
  for (const char* tz : kShippedZones) {
    int failures = 0;
    for (int64_t t = utc(2026, 1, 1, 0, 7); t < utc(2028, 1, 1) && failures < 3; t += 5 * kHour + 13 * 60) {
      const int64_t mine = mostRecentBoundary(t, tz);
      const int64_t want = libcBoundary(tz, t, kRunHourLocal);
      if (mine != want) {
        ++failures;
        ADD_FAILURE() << tz << " at " << t << ": " << mine << " != " << want;
      }
    }
  }
  unsetenv("TZ");
  tzset();
}

TEST(DailyBoundary, RulesThatDoNotParseAreRejected) {
  int32_t off = 0;
  for (const char* tz : {"", "X", "IST-2IDT,M3.4.4", "IST-2IDT,M13.1.0,M10.5.0", "IST-99", "CET-1CEST,M3.5.0,M10.5.0/3x",
                         "<+03-3"}) {
    EXPECT_FALSE(utcOffsetAt(tz, utc(2026, 6, 1), off)) << tz;
    EXPECT_EQ(mostRecentBoundary(utc(2026, 6, 1), tz), -1) << tz;
  }
  EXPECT_FALSE(utcOffsetAt(nullptr, utc(2026, 6, 1), off));
}

TEST(DailyBoundary, RejectsUnusableInput) {
  EXPECT_EQ(mostRecentBoundary(0, kJerusalem), -1);
  EXPECT_EQ(mostRecentBoundary(utc(2026, 1, 1), nullptr), -1);
  EXPECT_EQ(mostRecentBoundary(utc(2026, 1, 1), ""), -1);
}

TEST(DailyBoundary, NeverRanAndCorruptLastRunAreDue) {
  const int64_t now = utc(2026, 10, 11, 12);
  const int64_t b = mostRecentBoundary(now, kJerusalem);
  EXPECT_TRUE(isDue(now, 0, b));
  EXPECT_TRUE(isDue(now, now + 30 * 86400, b));
  EXPECT_FALSE(isDue(now, now, b));
}

TEST(DailyBoundary, TimezoneDefaultsToJerusalemWhenNeverChosen) {
  EXPECT_STREQ(routineTimezone(false, "UTC0"), kDefaultRoutineTz);
  EXPECT_STREQ(routineTimezone(true, nullptr), kDefaultRoutineTz);
  EXPECT_STREQ(routineTimezone(true, ""), kDefaultRoutineTz);
  EXPECT_STREQ(routineTimezone(true, "CET-1CEST,M3.5.0,M10.5.0/3"), "CET-1CEST,M3.5.0,M10.5.0/3");
  // Berlin's 06:00 on 2026-10-11 (CEST, +2) is 04:00Z.
  EXPECT_EQ(mostRecentBoundary(utc(2026, 10, 11, 12), "CET-1CEST,M3.5.0,M10.5.0/3"), utc(2026, 10, 11, 4));
}

TEST(DailyFormat, IsoUtc) { EXPECT_EQ(isoUtc(utc(2026, 3, 27, 3, 0, 5)), "2026-03-27T03:00:05Z"); }

// --- boot gate -------------------------------------------------------------------------

TEST(DailyGate, OnlyPowerButtonAndPlainBoots) {
  EXPECT_TRUE(bootQualifies(BootKind::PowerButton, false, false, false));
  EXPECT_TRUE(bootQualifies(BootKind::Other, false, false, false));
  EXPECT_FALSE(bootQualifies(BootKind::AfterUSBPower, false, false, false));
  EXPECT_FALSE(bootQualifies(BootKind::AfterFlash, false, false, false));
  EXPECT_FALSE(bootQualifies(BootKind::Other, /*silent=*/true, false, false));
  EXPECT_FALSE(bootQualifies(BootKind::Other, false, /*panic=*/true, false));
  EXPECT_FALSE(bootQualifies(BootKind::PowerButton, false, false, /*recovery=*/true));
  EXPECT_STREQ(triggerName(Trigger::PowerOn), "power_on");
  EXPECT_STREQ(triggerName(Trigger::Wake), "wake");
}

TEST(DailyGate, NotProvisionedDoesNothing) {
  FakePort p = dueDevice();
  p.key = false;
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::SkippedNotProvisioned);
  EXPECT_TRUE(p.events.empty());
}

// --- clock self-validation -------------------------------------------------------------

TEST(DailyClock, Plausibility) {
  const int64_t floor = utc(2026, 10, 1);
  EXPECT_FALSE(clockPlausible(0, 0));
  EXPECT_FALSE(clockPlausible(86400, 0));              // near epoch 0
  EXPECT_FALSE(clockPlausible(utc(2024, 12, 31), 0));  // before 2025-01-01
  EXPECT_TRUE(clockPlausible(utc(2025, 1, 1), 0));     // the min itself
  EXPECT_TRUE(clockPlausible(floor, floor));           // at the floor
  EXPECT_TRUE(clockPlausible(floor + 1, floor));       // past it
  EXPECT_FALSE(clockPlausible(floor - 1, floor));      // behind what was seen
}

TEST(DailyClock, PrefersTheRunningSystemClockThenTheRtc) {
  const int64_t floor = utc(2026, 10, 1);
  const int64_t t = utc(2026, 10, 11, 5);
  ClockChoice c = chooseLocalClock(t, t + 3, 0, floor);
  EXPECT_EQ(c.source, ClockSource::System);
  EXPECT_EQ(c.epoch, t + 3);
  // Cold boot: the system clock started near 0 (TrustedTime then raised it to
  // the floor, which says nothing about today); the RTC kept time.
  c = chooseLocalClock(5, floor, t, floor);
  EXPECT_EQ(c.source, ClockSource::Rtc);
  EXPECT_EQ(c.epoch, t);
  // An RTC behind the floor is not trusted.
  c = chooseLocalClock(5, floor, floor - 100, floor);
  EXPECT_EQ(c.source, ClockSource::None);
  // No RTC at all.
  c = chooseLocalClock(5, floor, 0, floor);
  EXPECT_EQ(c.source, ClockSource::None);
}

TEST(DailyClock, RtcIsAdoptedIntoTheSystemClock) {
  FakePort p = dueDevice();
  p.bootClock = 0;
  p.systemClock = utc(2026, 10, 1);  // the restored floor
  p.floor = utc(2026, 10, 1);
  p.rtc = utc(2026, 10, 11, 5);
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.clock, ClockSource::Rtc);
  EXPECT_EQ(p.adopted, p.rtc);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(p.sntpCalls, 0);
}

TEST(DailyClock, UntrustedClockJoinsAndUsesSntpBeforeDeciding) {
  FakePort p = dueDevice();
  p.bootClock = 0;
  p.systemClock = utc(2026, 10, 1);
  p.floor = utc(2026, 10, 1);
  p.sntp = utc(2026, 10, 11, 5);
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.clock, ClockSource::Network);
  EXPECT_EQ(r.now, p.sntp);
  EXPECT_EQ(r.status, Status::Ran);
  ASSERT_GE(p.events.size(), 3u);
  EXPECT_EQ(p.events[0], "join");
  EXPECT_EQ(p.events[1], "sntp");
  EXPECT_EQ(p.joinCalls, 1);  // the routine reuses the join
}

TEST(DailyClock, NoTimeMeansNoRoutineAndNoLastRun) {
  FakePort p = dueDevice();
  p.bootClock = 0;
  p.systemClock = 0;
  p.sntp = 0;  // SNTP failed
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::SkippedNoClock);
  EXPECT_EQ(p.syncCalls, 0);
  EXPECT_EQ(p.checkCalls, 0);
  EXPECT_EQ(p.setLastRunCalls, 0);
  EXPECT_FALSE(p.connected);  // radio released

  // The join itself fails: same outcome.
  FakePort q = dueDevice();
  q.bootClock = 0;
  q.joinOk = false;
  EXPECT_EQ(run(q, Trigger::PowerOn).status, Status::SkippedNoClock);
  EXPECT_EQ(q.sntpCalls, 0);
  EXPECT_EQ(q.setLastRunCalls, 0);

  // A bogus SNTP answer (near epoch 0) is not trusted either.
  FakePort s = dueDevice();
  s.bootClock = 0;
  s.sntp = 1000;
  EXPECT_EQ(run(s, Trigger::PowerOn).status, Status::SkippedNoClock);
  EXPECT_EQ(s.setLastRunCalls, 0);
}

TEST(DailyClock, LowBatteryWithoutClockDoesNotJoin) {
  FakePort p = dueDevice();
  p.bootClock = 0;
  p.battery = kDailyJoinBatteryFloor - 1;
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::SkippedNoClock);
  EXPECT_EQ(p.joinCalls, 0);
}

// --- battery ---------------------------------------------------------------------------------

TEST(DailyBattery, BelowTheJoinFloorSkipsEverything) {
  FakePort p = dueDevice();
  p.battery = kDailyJoinBatteryFloor - 1;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::SkippedLowBattery);
  EXPECT_EQ(p.joinCalls, 0);
  EXPECT_EQ(p.syncCalls, 0);
  EXPECT_EQ(p.setLastRunCalls, 0);  // a charged start later today still runs
  EXPECT_EQ(p.savedNote, Note::LowBattery);

  // Charged later the same day: it runs and the note is replaced.
  p.nextBoot(p.systemClock + 2 * kHour);
  p.battery = 80;
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Ran);
  EXPECT_EQ(p.savedNote, Note::None);
}

TEST(DailyBattery, FloorsAreOrdered) {
  static_assert(kDailyOtaBatteryFloor > kDailyJoinBatteryFloor, "OTA needs more headroom than a join");
  EXPECT_EQ(kDailyJoinBatteryFloor, 20);
}

// --- the five firmware branches: the sync always runs ------------------------------

struct FwBranch {
  const char* name;
  OtaCheck check;
  int battery;
  sync::Error install;
  const char* fwOutcome;
  int installs;
  Note note;
};

class DailyFirmwareBranchTest : public ::testing::TestWithParam<FwBranch> {};

TEST_P(DailyFirmwareBranchTest, SyncStillRunsInTheSameStartWithTheMatchingOutcome) {
  const FwBranch& b = GetParam();
  FakePort p = dueDevice();
  p.ota = b.check;
  p.battery = b.battery;
  p.installResult = b.install;
  const Result r = run(p, Trigger::Wake);

  EXPECT_EQ(r.status, Status::Ran) << b.name;
  EXPECT_FALSE(p.rebooted) << b.name;
  EXPECT_EQ(p.syncCalls, 1) << b.name;
  EXPECT_EQ(p.installCalls, b.installs) << b.name;
  EXPECT_EQ(p.syncFields.fwOutcome, b.fwOutcome) << b.name;
  EXPECT_EQ(r.fields.fwOutcome, b.fwOutcome) << b.name;
  EXPECT_TRUE(fwOutcomeValid(r.fields.fwOutcome)) << b.name;
  EXPECT_EQ(p.savedNote, b.note) << b.name;
  EXPECT_EQ(p.setLastRunCalls, 1) << b.name;

  // Order: firmware first, then the books, then last_run.
  std::vector<std::string> order;
  for (const auto& e : p.events) {
    if (e == "check" || e == "install" || e == "sync" || e == "last_run") order.push_back(e);
  }
  std::vector<std::string> want{"check"};
  if (b.installs) want.push_back("install");
  want.push_back("sync");
  want.push_back("last_run");
  EXPECT_EQ(order, want) << b.name;

  // The status fields the sync's reports carry.
  sync::StatusReport report;
  report.firmwareVersion = "xteink-fw 1.0.0";
  report.run = p.syncFields;
  report.run.contentOutcome = r.fields.contentOutcome;
  const std::string json = sync::buildStatusJson(report);
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, json)) << json;
  EXPECT_EQ(std::string(doc["fw_outcome"] | ""), b.fwOutcome) << b.name;
  EXPECT_EQ(std::string(doc["run_trigger"] | ""), "wake") << b.name;
  EXPECT_EQ(std::string(doc["run_started_at"] | ""), "2026-10-11T05:00:00Z") << b.name;
  EXPECT_EQ(std::string(doc["content_outcome"] | ""), "ok") << b.name;
}

INSTANTIATE_TEST_SUITE_P(
    Branches, DailyFirmwareBranchTest,
    ::testing::Values(
        FwBranch{"none", {sync::OtaDecision::None, sync::Error::None}, 90, sync::Error::None, "none", 0, Note::None},
        FwBranch{
            "current", {sync::OtaDecision::Current, sync::Error::None}, 90, sync::Error::None, "none", 0, Note::None},
        FwBranch{"low_battery",
                 {sync::OtaDecision::Install, sync::Error::None},
                 kDailyOtaBatteryFloor - 1,
                 sync::Error::None,
                 "deferred:battery",
                 0,
                 Note::FirmwarePending},
        FwBranch{"download_error",
                 {sync::OtaDecision::Install, sync::Error::None},
                 90,
                 sync::Error::Download,
                 "failed:EDL",
                 1,
                 Note::FirmwarePending},
        FwBranch{"sha_mismatch",
                 {sync::OtaDecision::Install, sync::Error::None},
                 90,
                 sync::Error::Hash,
                 "failed:ESHA",
                 1,
                 Note::FirmwarePending},
        // Needs a human: the offer is only reachable over the tunnel.
        FwBranch{"needs_human_tunnel",
                 {sync::OtaDecision::Deferred, sync::Error::None},
                 90,
                 sync::Error::None,
                 "deferred:tunnel",
                 0,
                 Note::FirmwarePending},
        // The offer names a build this device rolled back from: never again.
        FwBranch{"rolled_back",
                 {sync::OtaDecision::RolledBack, sync::Error::None},
                 90,
                 sync::Error::None,
                 "deferred:rolled_back",
                 0,
                 Note::FirmwarePending},
        FwBranch{"check_failed",
                 {sync::OtaDecision::None, sync::Error::Network},
                 90,
                 sync::Error::None,
                 "failed:ENET",
                 0,
                 Note::None}),
    [](const auto& info) { return std::string(info.param.name); });

// --- the Home note follows the offer -------------------------------------------------

TEST(DailyNote, AFailedCheckKeepsAnEarlierPendingNote) {
  // Yesterday the offer was deferred; today the check itself fails. The offer
  // may still be outstanding, so Home keeps "Update pending".
  FakePort p = dueDevice();
  p.savedNote = Note::FirmwarePending;
  p.ota = {sync::OtaDecision::None, sync::Error::Network};
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(r.fields.fwOutcome, "failed:ENET");
  EXPECT_EQ(p.savedNote, Note::FirmwarePending);
}

TEST(DailyNote, AFailedCheckClearsAStaleLowBatteryNote) {
  FakePort p = dueDevice();
  p.savedNote = Note::LowBattery;
  p.ota = {sync::OtaDecision::None, sync::Error::Network};
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Ran);
  EXPECT_EQ(p.savedNote, Note::None);
}

TEST(DailyNote, AFailedInstallLeavesTheOfferPending) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  p.installResult = sync::Error::Download;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.fields.fwOutcome, "failed:EDL");
  EXPECT_EQ(p.savedNote, Note::FirmwarePending);
}

TEST(DailyNote, NoTimeForTheCheckKeepsAPendingNote) {
  FakePort p = dueDevice();
  p.savedNote = Note::FirmwarePending;
  p.joinHonorsBudget = false;
  p.joinCostMs = kDailyRoutineCapMs - kSyncReserveMs;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(p.checkCalls, 0);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:time");
  EXPECT_EQ(p.savedNote, Note::FirmwarePending);
}

TEST(DailyNote, AnAnswerWithoutAnOfferClearsThePendingNote) {
  FakePort p = dueDevice();
  p.savedNote = Note::FirmwarePending;
  p.ota = {sync::OtaDecision::None, sync::Error::None};
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Ran);
  EXPECT_EQ(p.savedNote, Note::None);
}

// --- install, reboot, health check, no loop ----------------------------------------------

TEST(DailyOta, InstallRebootsWithoutLastRunThenReportsInstalled) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Rebooting);
  EXPECT_TRUE(p.rebooted);
  EXPECT_EQ(p.installCalls, 1);
  EXPECT_EQ(p.syncCalls, 0);
  EXPECT_EQ(p.setLastRunCalls, 0);  // written only once the new image is up
  EXPECT_EQ(p.otaCountValue, 1);
  EXPECT_FALSE(p.connected);

  // New image: its health check passed (else it would have rolled back); the
  // server now names the running build.
  p.nextBoot(p.systemClock + 60);
  p.ota = {sync::OtaDecision::Current, sync::Error::None};
  r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(r.fields.fwOutcome, "installed");
  EXPECT_EQ(p.syncFields.fwOutcome, "installed");
  EXPECT_EQ(p.installCalls, 0);
  EXPECT_EQ(p.syncCalls, 1);
  EXPECT_EQ(p.setLastRunCalls, 1);

  p.nextBoot(p.systemClock + 60);
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::NotDue);
}

TEST(DailyOta, AStationLeftUpByThePassedHealthCheckIsReusedThenReleased) {
  // The new image's health check joined Wi-Fi and kept it up for the routine:
  // no second join, and the routine still turns the radio off at the end.
  FakePort p = dueDevice();
  p.connected = true;
  p.otaCountValue = 1;
  p.otaDayValue = mostRecentBoundary(p.systemClock, kJerusalem);
  p.ota = {sync::OtaDecision::Current, sync::Error::None};
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(r.fields.fwOutcome, "installed");
  EXPECT_EQ(p.joinCalls, 0);
  EXPECT_FALSE(p.connected);
}

TEST(DailyOta, AnOfferThatKeepsComingBackCannotLoop) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  ASSERT_EQ(run(p, Trigger::PowerOn).status, Status::Rebooting);

  // The build did not change the reported version (or rolled back): the
  // server still offers it. No second install today; the books still sync.
  p.nextBoot(p.systemClock + 60);
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(p.installCalls, 0);
  EXPECT_FALSE(p.rebooted);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:daily_cap");
  EXPECT_EQ(p.savedNote, Note::FirmwarePending);
  EXPECT_EQ(p.syncCalls, 1);

  // Next routine-day it may try once more.
  p.nextBoot(p.systemClock + 24 * kHour);
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Rebooting);
  EXPECT_EQ(p.installCalls, 1);
}

// --- the single wall-clock cap ---------------------------------------------------------

TEST(DailyCap, EveryStepFitsInsideTheCap) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  p.installResult = sync::Error::Download;
  p.installCostMs = 1000000;  // would run forever: bounded by its budget
  p.syncCostMs = 1000000;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_LE(p.now - p.startMs, kDailyRoutineCapMs);
  EXPECT_LE(p.syncStartedAtElapsed + p.lastHardBudget, kDailyRoutineCapMs);
  EXPECT_GE(p.lastHardBudget, kSyncReserveMs - 1000);  // the install left the sync its reserve
  EXPECT_LT(p.lastItemBudget, p.lastHardBudget);
}

TEST(DailyCap, NoTimeForAnInstallDefersItAndStillSyncs) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  p.checkCostMs = kDailyRoutineCapMs - kSyncReserveMs - kOtaMinInstallMs;  // a slow server
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(p.installCalls, 0);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:time");
  EXPECT_EQ(p.syncCalls, 1);
}

TEST(DailyCap, AnInstallStartsOnlyWithARealisticBudget) {
  // 20 s used to be enough to start: esp_ota_begin then erased the slot and the
  // transfer was cancelled long before 6 MB arrived.
  static_assert(kOtaMinInstallMs >= 60000, "a 6 MB LAN install plus one stall needs >= 60 s");
  static_assert(kDailyRoutineCapMs - kSyncReserveMs - kJoinBudgetMs / 4 >= kOtaMinInstallMs,
                "a typical join and check must leave room for an install");
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  p.joinCostMs = 3000;
  p.checkCostMs = 1000;
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::Rebooting);
  EXPECT_GE(p.lastInstallBudget, kOtaMinInstallMs);

  // A slow join leaves less than the minimum: deferred, the day's attempt kept.
  FakePort slow = dueDevice();
  slow.ota = {sync::OtaDecision::Install, sync::Error::None};
  slow.joinCostMs = 12000;
  const Result r = run(slow, Trigger::PowerOn);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:time");
  EXPECT_EQ(slow.installCalls, 0);
  EXPECT_EQ(slow.otaCountValue, 0);
}

TEST(DailyCap, ABudgetCancelledInstallDoesNotUseUpTheDay) {
  FakePort p = dueDevice();
  p.ota = {sync::OtaDecision::Install, sync::Error::None};
  p.installResult = sync::Error::Cancelled;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(p.installCalls, 1);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:time");
  EXPECT_EQ(p.savedNote, Note::FirmwarePending);
  EXPECT_EQ(p.otaCountValue, 0);
  EXPECT_EQ(p.syncCalls, 1);

  // A real failure still counts (a crash mid-install must not loop).
  FakePort q = dueDevice();
  q.ota = {sync::OtaDecision::Install, sync::Error::None};
  q.installResult = sync::Error::Hash;
  run(q, Trigger::PowerOn);
  EXPECT_EQ(q.otaCountValue, 1);
}

TEST(DailyCap, NoTimeForTheCheckSkipsItAndStillSyncs) {
  FakePort p = dueDevice();
  p.joinHonorsBudget = false;
  p.joinCostMs = kDailyRoutineCapMs - kSyncReserveMs;
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_EQ(p.checkCalls, 0);
  EXPECT_EQ(r.fields.fwOutcome, "deferred:time");
  EXPECT_EQ(p.syncCalls, 1);
}

TEST(DailyCap, TheCheckIsBoundedByTheCap) {
  // A tunnel status POST with no deadline could hold the routine past its cap.
  FakePort p = dueDevice();
  p.checkCostMs = 1000000;  // a peer that never answers
  const Result r = run(p, Trigger::PowerOn);
  EXPECT_EQ(r.status, Status::Ran);
  EXPECT_GT(p.lastCheckBudget, 0u);
  EXPECT_LE(p.lastCheckBudget, kDailyRoutineCapMs - kSyncReserveMs);
  EXPECT_EQ(r.fields.fwOutcome, "failed:ENET");
  EXPECT_GE(p.lastHardBudget, kSyncReserveMs - 1000);  // the sync kept its reserve
  EXPECT_LE(p.now - p.startMs, kDailyRoutineCapMs);
}

TEST(DailyCap, JoinIsBoundedByItsBudget) {
  FakePort p = dueDevice();
  p.joinOk = false;
  p.joinCostMs = 1000000;
  EXPECT_EQ(run(p, Trigger::PowerOn).status, Status::SkippedNoWifi);
  EXPECT_LE(p.now - p.startMs, kJoinBudgetMs);
  EXPECT_EQ(p.setLastRunCalls, 0);
}

// --- outcome strings and the status line ----------------------------------------------

TEST(DailyFormat, ContentOutcome) {
  sync::Outcome o;
  EXPECT_EQ(contentOutcome(o), "ok");
  o.error = sync::Error::Network;
  EXPECT_EQ(contentOutcome(o), "error:ENET");
  o.error = sync::Error::Hash;
  o.delivered = 2;
  EXPECT_EQ(contentOutcome(o), "partial:ESHA");
  o.error = sync::Error::Cancelled;
  EXPECT_EQ(contentOutcome(o), "cancelled");
}

TEST(DailyFormat, FwOutcomeStringsMatchTheServerPattern) {
  for (const char* s :
       {kFwNone, kFwInstalled, kFwDeferredTunnel, kFwDeferredBattery, kFwDeferredDailyCap, kFwDeferredTime,
        kFwDeferredRolledBack, sync::otaDecisionName(sync::OtaDecision::RolledBack)}) {
    EXPECT_TRUE(fwOutcomeValid(s)) << s;
  }
  for (const auto e : {sync::Error::Network, sync::Error::Download, sync::Error::Hash, sync::Error::Flash,
                       sync::Error::WrongDevice, sync::Error::Cancelled, sync::Error::LowMemory}) {
    EXPECT_TRUE(fwOutcomeValid(fwFailed(e))) << fwFailed(e);
  }
}

TEST(DailyFormat, RunFieldsAreOmittedWhenEmpty) {
  sync::StatusReport report;
  report.firmwareVersion = "xteink-fw 1.0.0";
  const std::string json = sync::buildStatusJson(report);
  for (const char* k : {"run_started_at", "run_trigger", "fw_outcome", "content_outcome"}) {
    EXPECT_EQ(json.find(k), std::string::npos) << k << " in " << json;
  }
}

TEST(DailyFormat, StatusLineComposition) {
  char buf[64];
  EXPECT_TRUE(composeStatusLine("Synced 06:01", "Update pending", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced 06:01 \xC2\xB7 Update pending");
  EXPECT_TRUE(composeStatusLine(nullptr, "Update pending", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Update pending");
  EXPECT_TRUE(composeStatusLine("Synced", nullptr, buf, sizeof(buf)));
  EXPECT_STREQ(buf, "Synced");
  EXPECT_FALSE(composeStatusLine(nullptr, "", buf, sizeof(buf)));
  EXPECT_STREQ(buf, "");
  char tiny[8];
  EXPECT_TRUE(composeStatusLine("Synced 06:01", "x", tiny, sizeof(tiny)));
  EXPECT_EQ(std::string(tiny).size(), 7u);
  EXPECT_EQ(noteFromByte(1), Note::FirmwarePending);
  EXPECT_EQ(noteFromByte(2), Note::LowBattery);
  EXPECT_EQ(noteFromByte(77), Note::None);
}
