// Private little-endian protocol for static vehicle preparation. Unlike the
// rig-preserving zombie helper, this may adjust positions/UVs for better LoDs.
#include <meshoptimizer.h>
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
        std::uint32_t header[5];
        read(header, 5);
        const auto flags=header[4];
        const auto vertices=header[0], indices=header[1], attributes=header[2], target=header[3];
        if (flags>1 || !vertices || vertices>10000000 || indices<3 || indices>30000000 ||
            indices%3 || !attributes || attributes>32 || target<3 || target>indices)
            throw std::runtime_error("Invalid simplification dimensions");
        float error;
        read(&error, 1);
        std::vector<float> positions(vertices*3), values(vertices*attributes), weights(attributes);
        std::vector<unsigned int> output(indices);
        read(positions.data(), positions.size());
        read(values.data(), values.size());
        read(weights.data(), weights.size());
        read(output.data(), output.size());
        std::vector<unsigned char> locks;
        if (flags) { locks.resize(vertices); read(locks.data(), locks.size()); }
        for (auto index:output) if (index>=vertices)
            throw std::runtime_error("Index out of range");
        const auto count=meshopt_simplifyWithUpdate(output.data(), indices, positions.data(), vertices,
            3*sizeof(float), values.data(), attributes*sizeof(float), weights.data(), attributes, locks.empty() ? nullptr : locks.data(),
            target, error, meshopt_SimplifyPermissive|meshopt_SimplifyRegularizeLight|
                (flags ? meshopt_SimplifyLockBorder : 0), nullptr);
        if (count<3) throw std::runtime_error("Simplifier removed the entire mesh");
        meshopt_optimizeVertexCache(output.data(), output.data(), count, vertices);
        const auto result=static_cast<std::uint32_t>(count);
        std::cout.write(reinterpret_cast<const char*>(&result), sizeof(result));
        std::cout.write(reinterpret_cast<const char*>(positions.data()), positions.size()*sizeof(float));
        std::cout.write(reinterpret_cast<const char*>(values.data()), values.size()*sizeof(float));
        std::cout.write(reinterpret_cast<const char*>(output.data()), count*sizeof(unsigned int));
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
