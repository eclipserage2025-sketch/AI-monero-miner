# 🧠 AI Monero Miner

A high-performance Monero (XMR) CPU miner written in modern C++20, featuring a built-in **neural-network optimizer** that continuously tunes mining parameters in real time.

> ⚠️ **Disclaimer:** This software is intended for **legal, personal cryptocurrency mining only**. Do not deploy on systems you do not own or have explicit permission to use. The authors are not responsible for misuse.

---

## ✨ Features

| Feature | Description |
|---|---|
| **RandomX** | Full RandomX PoW via [tevador/RandomX](https://github.com/tevador/RandomX) |
| **Neural-Net Optimizer** | Lightweight feedforward NN auto-tunes thread count, CPU affinity, and intensity every N seconds |
| **System Monitor** | Reads CPU temperature, frequency, load, and power draw to feed the optimizer |
| **Stratum v1** | Pool mining with keepalive, reconnect, and job caching |
| **NUMA Awareness** | Optional hwloc integration for topology-aware thread pinning |
| **Huge Pages** | Automatic huge-page allocation for RandomX dataset |
| **JSON Config** | Simple JSON configuration with sensible defaults |
| **Cross-Platform** | Linux, macOS, Windows (MSVC / MinGW) |

---

## 🏗️ Building

### Prerequisites

- CMake ≥ 3.16
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- Git (for submodules)
- *Optional:* hwloc, for NUMA-aware pinning

### Steps

```bash
git clone --recursive https://github.com/YOUR_USER/ai-monero-miner.git
cd ai-monero-miner
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### Quick Start

```bash
cp config/default_config.json config/user_config.json
# Edit user_config.json with your pool and wallet
./ai-monero-miner --config ../config/user_config.json
```

---

## 🧠 How the AI Optimizer Works

The optimizer is a small **feedforward neural network** trained online via back-propagation:

1. **Observe** — the `SystemMonitor` samples CPU metrics (temperature, load, frequency, power) and the current hash rate every tick.
2. **Predict** — the NN receives the state vector and outputs a set of *candidate mining parameters* (thread count, intensity per thread, sleep intervals).
3. **Act** — the miner applies the new parameters for the next optimization window.
4. **Learn** — after the window, the actual hash-rate delta and a power-efficiency reward signal are used to compute loss and update weights via SGD.

An **ε-greedy exploration** strategy occasionally tries random parameter perturbations to escape local optima.

### State Vector (inputs)

| Index | Feature |
|---|---|
| 0 | Normalised CPU temperature |
| 1 | Average CPU load (0-1) |
| 2 | Average CPU frequency / max frequency |
| 3 | Estimated power draw (W, normalised) |
| 4 | Current hash rate (H/s, normalised) |
| 5 | Current thread count / max threads |
| 6 | Network difficulty (log-scaled) |
| 7 | Time-of-day (cyclical encoding) |

### Action Vector (outputs)

| Index | Action |
|---|---|
| 0 | Target thread count (scaled to [1, max]) |
| 1 | Per-thread intensity (0-1) |
| 2 | Inter-batch sleep µs (scaled) |

---

## 📁 Project Structure

```
ai-monero-miner/
├── CMakeLists.txt
├── README.md
├── config/
│   └── default_config.json
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── miner.h / .cpp          # Mining orchestrator
│   │   └── worker.h / .cpp         # Per-thread worker
│   ├── network/
│   │   ├── stratum_client.h / .cpp # Stratum v1 pool client
│   │   └── job.h                   # Mining job struct
│   ├── ai/
│   │   ├── optimizer.h / .cpp      # AI optimisation loop
│   │   ├── neural_net.h / .cpp     # Feedforward NN
│   │   └── system_monitor.h / .cpp # CPU / power telemetry
│   ├── crypto/
│   │   └── randomx_handler.h / .cpp
│   └── utils/
│       ├── config.h / .cpp
│       └── logger.h / .cpp
└── third_party/
    └── RandomX/                    # git submodule
```

---

## ⚙️ Configuration Reference

See [`config/default_config.json`](config/default_config.json) for all options. Key AI knobs:

| Key | Default | Description |
|---|---|---|
| `ai.enabled` | `true` | Enable / disable the neural-net optimizer |
| `ai.optimization_interval_sec` | `30` | Seconds between optimization ticks |
| `ai.learning_rate` | `0.001` | SGD learning rate |
| `ai.hidden_layers` | `[64, 32]` | NN hidden-layer sizes |
| `ai.exploration_rate` | `0.15` | ε-greedy exploration probability |
| `ai.target_temp_celsius` | `80` | Thermal throttle target |

---

## 📜 License

MIT — see [LICENSE](LICENSE).
