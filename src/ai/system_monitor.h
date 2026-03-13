#pragma once

#include <chrono>
#include <string>

namespace aiminer::ai {

/// Reads system metrics used as neural-net inputs
struct SystemMetrics {
    double cpu_temp      = 0.0;   // °C
    double cpu_load      = 0.0;   // 0-1
    double cpu_freq_norm = 0.0;   // current / max
    double power_draw    = 0.0;   // watts (estimated)
};

class SystemMonitor {
public:
    SystemMonitor();

    /// Sample current metrics
    SystemMetrics sample() const;

    /// Max CPU frequency detected at start (MHz)
    double max_freq_mhz() const { return max_freq_; }

private:
    double read_cpu_temp() const;
    double read_cpu_load() const;
    double read_cpu_freq() const;
    double estimate_power() const;

    double max_freq_ = 1.0;
    mutable double prev_idle_  = 0;
    mutable double prev_total_ = 0;
};

}  // namespace aiminer::ai
