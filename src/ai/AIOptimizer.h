#ifndef XMRIG_AIOPTIMIZER_H
#define XMRIG_AIOPTIMIZER_H

#include <vector>
#include <memory>
#include <chrono>
#include <random>
#include <thread>

namespace xmrig {

class Controller;

struct AIState {
    double hashrate = 0.0;
    double cpu_load = 0.0;
    double cpu_temp = 0.0;
    int thread_count = 0;
    std::vector<double> to_vector() const;
};

struct AIAction {
    int thread_count_delta = 0;
};

class AIOptimizer {
public:
    AIOptimizer(Controller* controller);
    ~AIOptimizer();

    void start();
    void stop();

    void update(double current_hashrate);

private:
    void loop();
    void apply(const AIAction& action);

    Controller* m_controller;
    bool m_running = false;
    std::unique_ptr<std::thread> m_thread;

    std::mt19937 m_rng;
    double m_epsilon = 0.2;
};

} // namespace xmrig

#endif // XMRIG_AIOPTIMIZER_H
