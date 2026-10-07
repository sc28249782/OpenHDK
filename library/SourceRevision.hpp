// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <span>

namespace OpenHDK {
struct SourceRevision {
  std::array<std::uint8_t, 32> sha256{};
  std::uint64_t byteCount = 0U;
  bool operator==(const SourceRevision&) const = default;
};

// Independently implemented from NIST FIPS 180-4, sections 5 and 6.2.
// Content comparison only: no authentication or FIPS validation claim.
inline SourceRevision sourceRevision(std::span<const std::uint8_t> bytes) noexcept {
  constexpr std::array<std::uint32_t, 64> k = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  std::array<std::uint32_t, 8> h = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                  0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  const auto block = [&](const std::array<std::uint8_t, 64>& input) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0U; i < 16U; ++i) {
      for (std::size_t j = 0U; j < 4U; ++j) w[i] = (w[i] << 8U) | input[i * 4U + j];
    }
    for (std::size_t i = 16U; i < 64U; ++i) {
      const auto a = w[i - 15U], b = w[i - 2U];
      w[i] = w[i - 16U] + (std::rotr(a,7) ^ std::rotr(a,18) ^ (a >> 3U)) +
             w[i - 7U] + (std::rotr(b,17) ^ std::rotr(b,19) ^ (b >> 10U));
    }
    auto a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], q=h[7];
    for (std::size_t i = 0U; i < 64U; ++i) {
      const auto t1 = q + (std::rotr(e,6) ^ std::rotr(e,11) ^ std::rotr(e,25)) +
                      ((e & f) ^ (~e & g)) + k[i] + w[i];
      const auto t2 = (std::rotr(a,2) ^ std::rotr(a,13) ^ std::rotr(a,22)) +
                      ((a & b) ^ (a & c) ^ (b & c));
      q=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=q;
  };
  std::size_t offset = 0U;
  while (bytes.size() - offset >= 64U) {
    std::array<std::uint8_t, 64> input{};
    for (std::size_t i = 0U; i < 64U; ++i) input[i] = bytes[offset + i];
    block(input); offset += 64U;
  }
  std::array<std::uint8_t, 64> tail{};
  const auto remainder = bytes.size() - offset;
  for (std::size_t i = 0U; i < remainder; ++i) tail[i] = bytes[offset + i];
  tail[remainder] = 0x80U;
  if (remainder >= 56U) { block(tail); tail.fill(0U); }
  const auto bits = static_cast<std::uint64_t>(bytes.size()) * 8U;
  for (std::size_t i = 0U; i < 8U; ++i) tail[63U - i] = static_cast<std::uint8_t>(bits >> (8U * i));
  block(tail);
  SourceRevision result{{}, static_cast<std::uint64_t>(bytes.size())};
  for (std::size_t i = 0U; i < 32U; ++i) result.sha256[i] = static_cast<std::uint8_t>(h[i / 4U] >> (24U - 8U * (i % 4U)));
  return result;
}
}  // namespace OpenHDK
