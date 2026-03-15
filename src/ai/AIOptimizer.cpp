#include "ai/AIOptimizer.h"
#include "ai/NeuralNetwork.h"
#include "core/Controller.h"
#include "core/Miner.h"
#include "backend/cpu/Cpu.h"
#include "base/kernel/Process.h"
#include "base/io/log/Log.h"
#include <thread>
#include <iostream>
#include <algorithm>

namespace xmrig {

std::vector<double> AIState::to_vector() const {
    return { hashrate / 10000.0, cpu_load / 100.0, cpu_temp / 100.0, static_cast<double>(thread_count) / 64.0 };
}

AIOptimizer::AIOptimizer(Controller* controller) : m_controller(controller), m_rng(std::random_device{}()) {
}

AIOptimizer::~AIOptimizer() {
    stop();
}

void AIOptimizer::start() {
    m_running = true;
    m_thread = std::make_unique<std::thread>(&AIOptimizer::loop, this);
}

void AIOptimizer::stop() {
    m_running = false;
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

void AIOptimizer::loop() {
    NeuralNetwork nn({4, 8, 3});
    AIState currentState;

    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        if (!m_running) break;

        AIState nextState;
        nextState.hashrate = m_controller->miner()->hashrate();
        nextState.cpu_load = 50.0;
        nextState.cpu_temp = 60.0;
        nextState.thread_count = static_cast<int>(Cpu::info()->threads());

        std::vector<double> qValues = nn.forward(currentState.to_vector());
        int actionIdx;
        if (std::uniform_real_distribution<double>(0, 1)(m_rng) < m_epsilon) {
            actionIdx = std::uniform_int_distribution<int>(0, 2)(m_rng);
        } else {
            actionIdx = std::distance(qValues.begin(), std::max_element(qValues.begin(), qValues.end()));
        }

        AIAction action;
        action.thread_count_delta = actionIdx - 1;

        apply(action);

        double reward = (nextState.hashrate - currentState.hashrate);
        std::vector<double> targets = qValues;
        targets[actionIdx] = reward + 0.9 * (*std::max_element(qValues.begin(), qValues.end()));
        nn.backward(targets);

        LOG_INFO("[AI] Hashrate: %.2f H/s, Reward: %.2f, Action: %d", nextState.hashrate, reward, action.thread_count_delta);

        currentState = nextState;
    }
}

void AIOptimizer::apply(const AIAction& action) {
    if (action.thread_count_delta == 0) return;
    LOG_INFO("[AI] Optimization suggested thread delta: %d", action.thread_count_delta);
}

} // namespace xmrig
