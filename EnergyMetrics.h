#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace energy_metrics {

struct Control {
  bool eligible = false;
  double commandedW = 0;
  double headroomW = 0;
};

inline bool addResistiveOutput(Control& control, int nominalW, int delayPercent,
                               bool supported) {
  if (!supported || nominalW <= 0 || delayPercent < 0 || delayPercent > 100) return false;
  const double fraction = (100 - delayPercent) / 100.0;
  control.commandedW += nominalW * fraction;
  control.headroomW += nominalW * (1.0 - fraction);
  return true;
}

class Counter {
public:
  static constexpr uint32_t staleMs = 3000;
  static constexpr uint32_t maxTickMs = 1000;

  double importWh = 0;
  double exportWh = 0;
  double importW = 0;
  double exportW = 0;
  double gridW = 0;
  uint32_t sourcePeriodMs = 0;

  void invalidate() {
    valid_ = false;
    clearDiagnostics();
  }

  void setControl(uint32_t now, Control control) {
    advance(now);
    control_ = control;
    updateDiagnostics();
  }

  bool sample(uint32_t receivedAt, uint64_t stamp, double powerW) {
    // A mailbox notification can arrive after a loop tick at the same instant.
    if (!started_ || uint32_t(receivedAt - lastTick_) < UINT32_MAX / 2)
      advance(receivedAt);
    if (!stamp || !std::isfinite(powerW) || stamp < stamp_) {
      invalidate();
      return false;
    }
    // Source timestamps have one-second resolution: equal stamps can carry new power.
    if (stamp_ && stamp > stamp_) {
      const uint64_t period = (stamp - stamp_) * 1000;
      sourcePeriodMs = period <= UINT32_MAX ? static_cast<uint32_t>(period) : 0;
    }
    stamp_ = stamp;
    powerW_ = powerW;
    receivedAt_ = receivedAt;
    valid_ = true;
    updateDiagnostics();
    return true;
  }

  bool fresh(uint32_t now) const {
    return valid_ && uint32_t(now - receivedAt_) <= staleMs;
  }

  void advance(uint32_t now) {
    if (!started_) {
      started_ = true;
      lastTick_ = now;
      return;
    }
    uint32_t dt = now - lastTick_;
    lastTick_ = now;
    if (!fresh(now) || dt > maxTickMs) {
      invalidate();
      return;
    }
    if (!control_.eligible) {
      updateDiagnostics();
      return;
    }
    updateDiagnostics();
    importWh += importW * dt / 3600000.0;
    exportWh += exportW * dt / 3600000.0;
  }

private:
  Control control_;
  bool started_ = false;
  bool valid_ = false;
  uint32_t lastTick_ = 0;
  uint32_t receivedAt_ = 0;
  uint64_t stamp_ = 0;
  double powerW_ = 0;
  void clearDiagnostics() {
    importW = exportW = gridW = 0;
  }

  void updateDiagnostics() {
    if (!valid_) {
      clearDiagnostics();
      return;
    }
    gridW = powerW_;
    importW = control_.eligible
      ? std::min(std::max(gridW, 0.0), control_.commandedW) : 0;
    exportW = control_.eligible
      ? std::min(std::max(-gridW, 0.0), control_.headroomW) : 0;
  }
};

}  // namespace energy_metrics
