#include <torch/torch.h>

struct RegisteredNetwork final : torch::nn::Module {
    RegisteredNetwork() {
        embedding = register_module("embedding", torch::nn::Embedding(8, 4));
        encoder = register_module("encoder", torch::nn::GRU(torch::nn::GRUOptions(4, 4).num_layers(1)));
    }
    torch::nn::Embedding embedding{nullptr};
    torch::nn::GRU encoder{nullptr};
};

int main() {
    const auto value = torch::ones({1}, torch::TensorOptions().dtype(torch::kFloat));
    if (value.item<float>() != 1.0f) return 1;
    auto embedding = torch::nn::Embedding(torch::nn::EmbeddingOptions(8, 4));
    auto gru = torch::nn::GRU(torch::nn::GRUOptions(4, 4).num_layers(1));
    const auto ids = torch::tensor(std::vector<std::int64_t>{1},
                                   torch::TensorOptions().dtype(torch::kInt64)).reshape({1, 1});
    const auto output = gru->forward(embedding->forward(ids));
    if (std::get<0>(output).numel() != 4) return 1;
    auto network = std::make_shared<RegisteredNetwork>();
    network->eval();
    const auto registeredOutput = network->encoder->forward(network->embedding->forward(ids));
    return std::get<0>(registeredOutput).numel() == 4 ? 0 : 1;
}
