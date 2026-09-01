#pragma once

/** Deterministic millisecond clock for host-side simulator scenarios. */
class FakeClock {
  public:
    explicit FakeClock(unsigned long startMs = 0) : nowMs_(startMs) {}

    unsigned long now() const { return nowMs_; }

    void set(unsigned long ms) { nowMs_ = ms; }

    void advance(unsigned long deltaMs) { nowMs_ += deltaMs; }

  private:
    unsigned long nowMs_ = 0;
};
