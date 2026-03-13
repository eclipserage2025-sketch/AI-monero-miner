#include "ai/neural_net.h"

#include <cmath>
#include <fstream>
#include <stdexcept>

namespace aiminer::ai {

NeuralNet::NeuralNet(const std::vector<int>& layer_sizes, double learning_rate)
    : layers_(layer_sizes), lr_(learning_rate) {
    if (layers_.size() < 2)
        throw std::invalid_argument("Need at least input + output layers");

    // Xavier initialisation
    std::normal_distribution<double> dist(0.0, 1.0);

    for (size_t l = 0; l + 1 < layers_.size(); ++l) {
        int fan_in  = layers_[l];
        int fan_out = layers_[l + 1];
        double scale = std::sqrt(2.0 / (fan_in + fan_out));

        std::vector<std::vector<double>> W(fan_out, std::vector<double>(fan_in));
        for (auto& row : W)
            for (auto& w : row)
                w = dist(rng_) * scale;
        weights_.push_back(std::move(W));

        biases_.emplace_back(fan_out, 0.0);
    }
}

// ── Activations ─────────────────────────────────────────────────────────────
double NeuralNet::activate(double x) const {
    return x > 0 ? x : 0.01 * x;  // Leaky ReLU
}

double NeuralNet::activate_deriv(double x) const {
    return x > 0 ? 1.0 : 0.01;
}

double NeuralNet::activate_output(double x) const {
    return 1.0 / (1.0 + std::exp(-x));  // Sigmoid
}

// ── Forward ─────────────────────────────────────────────────────────────────
std::vector<double> NeuralNet::forward(const std::vector<double>& input) {
    activations_.clear();
    z_values_.clear();
    activations_.push_back(input);

    auto current = input;
    for (size_t l = 0; l < weights_.size(); ++l) {
        int out_size = static_cast<int>(weights_[l].size());
        int in_size  = static_cast<int>(weights_[l][0].size());
        std::vector<double> z(out_size), a(out_size);

        for (int j = 0; j < out_size; ++j) {
            double sum = biases_[l][j];
            for (int i = 0; i < in_size; ++i)
                sum += weights_[l][j][i] * current[i];
            z[j] = sum;

            bool is_output = (l + 1 == weights_.size());
            a[j] = is_output ? activate_output(sum) : activate(sum);
        }

        z_values_.push_back(std::move(z));
        activations_.push_back(a);
        current = activations_.back();
    }
    return current;
}

// ── Backpropagation ─────────────────────────────────────────────────────────
double NeuralNet::train(const std::vector<double>& input, const std::vector<double>& target) {
    auto output = forward(input);
    size_t L = weights_.size();

    // Output error (MSE derivative * sigmoid derivative)
    std::vector<std::vector<double>> deltas(L);
    {
        int n = static_cast<int>(output.size());
        deltas[L - 1].resize(n);
        double loss = 0;
        for (int j = 0; j < n; ++j) {
            double o = output[j];
            double err = o - target[j];
            loss += err * err;
            deltas[L - 1][j] = err * o * (1.0 - o);  // sigmoid deriv
        }

        // Hidden layers
        for (int l = static_cast<int>(L) - 2; l >= 0; --l) {
            int cur_size = static_cast<int>(weights_[l].size());
            int next_size = static_cast<int>(weights_[l + 1].size());
            deltas[l].resize(cur_size);
            for (int j = 0; j < cur_size; ++j) {
                double sum = 0;
                for (int k = 0; k < next_size; ++k)
                    sum += weights_[l + 1][k][j] * deltas[l + 1][k];
                deltas[l][j] = sum * activate_deriv(z_values_[l][j]);
            }
        }

        // Update weights
        for (size_t l = 0; l < L; ++l) {
            int out_sz = static_cast<int>(weights_[l].size());
            int in_sz  = static_cast<int>(weights_[l][0].size());
            for (int j = 0; j < out_sz; ++j) {
                for (int i = 0; i < in_sz; ++i)
                    weights_[l][j][i] -= lr_ * deltas[l][j] * activations_[l][i];
                biases_[l][j] -= lr_ * deltas[l][j];
            }
        }
        return loss / n;
    }
}

// ── Persistence ─────────────────────────────────────────────────────────────
void NeuralNet::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    for (size_t l = 0; l < weights_.size(); ++l) {
        for (auto& row : weights_[l])
            f.write(reinterpret_cast<const char*>(row.data()), row.size() * sizeof(double));
        f.write(reinterpret_cast<const char*>(biases_[l].data()), biases_[l].size() * sizeof(double));
    }
}

void NeuralNet::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    for (size_t l = 0; l < weights_.size(); ++l) {
        for (auto& row : weights_[l])
            f.read(reinterpret_cast<char*>(row.data()), row.size() * sizeof(double));
        f.read(reinterpret_cast<char*>(biases_[l].data()), biases_[l].size() * sizeof(double));
    }
}

}  // namespace aiminer::ai
