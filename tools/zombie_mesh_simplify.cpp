// Private binary stdin/stdout protocol used by prepare_zombie_lods.py.
// Every output index still addresses an original vertex: rigs are never rewritten.
#include <meshoptimizer.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

template<class T> void read(T* data, std::size_t count) {
    if (!std::cin.read(reinterpret_cast<char*>(data), count * sizeof(T)))
        throw std::runtime_error("Truncated simplification input");
}
int main() {
    try {
        std::uint32_t header[4];
        read(header, 4);
        const auto vertices = header[0], indices = header[1], attributes = header[2], target = header[3];
        if (!vertices || vertices > 10000000 || indices < 3 || indices > 30000000 ||
            indices % 3 || attributes == 0 || attributes > 32 || target < 3 || target > indices)
            throw std::runtime_error("Invalid simplification dimensions");
        float error;
        read(&error, 1);
        std::vector<float> positions(vertices * 3), values(vertices * attributes), weights(attributes);
        std::vector<unsigned int> input(indices), output(indices);
        read(positions.data(), positions.size()); read(values.data(), values.size());
        read(weights.data(), weights.size()); read(input.data(), input.size());
        for (auto index : input) if (index >= vertices) throw std::runtime_error("Index out of range");
        const auto count = meshopt_simplifyWithAttributes(output.data(), input.data(), indices,
            positions.data(), vertices, 3 * sizeof(float), values.data(), attributes * sizeof(float),
            weights.data(), attributes, nullptr, target, error,
            meshopt_SimplifyPermissive | meshopt_SimplifyRegularize, nullptr);
        if (count < 3) throw std::runtime_error("Simplifier removed the entire mesh");
        meshopt_optimizeVertexCache(output.data(), output.data(), count, vertices);
        std::cout.write(reinterpret_cast<const char*>(output.data()), count * sizeof(unsigned int));
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
