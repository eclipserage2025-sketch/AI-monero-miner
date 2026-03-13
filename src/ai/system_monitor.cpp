#include "ai/system_monitor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "utils/logger.h"

namespace aiminer::ai {

SystemMonitor::SystemMonitor() {
    // Detect max CPU frequency
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq");
    if (f.is_open()) {
        double khz;
        f >> khz;
        max_freq_ = khz / 1000.0;  // MHz
    } else {
        max_freq_ = 3500.0;  // fallback guess
    }
    LOG_DEBUG("SystemMonitor: max_freq = {:.0f} MHz", max_freq_);

    // Prime CPU load reading
    read_cpu_load();
}

SystemMetrics SystemMonitor::sample() const {
    SystemMetrics m;
    m.cpu_temp      = read_cpu_temp();
    m.cpu_load      = read_cpu_load();
    m.cpu_freq_norm = read_cpu_freq() / max_freq_;
    m.power_draw    = estimate_power();
    return m;
}

double SystemMonitor::read_cpu_temp() const {
    // Try hwmon thermal zone
    for (int i = 0; i < 10; ++i) {
        std::string path = "/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp";
        std::ifstream f(path);
        if (f.is_open()) {
            double millideg;
            f >> millideg;
            return millideg / 1000.0;
        }
    }
    return 50.0;  // fallback
}

double SystemMonitor::read_cpu_load() const {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0.5;

    std::string line;
    std::getline(f, line);  // first line = aggregate
    std::istringstream ss(line);
    std::string cpu_label;
    ss >> cpu_label;

    std::vector<double> vals;
    double v;
    while (ss >> v) vals.push_back(v);
    if (vals.size() < 4) return 0.5;

    double idle  = vals[3];
    double total = std::accumulate(vals.begin(), vals.end(), 0.0);

    double d_idle  = idle - prev_idle_;
    double d_total = total - prev_total_;
    prev_idle_  = idle;
    prev_total_ = total;

    if (d_total < 1.0) return 0.0;
    return std::clamp(1.0 - d_idle / d_total, 0.0, 1.0);
}

double SystemMonitor::read_cpu_freq() const {
    std::ifstream f("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    if (f.is_open()) {
        double khz;
        f >> khz;
        return khz / 1000.0;
    }
    return max_freq_;
}

double SystemMonitor::estimate_power() const {
    // Try Intel RAPL (Running Average Power Limit)
    std::ifstream f("/sys/class/powercap/intel-rapl:0/energy_uj");
    if (f.is_open()) {
        // NOTE: A proper implementation would track delta over time.
        // For now we return a normalised estimate.
        return 65.0;  // placeholder TDP estimate
    }
    return 65.0;
}

}  // namespace aiminer::ai
