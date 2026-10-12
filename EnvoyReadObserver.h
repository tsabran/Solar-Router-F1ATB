#pragma once

#include <cstdint>

struct EnvoyEnergySample {
  uint64_t stamp = 0;
  double powerW = 0;
};

class EnvoyReadObserver {
public:
  struct Hooks {
    bool (*enabled)();
    bool (*header)(const char*, EnvoyEnergySample&);
    bool (*phase)(const char*, double&);
    void (*received)(EnvoyEnergySample, bool);
  };

  // Register before starting the measurement task; no observer means no extra parsing.
  static void registerHooks(const Hooks* hooks) { registeredHooks() = hooks; }

  EnvoyReadObserver() {
    const Hooks* hooks = registeredHooks();
    if (hooks && hooks->enabled()) hooks_ = hooks;
  }

  EnvoyReadObserver(const EnvoyReadObserver&) = delete;
  EnvoyReadObserver& operator=(const EnvoyReadObserver&) = delete;

  ~EnvoyReadObserver() {
    if (hooks_) hooks_->received(sample_, valid_ && complete_);
  }

  template <typename Payload>
  void header(const Payload& payload) {
    if (hooks_) valid_ = hooks_->header(text(payload), sample_);
  }

  template <typename Payload>
  void phase(const Payload& payload) {
    if (hooks_) valid_ = valid_ && hooks_->phase(text(payload), sample_.powerW);
  }

  void complete() { complete_ = true; }

private:
  const Hooks* hooks_ = nullptr;
  EnvoyEnergySample sample_;
  bool valid_ = false;
  bool complete_ = false;

  static const Hooks*& registeredHooks() {
    static const Hooks* hooks = nullptr;
    return hooks;
  }

  static const char* text(const char* payload) { return payload; }

  template <typename Payload>
  static const char* text(const Payload& payload) { return payload.c_str(); }
};
