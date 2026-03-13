#pragma once

#include <cstddef>
#include <random>
#include <vector>

namespace aiminer::ai {

/// A lightweight, fully-connected feedforward neural network trained online
/// using the Adam optimizer with gradient clipping.
class NeuralNet {
public:
    /// Construct with layer sizes, e.g. {8, 64, 32, 3}
    explicit NeuralNet(const std::vector<int>& layer_sizes,
                       double learning_rate = 0.001,
                       double beta1 = 0.9,
                       double beta2 = 0.999);

    /// Forward pass: returns output vector
    std::vector<double> forward(const std::vector<double>& input);

    /// Backpropagation with MSE loss against a target vector (Adam update)
    double train(const std::vector<double>& input, const std::vector<double>& target);

    /// Getters
    size_t input_size()  const { return layers_.front(); }
    size_t output_size() const { return layers_.back(); }

    /// Save / Load weights
    void save(const std::string& path) const;
    void load(const std::string& path);

private:
    double activate(double x) const;       // Leaky ReLU
    double activate_deriv(double x) const;
    double activate_output(double x) const; // Sigmoid for output layer

    std::vector<int> layers_;
    double lr_;
    double beta1_;
    double beta2_;
    static constexpr double epsilon_ = 1e-8;
    static constexpr double max_grad_norm_ = 1.0;
    int timestep_ = 0;

    // weights_[l][j][i]  = weight from neuron i in layer l to neuron j in layer l+1
    std::vector<std::vector<std::vector<double>>> weights_;
    std::vector<std::vector<double>> biases_;

    // Adam first moment estimates (same shape as weights/biases)
    std::vector<std::vector<std::vector<double>>> m_weights_;
    std::vector<std::vector<double>> m_biases_;

    // Adam second moment estimates
    std::vector<std::vector<std::vector<double>>> v_weights_;
    std::vector<std::vector<double>> v_biases_;

    // Cached activations for backprop
    std::vector<std::vector<double>> activations_;
    std::vector<std::vector<double>> z_values_;

    std::mt19937 rng_{42};
};

}  // namespace aiminer::ai
