#pragma once

#include "sf/psx/spu.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf::psx {

enum class VabDecodeStatus : std::uint8_t {
  decoded,
  invalid_header,
  invalid_sound,
  unsupported_sound,
  invalid_sample,
};

struct VabDecodeResult {
  VabDecodeStatus status{VabDecodeStatus::invalid_header};
  std::uint16_t vab_id{};
  std::uint16_t program{};
  std::uint16_t sample{};
  std::uint16_t pitch{};
  std::uint16_t volume_left{};
  std::uint16_t volume_right{};
  std::vector<SpuPcmFrame> frames;

  [[nodiscard]] bool succeeded() const noexcept {
    return status == VabDecodeStatus::decoded;
  }
};

[[nodiscard]] VabDecodeResult
decodeVabSound(std::span<const std::byte> header,
               std::span<const std::byte> body,
               std::size_t sound_index);

}
