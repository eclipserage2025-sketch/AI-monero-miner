#include "ai/system_monitor.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "utils/logger.h"

#ifdef _WIN32
#   include <comdef.h>
#   include <wbemidl.h>
#   pragma comment(lib, "pdh.lib")
#   pragma comment(lib, "ole32.lib")
#   pragma comment(lib, "oleaut32.lib")
#   pragma comment(lib, "wbemuuid.lib")
#else
#   include <fstream>
#   include <numeric>
#   include <sstream>
#endif

namespace aiminer::ai {

// ═══════════════════════════════════════════════════════════════════════════
// Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════════════

SystemMonitor::SystemMonitor() {
#ifdef _WIN32
    // Detect max CPU frequency via PDH
    max_freq_ = 3500.0;  // fallback

    if (PdhOpenQuery(nullptr, 0, &pdh_query_) == ERROR_SUCCESS) {
        if (PdhAddEnglishCounterA(pdh_query_,
                "\\Processor Information(_Total)\\Processor Frequency",
                0, &pdh_freq_counter_) != ERROR_SUCCESS) {
            pdh_freq_counter_ = nullptr;
        }
        // Prime the query
        PdhCollectQueryData(pdh_query_);
    }

    // Prime CPU load via GetSystemTimes
    GetSystemTimes(&prev_idle_ft_, &prev_kernel_ft_, &prev_user_ft_);
    win_cpu_primed_ = true;

    LOG_DEBUG("SystemMonitor (Windows): max_freq = {:.0f} MHz (fallback)", max_freq_);
#else
    // Detect max CPU frequency on Linux
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
#endif
}

SystemMonitor::~SystemMonitor() {
#ifdef _WIN32
    if (pdh_query_) {
        PdhCloseQuery(pdh_query_);
        pdh_query_ = nullptr;
    }
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
// Sample
// ═══════════════════════════════════════════════════════════════════════════

SystemMetrics SystemMonitor::sample() const {
    SystemMetrics m;
    m.cpu_temp      = read_cpu_temp();
    m.cpu_load      = read_cpu_load();
    m.cpu_freq_norm = read_cpu_freq() / max_freq_;
    m.power_draw    = estimate_power();
    return m;
}

// ═══════════════════════════════════════════════════════════════════════════
// Platform implementations
// ═══════════════════════════════════════════════════════════════════════════

#ifdef _WIN32

// ── Windows: CPU temperature ────────────────────────────────────────────────
double SystemMonitor::read_cpu_temp() const {
    // Try WMI MSAcpi_ThermalZoneTemperature
    // This requires admin privileges on most systems; fall back gracefully.
    double temp = 50.0;  // fallback

    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    IEnumWbemClassObject* enumerator = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool com_init = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    if (com_init) {
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, reinterpret_cast<void**>(&locator));
        if (SUCCEEDED(hr) && locator) {
            hr = locator->ConnectServer(
                _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr,
                nullptr, &services);
            if (SUCCEEDED(hr) && services) {
                CoSetProxyBlanket(services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                  nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

                hr = services->ExecQuery(
                    _bstr_t(L"WQL"),
                    _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                    nullptr, &enumerator);

                if (SUCCEEDED(hr) && enumerator) {
                    IWbemClassObject* obj = nullptr;
                    ULONG ret = 0;
                    if (enumerator->Next(WBEM_INFINITE, 1, &obj, &ret) == S_OK && ret > 0) {
                        VARIANT val;
                        VariantInit(&val);
                        if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &val, nullptr, nullptr))) {
                            // Value is in tenths of Kelvin
                            temp = (static_cast<double>(val.intVal) / 10.0) - 273.15;
                        }
                        VariantClear(&val);
                        obj->Release();
                    }
                    enumerator->Release();
                }
                services->Release();
            }
            locator->Release();
        }
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            // Only uninit if we initialised
            if (hr != RPC_E_CHANGED_MODE) CoUninitialize();
        }
    }
    return temp;
}

// ── Windows: CPU load via GetSystemTimes ────────────────────────────────────
static inline uint64_t ft_to_u64(const FILETIME& ft) {
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

double SystemMonitor::read_cpu_load() const {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0.5;

    if (!win_cpu_primed_) {
        prev_idle_ft_ = idle;
        prev_kernel_ft_ = kernel;
        prev_user_ft_ = user;
        win_cpu_primed_ = true;
        return 0.0;
    }

    uint64_t d_idle   = ft_to_u64(idle)   - ft_to_u64(prev_idle_ft_);
    uint64_t d_kernel = ft_to_u64(kernel) - ft_to_u64(prev_kernel_ft_);
    uint64_t d_user   = ft_to_u64(user)   - ft_to_u64(prev_user_ft_);

    prev_idle_ft_   = idle;
    prev_kernel_ft_ = kernel;
    prev_user_ft_   = user;

    uint64_t d_total = d_kernel + d_user;  // kernel includes idle
    if (d_total == 0) return 0.0;
    return std::clamp(1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total), 0.0, 1.0);
}

// ── Windows: CPU frequency via PDH ──────────────────────────────────────────
double SystemMonitor::read_cpu_freq() const {
    if (pdh_query_ && pdh_freq_counter_) {
        if (PdhCollectQueryData(pdh_query_) == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE val;
            if (PdhGetFormattedCounterValue(pdh_freq_counter_, PDH_FMT_DOUBLE,
                                            nullptr, &val) == ERROR_SUCCESS) {
                return val.doubleValue;  // MHz
            }
        }
    }
    return max_freq_;
}

// ── Windows: power estimate via GetSystemPowerStatus ────────────────────────
double SystemMonitor::estimate_power() const {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        // If on battery, rough estimate from battery life percentage
        if (sps.ACLineStatus == 0 && sps.BatteryLifePercent != 255) {
            // Very rough heuristic: assume ~45W TDP on battery
            return 45.0 * (static_cast<double>(100 - sps.BatteryLifePercent) / 100.0 + 0.5);
        }
    }
    return 65.0;  // fallback TDP estimate
}

#else  // ── Linux / macOS implementations ────────────────────────────────────

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

#endif  // _WIN32

}  // namespace aiminer::ai
