#include "DailyRoutine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace xteink::daily {

// --- pure helpers ------------------------------------------------------------

bool bootQualifies(const BootKind kind, const bool silentReboot, const bool panicReboot, const bool recoveryMode) {
  if (silentReboot || panicReboot || recoveryMode) return false;
  return kind == BootKind::PowerButton || kind == BootKind::Other;
}

const char* triggerName(const Trigger t) { return t == Trigger::Wake ? "wake" : "power_on"; }

const char* routineTimezone(const bool timezoneChosen, const char* clockTz) {
  if (!timezoneChosen || clockTz == nullptr || clockTz[0] == '\0') return kDefaultRoutineTz;
  return clockTz;
}

namespace {

// Applies a POSIX TZ rule for the lifetime of the object and restores the
// previous one (or its absence) afterwards.
class ScopedTz {
 public:
  explicit ScopedTz(const char* tz) {
    const char* old = getenv("TZ");
    hadOld = old != nullptr;
    if (hadOld) snprintf(oldTz, sizeof(oldTz), "%s", old);
    setenv("TZ", tz, 1);
    tzset();
  }
  ~ScopedTz() {
    if (hadOld) {
      setenv("TZ", oldTz, 1);
    } else {
      unsetenv("TZ");
    }
    tzset();
  }
  ScopedTz(const ScopedTz&) = delete;
  ScopedTz& operator=(const ScopedTz&) = delete;

 private:
  bool hadOld = false;
  char oldTz[64] = "";
};

// Local `hour`:00:00 on the calendar day of `t`, `dayOffset` days later, or -1.
int64_t localHourOn(const time_t t, const int dayOffset, const int hour) {
  struct tm tm{};
  if (localtime_r(&t, &tm) == nullptr) return -1;
  tm.tm_mday += dayOffset;
  tm.tm_hour = hour;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_isdst = -1;  // let the rule decide (06:00 exists exactly once on every DST day)
  const time_t b = mktime(&tm);
  return b == static_cast<time_t>(-1) ? -1 : static_cast<int64_t>(b);
}

}  // namespace

int64_t mostRecentBoundary(const int64_t nowUtc, const char* posixTz, const int hour) {
  if (nowUtc <= 0 || posixTz == nullptr || posixTz[0] == '\0' || hour < 0 || hour > 23) return -1;
  ScopedTz scope(posixTz);
  const auto now = static_cast<time_t>(nowUtc);
  int64_t b = localHourOn(now, 0, hour);
  if (b > nowUtc) b = localHourOn(now, -1, hour);
  return b;
}

bool isDue(const int64_t nowUtc, const int64_t lastRun, const int64_t boundary) {
  if (lastRun <= 0) return true;
  if (lastRun > nowUtc + kLastRunFutureSlackSecs) return true;  // corrupt value
  return lastRun < boundary;
}

std::string isoUtc(const int64_t epoch) {
  const auto t = static_cast<time_t>(epoch);
  struct tm tm{};
  if (gmtime_r(&t, &tm) == nullptr) return "";
  char buf[24];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) return "";
  return buf;
}

// --- clock -------------------------------------------------------------------

const char* clockSourceName(const ClockSource s) {
  switch (s) {
    case ClockSource::None:
      return "none";
    case ClockSource::System:
      return "system";
    case ClockSource::Rtc:
      return "rtc";
    case ClockSource::Network:
      return "sntp";
  }
  return "none";
}

bool clockPlausible(const int64_t epoch, const int64_t floor) { return epoch >= kMinPlausibleEpoch && epoch >= floor; }

ClockChoice chooseLocalClock(const int64_t bootSystemEpoch, const int64_t systemNowEpoch, const int64_t rtcEpoch,
                             const int64_t floor) {
  // The boot reading is the system clock before TrustedTime moved it up to
  // the floor; the clock now is that reading plus the time since boot.
  if (clockPlausible(bootSystemEpoch, floor) && systemNowEpoch >= bootSystemEpoch) {
    return {ClockSource::System, systemNowEpoch};
  }
  if (rtcEpoch > 0 && clockPlausible(rtcEpoch, floor)) return {ClockSource::Rtc, rtcEpoch};
  return {};
}

// --- outcome strings -------------------------------------------------------------

std::string contentOutcome(const sync::Outcome& out) {
  if (out.error == sync::Error::None) return "ok";
  if (out.error == sync::Error::Cancelled) return "cancelled";
  const std::string code = sync::errorCode(out.error);
  return (out.delivered > 0 ? "partial:" : "error:") + code;
}

std::string fwFailed(const sync::Error e) {
  const char* code = sync::errorCode(e);
  return std::string("failed:") + (code && code[0] ? code : "EUNKNOWN");
}

Note noteFromByte(const uint8_t b) {
  switch (b) {
    case static_cast<uint8_t>(Note::FirmwarePending):
      return Note::FirmwarePending;
    case static_cast<uint8_t>(Note::LowBattery):
      return Note::LowBattery;
    default:
      return Note::None;
  }
}

bool composeStatusLine(const char* syncLine, const char* noteText, char* buf, const size_t len) {
  if (buf == nullptr || len == 0) return false;
  buf[0] = '\0';
  const bool hasSync = syncLine != nullptr && syncLine[0] != '\0';
  const bool hasNote = noteText != nullptr && noteText[0] != '\0';
  if (hasSync && hasNote) {
    snprintf(buf, len, "%s \xC2\xB7 %s", syncLine, noteText);
  } else if (hasSync) {
    snprintf(buf, len, "%s", syncLine);
  } else if (hasNote) {
    snprintf(buf, len, "%s", noteText);
  } else {
    return false;
  }
  return true;
}

const char* statusName(const Status s) {
  switch (s) {
    case Status::NotDue:
      return "not-due";
    case Status::Ran:
      return "ran";
    case Status::Rebooting:
      return "rebooting";
    case Status::SkippedReaderOpen:
      return "skipped:reader-open";
    case Status::SkippedNotProvisioned:
      return "skipped:not-provisioned";
    case Status::SkippedNoClock:
      return "skipped:no-clock";
    case Status::SkippedLowBattery:
      return "skipped:battery";
    case Status::SkippedNoWifi:
      return "skipped:no-wifi";
  }
  return "unknown";
}

// --- orchestration -----------------------------------------------------------------

namespace {

class Routine {
 public:
  Routine(RoutinePort& p, const Trigger t) : port(p), trigger(t), startMs(p.millis()) {}

  Result run() {
    if (port.readerOpen()) return finish(Status::SkippedReaderOpen);
    if (!port.provisioned()) return finish(Status::SkippedNotProvisioned);
    const int battery = port.batteryPercent();

    // 1. A trusted "now" before deciding anything (fail closed).
    if (!establishClock(battery)) return finish(Status::SkippedNoClock);

    // 2. The 06:00 gate.
    const std::string tz = port.timezone();
    result.boundary = mostRecentBoundary(result.now, tz.c_str());
    if (result.boundary < 0) return finish(Status::SkippedNoClock);
    if (!isDue(result.now, port.lastRun(), result.boundary)) return finish(Status::NotDue);

    // 3. Battery floor for the whole routine. last_run stays unwritten so a
    // charged start later today still runs it.
    if (battery < kDailyJoinBatteryFloor) {
      port.setNote(Note::LowBattery);
      return finish(Status::SkippedLowBattery);
    }

    // 4. Wi-Fi.
    if (!ensureWifi()) return finish(Status::SkippedNoWifi);

    result.fields.startedAt = isoUtc(result.now);
    result.fields.trigger = triggerName(trigger);

    // 5. Firmware. Returns true when an image was installed (reboot pending).
    Note note = Note::None;
    if (firmwareStep(battery, note)) {
      result.status = Status::Rebooting;
      port.setNote(Note::None);
      port.wifiOff();
      port.reboot();
      return result;
    }
    port.setNote(note);

    // 6. Books: always, whatever the firmware branch did.
    const uint32_t rem = remaining();
    const uint32_t hard = rem > 0 ? rem : 1;  // 0 would mean "no budget" downstream
    const uint32_t itemBudget = hard > kSyncTailMs ? hard - kSyncTailMs : 1;
    port.showStage(Stage::Syncing);
    result.sync = port.runSync(result.fields, itemBudget, hard);
    result.fields.contentOutcome = contentOutcome(result.sync);

    // 7. Done for this routine-day.
    port.setLastRun(result.now);
    return finish(Status::Ran);
  }

 private:
  RoutinePort& port;
  const Trigger trigger;
  const uint32_t startMs;
  bool ownWifi = false;
  Result result;

  uint32_t remaining() {
    const uint32_t elapsed = port.millis() - startMs;
    return elapsed >= kDailyRoutineCapMs ? 0 : kDailyRoutineCapMs - elapsed;
  }

  static uint32_t cap(const uint32_t budget, const uint32_t rem) { return budget < rem ? budget : rem; }

  Result finish(const Status s) {
    if (ownWifi) port.wifiOff();
    result.status = s;
    return result;
  }

  bool ensureWifi() {
    if (port.wifiConnected()) {
      ownWifi = true;
      return true;
    }
    const uint32_t budget = cap(kJoinBudgetMs, remaining());
    if (budget == 0 || !port.joinWifi(budget)) return false;
    ownWifi = true;
    return true;
  }

  bool establishClock(const int battery) {
    const ClockChoice local =
        chooseLocalClock(port.bootClockEpoch(), port.systemClockEpoch(), port.rtcEpoch(), port.timeFloor());
    if (local.source != ClockSource::None) {
      if (local.source == ClockSource::Rtc) port.adoptClock(local.epoch);
      result.clock = local.source;
      result.now = local.epoch;
      return true;
    }
    // No trustworthy local clock: Wi-Fi + SNTP, unless the battery forbids
    // the join (then the routine cannot know whether it is due).
    if (battery < kDailyJoinBatteryFloor) return false;
    port.showStage(Stage::Clock);
    if (!ensureWifi()) return false;
    const uint32_t budget = cap(kClockSyncBudgetMs, remaining());
    const int64_t synced = budget > 0 ? port.syncClock(budget) : 0;
    if (synced < kMinPlausibleEpoch) return false;
    result.clock = ClockSource::Network;
    result.now = synced;
    return true;
  }

  // Sets result.fields.fwOutcome and `note`; true when an image was installed.
  bool firmwareStep(const int battery, Note& note) {
    std::string& fw = result.fields.fwOutcome;
    if (remaining() <= kSyncReserveMs) {
      fw = kFwDeferredTime;
      return false;
    }
    const bool sameDay = port.otaDay() == result.boundary;
    const int attempts = sameDay ? port.otaCount() : 0;

    port.showStage(Stage::CheckingFirmware);
    const OtaCheck check = port.checkOta();
    if (check.error != sync::Error::None) {
      fw = fwFailed(check.error);
      return false;
    }
    switch (check.decision) {
      case sync::OtaDecision::None:
        fw = kFwNone;
        return false;
      case sync::OtaDecision::Current:
        // The image installed earlier this routine-day passed its health check
        // (a failed one would have rolled back and still be offered).
        fw = attempts > 0 ? kFwInstalled : kFwNone;
        return false;
      case sync::OtaDecision::Deferred:
        fw = kFwDeferredTunnel;
        note = Note::FirmwarePending;
        return false;
      case sync::OtaDecision::Install:
        break;
    }
    // Needs a human or another day: sync first, firmware deferred.
    if (attempts >= kMaxOtaInstallsPerDay) {
      fw = kFwDeferredDailyCap;
      note = Note::FirmwarePending;
      return false;
    }
    if (battery < kDailyOtaBatteryFloor) {
      fw = kFwDeferredBattery;
      note = Note::FirmwarePending;
      return false;
    }
    const uint32_t rem = remaining();
    if (rem < kSyncReserveMs + kOtaMinInstallMs) {
      fw = kFwDeferredTime;
      note = Note::FirmwarePending;
      return false;
    }
    // Count the attempt before it starts, so a crash or power loss mid-install
    // still uses up today's install.
    port.setOtaDay(result.boundary);
    port.setOtaCount(static_cast<uint8_t>(attempts + 1));
    port.showStage(Stage::InstallingFirmware);
    const sync::Error e = port.installOta(rem - kSyncReserveMs);
    if (e == sync::Error::None) {
      fw = kFwInstalled;
      return true;
    }
    fw = fwFailed(e);
    return false;
  }
};

}  // namespace

Result run(RoutinePort& port, const Trigger trigger) { return Routine(port, trigger).run(); }

}  // namespace xteink::daily

#ifdef ARDUINO
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <nvs.h>
#include <sys/time.h>

#include <atomic>

#include "CrossPointSettings.h"
#include "WifiCredentialStore.h"
#include "WifiJoin.h"
#include "XteinkConfig.h"
#include "activities/Activity.h"  // ActivityManager + RenderLock
#include "components/UITheme.h"
#include "network/OtaUpdater.h"
#include "util/Timezones.h"

namespace xteink::daily {

namespace {

constexpr const char* kTag = "DAILY";
constexpr const char* kNvsNamespace = "xteink";  // shared with XteinkConfig/XteinkSync/OtaHealth
constexpr const char* kNvsLastRun = "last_run";
constexpr const char* kNvsOtaDay = "ota_day";
constexpr const char* kNvsOtaCount = "ota_n";
constexpr const char* kNvsNote = "dr_note";
// SETTINGS.clockUtcOffsetQ value meaning "UTC+0", i.e. never set (legacy).
constexpr uint8_t kLegacyOffsetUnset = 48;

std::atomic<int> cachedNote{-1};  // -1 = not loaded

int64_t nvsGetI64(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
  int64_t v = 0;
  if (nvs_get_i64(h, key, &v) != ESP_OK) v = 0;
  nvs_close(h);
  return v;
}

uint8_t nvsGetU8(const char* key) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) != ESP_OK) return 0;
  uint8_t v = 0;
  if (nvs_get_u8(h, key, &v) != ESP_OK) v = 0;
  nvs_close(h);
  return v;
}

template <typename Setter>
void nvsWrite(const char* key, Setter set) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) {
    LOG_ERR(kTag, "NVS open failed (%s)", key);
    return;
  }
  esp_err_t err = set(h);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) LOG_ERR(kTag, "NVS write %s failed: %d", key, static_cast<int>(err));
}

struct CancelCtx {
  uint32_t startMs;
  uint32_t budgetMs;
};

bool syncShouldCancel(void* ctx) {
  const auto* c = static_cast<const CancelCtx*>(ctx);
  return sync::budgetExpired(::millis(), c->startMs, c->budgetMs);
}

class DevicePort final : public RoutinePort {
 public:
  DevicePort(GfxRenderer& r, const int64_t bootClock) : renderer(r), bootClock(bootClock) {}

  uint32_t millis() override { return ::millis(); }
  bool readerOpen() override { return activityManager.isReaderActivity(); }
  bool provisioned() override { return !config::getDeviceKey().empty(); }
  int batteryPercent() override { return powerManager.getBatteryPercentage(); }

  int64_t bootClockEpoch() override { return bootClock; }
  int64_t systemClockEpoch() override { return static_cast<int64_t>(time(nullptr)); }
  int64_t rtcEpoch() override {
    if (!halClock.isAvailable()) return 0;
    struct tm local{};
    if (!halClock.localTime(local)) return 0;
    // localTime() applied the process TZ; mktime() under the same TZ inverts it.
    const time_t t = mktime(&local);
    return t == static_cast<time_t>(-1) ? 0 : static_cast<int64_t>(t);
  }
  int64_t timeFloor() override { return trustedtime::persistedFloor(); }
  void adoptClock(const int64_t epoch) override {
    if (static_cast<int64_t>(time(nullptr)) >= epoch) return;
    timeval tv = {static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
    trustedtime::note();
  }
  int64_t syncClock(const uint32_t budgetMs) override {
    if (!trustedtime::syncNow(budgetMs)) return 0;
    return static_cast<int64_t>(time(nullptr));
  }
  std::string timezone() override {
    // The shipped clock default is UTC (clockTimezone 255, legacy offset
    // unset); the routine's 06:00 then follows Asia/Jerusalem.
    const bool chosen = SETTINGS.clockTimezone < timezones::count() || SETTINGS.clockUtcOffsetQ != kLegacyOffsetUnset;
    return routineTimezone(chosen, getenv("TZ"));
  }

  bool wifiConnected() override { return WiFi.status() == WL_CONNECTED; }
  bool joinWifi(const uint32_t budgetMs) override {
    if (!storeLoaded) {
      WIFI_STORE.loadFromFile();
      storeLoaded = true;
    }
    const wifijoin::Result r = wifijoin::joinAnySaved(budgetMs);
    if (!r.ok()) {
      LOG_ERR(kTag, "Wi-Fi join failed: %s", wifijoin::errorName(r.error));
      return false;
    }
    trustedtime::startSync();  // snap the clock floor while the network is up
    return true;
  }
  void wifiOff() override {
    if (WiFi.getMode() != WIFI_MODE_NULL) {
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }

  int64_t lastRun() override { return nvsGetI64(kNvsLastRun); }
  void setLastRun(const int64_t epoch) override {
    nvsWrite(kNvsLastRun, [epoch](nvs_handle_t h) { return nvs_set_i64(h, kNvsLastRun, epoch); });
  }
  int64_t otaDay() override { return nvsGetI64(kNvsOtaDay); }
  void setOtaDay(const int64_t boundary) override {
    nvsWrite(kNvsOtaDay, [boundary](nvs_handle_t h) { return nvs_set_i64(h, kNvsOtaDay, boundary); });
  }
  uint8_t otaCount() override { return nvsGetU8(kNvsOtaCount); }
  void setOtaCount(const uint8_t n) override {
    nvsWrite(kNvsOtaCount, [n](nvs_handle_t h) { return nvs_set_u8(h, kNvsOtaCount, n); });
  }
  void setNote(const Note note) override {
    const auto b = static_cast<uint8_t>(note);
    cachedNote.store(b);
    if (nvsGetU8(kNvsNote) == b) return;  // NVS wear: write only on change
    nvsWrite(kNvsNote, [b](nvs_handle_t h) { return nvs_set_u8(h, kNvsNote, b); });
  }

  OtaCheck checkOta() override {
    OtaCheck c;
    const OtaUpdater::OtaUpdaterError e = updater.checkForUpdate();
    if (e == OtaUpdater::OK || e == OtaUpdater::NO_UPDATE) {
      c.decision = updater.getDecision();
    } else {
      c.error = updater.getLastError() != sync::Error::None ? updater.getLastError() : sync::Error::Network;
    }
    return c;
  }
  sync::Error installOta(const uint32_t budgetMs) override {
    const OtaUpdater::OtaUpdaterError e = updater.installUpdate(nullptr, nullptr, budgetMs);
    if (e == OtaUpdater::OK) return sync::Error::None;
    return updater.getLastError() != sync::Error::None ? updater.getLastError() : sync::Error::Flash;
  }
  void reboot() override {
    LOG_INF(kTag, "Firmware installed; rebooting into it");
    delay(50);
    ESP.restart();
  }
  sync::Outcome runSync(const sync::RunFields& fields, const uint32_t itemBudgetMs,
                        const uint32_t hardBudgetMs) override {
    CancelCtx ctx{::millis(), hardBudgetMs};
    sync::Callbacks cb;
    cb.ctx = &ctx;
    cb.shouldCancel = &syncShouldCancel;
    return sync::run(&renderer, cb, itemBudgetMs, &fields);
  }

  void showStage(const Stage stage) override {
    const char* text = nullptr;
    switch (stage) {
      case Stage::Clock:
        text = tr(STR_XTEINK_SYNC_CONTACTING);
        break;
      case Stage::CheckingFirmware:
        text = tr(STR_CHECKING_UPDATE);
        break;
      case Stage::InstallingFirmware:
        text = tr(STR_UPDATING);
        break;
      case Stage::Syncing:
        text = tr(STR_XTEINK_SYNCING);
        break;
    }
    RenderLock lock;
    GUI.drawPopup(renderer, text);
  }

 private:
  GfxRenderer& renderer;
  const int64_t bootClock;
  bool storeLoaded = false;
  OtaUpdater updater;
};

}  // namespace

void onBoot(GfxRenderer& renderer, const BootKind kind, const bool silentReboot, const bool panicReboot,
            const bool recoveryMode, const bool deepSleepWake, const int64_t bootClockEpoch) {
  if (!bootQualifies(kind, silentReboot, panicReboot, recoveryMode)) return;
  DevicePort port(renderer, bootClockEpoch);
  const uint32_t startMs = millis();
  const Result r = run(port, deepSleepWake ? Trigger::Wake : Trigger::PowerOn);
  LOG_INF(kTag, "Daily routine: %s (clock %s, fw %s, content %s, %lu ms)", statusName(r.status),
          clockSourceName(r.clock), r.fields.fwOutcome.empty() ? "-" : r.fields.fwOutcome.c_str(),
          r.fields.contentOutcome.empty() ? "-" : r.fields.contentOutcome.c_str(),
          static_cast<unsigned long>(millis() - startMs));
}

Note homeNote() {
  int v = cachedNote.load();
  if (v < 0) {
    v = nvsGetU8(kNvsNote);
    cachedNote.store(v);
  }
  return noteFromByte(static_cast<uint8_t>(v));
}

void onSyncSucceeded() {
  if (homeNote() != Note::LowBattery) return;
  cachedNote.store(static_cast<int>(Note::None));
  nvsWrite(kNvsNote, [](nvs_handle_t h) { return nvs_set_u8(h, kNvsNote, static_cast<uint8_t>(Note::None)); });
}

}  // namespace xteink::daily
#endif
