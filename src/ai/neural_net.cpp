#include "ai/neural_net.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace aiminer::ai {

NeuralNet::NeuralNet(const std::vector<int>& layer_sizes,
                     double learning_rate,
                     double beta1,
                     double beta2)
    : layers_(layer_sizes), lr_(learning_rate), beta1_(beta1), beta2_(beta2) {
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

        // Initialise Adam moment estimates to zero (same shapes)
        m_weights_.emplace_back(fan_out, std::vector<double>(fan_in, 0.0));
        v_weights_.emplace_back(fan_out, std::vector<double>(fan_in, 0.0));
        m_biases_.emplace_back(fan_out, 0.0);
        v_biases_.emplace_back(fan_out, 0.0);
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

// ── Backpropagation with Adam ───────────────────────────────────────────────
double NeuralNet::train(const std::vector<double>& input, const std::vector<double>& target) {
    auto output = forward(input);
    size_t L = weights_.size();
    ++timestep_;

    // Output error (MSE derivative * sigmoid derivative)
    std::vector<std::vector<double>> deltas(L);
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

    // ── Gradient clipping (max norm) ────────────────────────────────────
    // Compute total gradient norm across all layers
    double grad_norm_sq = 0.0;
    for (size_t l = 0; l < L; ++l) {
        int out_sz = static_cast<int>(weights_[l].size());
        int in_sz  = static_cast<int>(weights_[l][0].size());
        for (int j = 0; j < out_sz; ++j) {
            for (int i = 0; i < in_sz; ++i) {
                double g = deltas[l][j] * activations_[l][i];
                grad_norm_sq += g * g;
            }
            // bias gradient
            grad_norm_sq += deltas[l][j] * deltas[l][j];
        }
    }
    double grad_norm = std::sqrt(grad_norm_sq);
    double clip_coeff = (grad_norm > max_grad_norm_)
                        ? (max_grad_norm_ / grad_norm)
                        : 1.0;

    // Bias-corrected decay rates for Adam
    double bc1 = 1.0 - std::pow(beta1_, timestep_);
    double bc2 = 1.0 - std::pow(beta2_, timestep_);

    // ── Adam update ─────────────────────────────────────────────────────
    for (size_t l = 0; l < L; ++l) {
        int out_sz = static_cast<int>(weights_[l].size());
        int in_sz  = static_cast<int>(weights_[l][0].size());
        for (int j = 0; j < out_sz; ++j) {
            for (int i = 0; i < in_sz; ++i) {
                double g = clip_coeff * deltas[l][j] * activations_[l][i];

                // Update first moment
                m_weights_[l][j][i] = beta1_ * m_weights_[l][j][i] + (1.0 - beta1_) * g;
                // Update second moment
                v_weights_[l][j][i] = beta2_ * v_weights_[l][j][i] + (1.0 - beta2_) * g * g;

                // Bias-corrected estimates
                double m_hat = m_weights_[l][j][i] / bc1;
                double v_hat = v_weights_[l][j][i] / bc2;

                weights_[l][j][i] -= lr_ * m_hat / (std::sqrt(v_hat) + epsilon_);
            }

            // Bias update
            double gb = clip_coeff * deltas[l][j];
            m_biases_[l][j] = beta1_ * m_biases_[l][j] + (1.0 - beta1_) * gb;
            v_biases_[l][j] = beta2_ * v_biases_[l][j] + (1.0 - beta2_) * gb * gb;

            double m_hat_b = m_biases_[l][j] / bc1;
            double v_hat_b = v_biases_[l][j] / bc2;

            biases_[l][j] -= lr_ * m_hat_b / (std::sqrt(v_hat_b) + epsilon_);
        }
    }

    return loss / n;
}

// ── Persistence ─────────────────────────────────────────────────────────────
void NeuralNet::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    // Save timestep
    f.write(reinterpret_cast<const char*>(&timestep_), sizeof(timestep_));

    for (size_t l = 0; l < weights_.size(); ++l) {
        for (auto& row : weights_[l])
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        f.write(reinterpret_cast<const char*>(biases_[l].data()), static_cast<std::streamsize>(biases_[l].size() * sizeof(double)));

        // Save Adam state
        for (auto& row : m_weights_[l])
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        for (auto& row : v_weights_[l])
            f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        f.write(reinterpret_cast<const char*>(m_biases_[l].data()), static_cast<std::streamsize>(m_biases_[l].size() * sizeof(double)));
        f.write(reinterpret_cast<const char*>(v_biases_[l].data()), static_cast<std::streamsize>(v_biases_[l].size() * sizeof(double)));
    }
}

void NeuralNet::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return;

    f.read(reinterpret_cast<char*>(&timestep_), sizeof(timestep_));

    for (size_t l = 0; l < weights_.size(); ++l) {
        for (auto& row : weights_[l])
            f.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        f.read(reinterpret_cast<char*>(biases_[l].data()), static_cast<std::streamsize>(biases_[l].size() * sizeof(double)));

        // Load Adam state
        for (auto& row : m_weights_[l])
            f.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        for (auto& row : v_weights_[l])
            f.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size() * sizeof(double)));
        f.read(reinterpret_cast<char*>(m_biases_[l].data()), static_cast<std::streamsize>(m_biases_[l].size() * sizeof(double)));
        f.read(reinterpret_cast<char*>(v_biases_[l].data()), static_cast<std::streamsize>(v_biases_[l].size() * sizeof(double)));
    }
}

}  // namespace aiminer::ai
