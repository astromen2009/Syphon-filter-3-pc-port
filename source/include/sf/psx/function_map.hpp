#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sf::psx {

struct FunctionCandidate {
    std::uint32_t address{};
    std::size_t static_call_count{};
};

[[nodiscard]] std::vector<FunctionCandidate> discoverFunctionCandidates(
    std::span<const std::byte> text,
    std::uint32_t load_address,
    std::uint32_t entry_point);

}
