#include <rex/graphics/ht_float_constant_copy.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>

using Map = std::array<uint64_t, 4>;
using Copy = uint8_t* (*)(uint8_t*, const uint32_t*, const uint64_t*);
std::array<uint32_t, 1024> registers;

void Check(const Map& map) {
  std::array<uint8_t, 4128> a, b;
  a.fill(0xA5); b.fill(0xA5);
  auto* ae = rex::graphics::ht::CopyFloatConstantsScalar(a.data() + 16, registers.data(), map.data());
  auto* be = rex::graphics::ht::CopyFloatConstantsRuns(b.data() + 16, registers.data(), map.data());
  size_t expected = 0;
  for (auto word : map) expected += std::popcount(word) * 16;
  if (size_t(ae - a.data()) != expected + 16 || size_t(be - b.data()) != expected + 16 || a != b)
    throw std::runtime_error("Packed data, bounds or length differs from scalar reference");
}

double Time(Copy copy, const Map& map) {
  std::array<uint8_t, 4096> output;
  uint64_t sink = 0;
  auto start = std::chrono::steady_clock::now();
  constexpr int iterations = 2000000;
  for (int i = 0; i < iterations; ++i) {
    registers[0] += 1;
    auto* end = copy(output.data(), registers.data(), map.data());
    sink += output[0] + (end - output.data());
  }
  double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / iterations;
  std::printf(" sink=%llu", static_cast<unsigned long long>(sink));
  return ns;
}

int main() {
  std::mt19937_64 random(0x5841096A);
  for (auto& value : registers) value = uint32_t(random());
  Check({0, 0, 0, 0}); Check({~0ull, ~0ull, ~0ull, ~0ull});
  Check({0x5555555555555555, 0xAAAAAAAAAAAAAAAA, 1, 1ull << 63});
  for (int word = 0; word < 4; ++word)
    for (int first = 0; first < 64; ++first)
      for (int count = 1; count <= 64 - first; ++count) {
        Map map{};
        map[word] = count == 64 ? ~0ull : ((1ull << count) - 1) << first;
        Check(map);
      }
  for (int i = 0; i < 100000; ++i) Check({random(), random(), random(), random()});
  std::puts("108323 maps passed: exact bytes, output length and guard bytes");
  const std::array<Map, 3> cases = {{
    {~0ull, ~0ull, 0, 0},
    {0x5555555555555555, 0xAAAAAAAAAAAAAAAA, 0, 0},
    {0xFFFF00000000FFFF, 0xFFFFFF, 0, 0}
  }};
  for (const auto& map : cases) {
    double scalar = Time(rex::graphics::ht::CopyFloatConstantsScalar, map);
    double runs = Time(rex::graphics::ht::CopyFloatConstantsRuns, map);
    std::printf(" scalar=%.1f ns runs=%.1f ns ratio=%.3f\n", scalar, runs, runs/scalar);
  }
}
