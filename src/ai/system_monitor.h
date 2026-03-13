#pragma once

#include <chrono>
#include <string>

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <pdh.h>
#endif

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
    ~SystemMonitor();

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

#ifdef _WIN32
    // Windows: GetSystemTimes-based CPU load
    mutable FILETIME prev_idle_ft_{};
    mutable FILETIME prev_kernel_ft_{};
    mutable FILETIME prev_user_ft_{};
    mutable bool win_cpu_primed_ = false;

    // PDH query for CPU frequency
    PDH_HQUERY   pdh_query_ = nullptr;
    PDH_HCOUNTER pdh_freq_counter_ = nullptr;
#else
    // Linux: /proc/stat-based CPU load
    mutable double prev_idle_  = 0;
    mutable double prev_total_ = 0;
#endif
};

}  // namespace aiminer::ai
