#ifndef XMRIG_AI_NEURALNETWORK_H
#define XMRIG_AI_NEURALNETWORK_H

#include <vector>
#include <random>

namespace xmrig {

class NeuralNetwork {
public:
    NeuralNetwork(const std::vector<int>& topology, double lr = 0.01);

    std::vector<double> forward(const std::vector<double>& inputs);
    void backward(const std::vector<double>& targets);

private:
    struct Layer {
        std::vector<double> neurons;
        std::vector<double> errors;
        std::vector<std::vector<double>> weights;
        std::vector<double> biases;
    };

    std::vector<Layer> m_layers;
    double m_lr;
};

} // namespace xmrig

#endif // XMRIG_AI_NEURALNETWORK_H
