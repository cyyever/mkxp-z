#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace bench {
inline void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
inline std::array<unsigned char,4> tileColor(int x, int y) {
    std::array<unsigned char,4> color{0,0,0,255};
    color[(x + y * 8) % 3] = 255;
    return color;
}
inline std::vector<unsigned char> pixels(int width, int height, const std::string &operation) {
    std::vector<unsigned char> result(size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto color = operation == "clear" ? std::array<unsigned char,4>{0,0,255,255} :
            operation == "blit" ? std::array<unsigned char,4>{
                static_cast<unsigned char>((x * 13 + y * 7) % 256),
                static_cast<unsigned char>((x * 3 + y * 17) % 256),
                static_cast<unsigned char>((x + y * 5) % 256),255} :
            tileColor(x * 8 / width, y * 8 / height);
        std::copy(color.begin(), color.end(), result.begin() + (size_t(y) * width + x) * 4);
    }
    return result;
}
inline uint64_t checksum(std::span<const unsigned char> bytes) {
    uint64_t value = 14695981039346656037ull;
    for (const auto byte : bytes) { value ^= byte; value *= 1099511628211ull; }
    return value;
}
template<class Operation>
void measure(const char *backend, const std::string &driver, const std::string &adapter,
             const std::string &version, const std::string &operation,
             int width, int height, int samples, int iterations,
             uint64_t hash, Operation execute) {
    for (int i = 0; i < 5; ++i) execute(iterations);
    std::vector<double> times;
    for (int sample = 0; sample < samples; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        execute(iterations);
        times.push_back(std::chrono::duration<double,std::micro>(
            std::chrono::steady_clock::now() - start).count() / iterations);
    }
    auto sorted = times;
    std::sort(sorted.begin(), sorted.end());
    const double median = (sorted[(sorted.size()-1)/2] + sorted[sorted.size()/2]) / 2;
    const double p95 = sorted[size_t(std::ceil(.95 * sorted.size())) - 1];
    std::cout << "{\"backend\":" << std::quoted(backend)
              << ",\"driver\":" << std::quoted(driver) << ",\"adapter\":" << std::quoted(adapter)
              << ",\"version\":" << std::quoted(version) << ",\"operation\":" << std::quoted(operation)
              << ",\"width\":" << width << ",\"height\":" << height
              << ",\"iterations_per_sample\":" << iterations << ",\"warmup_samples\":5"
              << ",\"pixel_checksum\":" << std::quoted(std::to_string(hash))
              << ",\"median_us\":" << median << ",\"p95_us\":" << p95 << ",\"samples_us\":[";
    for (size_t i = 0; i < times.size(); ++i) std::cout << (i ? "," : "") << times[i];
    std::cout << "]}\n" << std::flush;
}
}
