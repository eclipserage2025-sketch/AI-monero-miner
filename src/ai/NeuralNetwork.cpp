#include "ai/NeuralNetwork.h"
#include <cmath>
#include <algorithm>

namespace xmrig {

static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }
static double sigmoid_derivative(double x) { return x * (1.0 - x); }

NeuralNetwork::NeuralNetwork(const std::vector<int>& topology, double lr) : m_lr(lr) {
    std::default_random_engine engine(std::random_device{}());
    std::uniform_real_distribution<double> dist(-1.0, 1.0);

    for (size_t i = 0; i < topology.size(); ++i) {
        m_layers.emplace_back();
        auto& layer = m_layers.back();
        layer.neurons.resize(topology[i], 0.0);
        layer.errors.resize(topology[i], 0.0);

        if (i > 0) {
            layer.weights.resize(topology[i], std::vector<double>(topology[i - 1]));
            layer.biases.resize(topology[i], 0.0);
            for (int j = 0; j < topology[i]; ++j) {
                for (int k = 0; k < topology[i - 1]; ++k) {
                    layer.weights[j][k] = dist(engine);
                }
                layer.biases[j] = dist(engine);
            }
        }
    }
}

std::vector<double> NeuralNetwork::forward(const std::vector<double>& inputs) {
    m_layers[0].neurons = inputs;

    for (size_t i = 1; i < m_layers.size(); ++i) {
        auto& prevLayer = m_layers[i - 1];
        auto& currLayer = m_layers[i];

        for (size_t j = 0; j < currLayer.neurons.size(); ++j) {
            double sum = currLayer.biases[j];
            for (size_t k = 0; k < prevLayer.neurons.size(); ++k) {
                sum += currLayer.weights[j][k] * prevLayer.neurons[k];
            }
            currLayer.neurons[j] = sigmoid(sum);
        }
    }
    return m_layers.back().neurons;
}

void NeuralNetwork::backward(const std::vector<double>& targets) {
    auto& outputLayer = m_layers.back();
    for (size_t i = 0; i < outputLayer.neurons.size(); ++i) {
        outputLayer.errors[i] = (targets[i] - outputLayer.neurons[i]) * sigmoid_derivative(outputLayer.neurons[i]);
    }

    for (int i = static_cast<int>(m_layers.size()) - 2; i > 0; --i) {
        auto& currLayer = m_layers[i];
        auto& nextLayer = m_layers[i + 1];

        for (size_t j = 0; j < currLayer.neurons.size(); ++j) {
            double error = 0.0;
            for (size_t k = 0; k < nextLayer.neurons.size(); ++k) {
                error += nextLayer.errors[k] * nextLayer.weights[k][j];
            }
            currLayer.errors[j] = error * sigmoid_derivative(currLayer.neurons[j]);
        }
    }

    for (size_t i = 1; i < m_layers.size(); ++i) {
        auto& currLayer = m_layers[i];
        auto& prevLayer = m_layers[i - 1];

        for (size_t j = 0; j < currLayer.neurons.size(); ++j) {
            for (size_t k = 0; k < prevLayer.neurons.size(); ++k) {
                currLayer.weights[j][k] += m_lr * currLayer.errors[j] * prevLayer.neurons[k];
            }
            currLayer.biases[j] += m_lr * currLayer.errors[j];
        }
    }
}

} // namespace xmrig
