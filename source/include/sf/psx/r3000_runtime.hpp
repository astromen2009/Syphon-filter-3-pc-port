#pragma once

#include "sf/psx/executable.hpp"
#include "sf/psx/gte_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sf::psx {

enum class R3000StopReason {
    running,
    returned,
    instruction_budget,
    unsupported_instruction,
    memory_fault,
    alignment_fault,
    arithmetic_overflow,
    syscall,
    breakpoint,
};

[[nodiscard]] std::string_view toString(R3000StopReason reason) noexcept;

struct R3000RunResult {
    R3000StopReason reason{R3000StopReason::running};
    std::uint64_t instructions{};
    std::uint32_t pc{};
    std::uint32_t instruction{};
};

enum class R3000AccessWidth : std::uint8_t {
    byte = 1U,
    halfword = 2U,
    word = 4U,
};

class R3000MmioBus {
public:
    virtual ~R3000MmioBus() = default;
    [[nodiscard]] virtual bool readMmio(
        std::uint32_t physical_address,
        R3000AccessWidth width,
        std::uint32_t& value) noexcept = 0;
    [[nodiscard]] virtual bool writeMmio(
        std::uint32_t physical_address,
        R3000AccessWidth width,
        std::uint32_t value) noexcept = 0;
};

struct R3000DelayedLoadState {
    std::uint8_t reg{};
    std::uint32_t value{};
    bool valid{};
};

struct R3000State {
    std::array<std::uint32_t, 32> gpr{};
    GteState gte{};
    std::uint32_t cop0_status{};
    std::uint32_t cop0_cause{};
    std::uint32_t cop0_epc{};
    std::uint32_t cop0_bad_vaddr{};
    std::uint32_t hi{};
    std::uint32_t lo{};
    std::uint32_t pc{};
    std::uint32_t next_pc{};
    std::uint32_t branch_pc{};
    bool branch_delay_slot{};
    R3000DelayedLoadState load_delay{};
    R3000DelayedLoadState next_load_delay{};
};

class R3000Runtime final {
public:
    static constexpr std::size_t ram_size = 2U * 1024U * 1024U;

    static constexpr std::uint32_t extra_ram_base = 0x00700000U;
    static constexpr std::size_t extra_ram_size = 0x70000U;

    static constexpr std::uint32_t arms_ram_base = 0x00600000U;
    static constexpr std::size_t arms_ram_size = 0x100000U;
    static constexpr std::uint32_t ramIndex(std::uint32_t physical) noexcept {
        return (physical - extra_ram_base) < extra_ram_size ? std::uint32_t(ram_size) + (physical - extra_ram_base)
             : (physical - arms_ram_base) < arms_ram_size ? std::uint32_t(ram_size + extra_ram_size) + (physical - arms_ram_base)
             : (physical & std::uint32_t(ram_size - 1U));
    }
    static constexpr std::size_t scratchpad_size = 1024U;
    static constexpr std::size_t mmio_size = 4U * 1024U;
    static constexpr std::uint32_t return_sentinel = 0xfffffff0U;

    R3000Runtime();

    void clearMemory() noexcept;
    void loadExecutable(const Executable& executable);
    [[nodiscard]] bool loadBytes(
        std::uint32_t address,
        std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] bool copyBytes(
        std::uint32_t address,
        std::span<std::byte> destination) const noexcept;
    [[nodiscard]] bool restoreRam(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] bool restoreScratchpad(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] bool restoreMmio(std::span<const std::byte> bytes) noexcept;

    void reset(std::uint32_t pc, std::uint32_t gp = 0U, std::uint32_t sp = 0U) noexcept;
    void restoreCpuState(const R3000State& state) noexcept;
    [[nodiscard]] bool beginCall(
        std::uint32_t address,
        std::span<const std::uint32_t> arguments = {}) noexcept;
    void completeHostCall() noexcept;
    void settleLoadDelay() noexcept;
    void setRegister(std::uint8_t reg, std::uint32_t value) noexcept;
    void attachMmioBus(R3000MmioBus* bus) noexcept { mmio_bus_ = bus; }
    void setExternalInterrupt(bool active) noexcept;

    [[nodiscard]] bool interruptPending() const noexcept;

    [[nodiscard]] bool atReturnSentinel() const noexcept {
        return state_.pc == return_sentinel;
    }

    [[nodiscard]] R3000RunResult step() noexcept;
    [[nodiscard]] R3000RunResult call(
        std::uint32_t address,
        std::span<const std::uint32_t> arguments = {},
        std::uint64_t instruction_budget = 1'000'000U) noexcept;

    [[nodiscard]] bool read8(std::uint32_t address, std::uint8_t& value) const noexcept;
    [[nodiscard]] bool read16(std::uint32_t address, std::uint16_t& value) const noexcept;
    [[nodiscard]] bool read32(std::uint32_t address, std::uint32_t& value) const noexcept;
    [[nodiscard]] bool write8(std::uint32_t address, std::uint8_t value) noexcept;
    [[nodiscard]] bool write16(std::uint32_t address, std::uint16_t value) noexcept;
    [[nodiscard]] bool write32(std::uint32_t address, std::uint32_t value) noexcept;

    [[nodiscard]] const R3000State& state() const noexcept { return state_; }
    [[nodiscard]] std::span<const std::byte> ram() const noexcept { return std::span<const std::byte>(ram_).first(ram_size); }
    [[nodiscard]] std::span<const std::byte> scratchpad() const noexcept {
        return scratchpad_;
    }
    [[nodiscard]] std::span<const std::byte> mmio() const noexcept { return mmio_; }

    [[nodiscard]] std::span<const std::byte> extraRam() const noexcept { return std::span<const std::byte>(ram_).subspan(ram_size, extra_ram_size); }

    [[nodiscard]] std::span<std::byte> armsRam() noexcept { return std::span<std::byte>(ram_).subspan(ram_size + extra_ram_size, arms_ram_size); }
    [[nodiscard]] std::span<const std::byte> armsRam() const noexcept { return std::span<const std::byte>(ram_).subspan(ram_size + extra_ram_size, arms_ram_size); }
    bool restoreExtraRam(std::span<const std::byte> bytes) noexcept {
        if (bytes.size() != extra_ram_size) return false;
        std::copy(bytes.begin(), bytes.end(), ram_.begin() + std::ptrdiff_t(ram_size));
        return true;
    }

private:
    [[nodiscard]] std::byte* memoryByte(std::uint32_t address) noexcept;
    [[nodiscard]] const std::byte* memoryByte(std::uint32_t address) const noexcept;
    [[nodiscard]] static bool physicalAddress(
        std::uint32_t address,
        std::uint32_t& physical) noexcept;
    [[nodiscard]] bool readMmio(
        std::uint32_t address,
        R3000AccessWidth width,
        std::uint32_t& value) const noexcept;
    [[nodiscard]] bool writeMmio(
        std::uint32_t address,
        R3000AccessWidth width,
        std::uint32_t value) noexcept;
    void writeRegister(std::uint8_t reg, std::uint32_t value) noexcept;
    void scheduleLoad(std::uint8_t reg, std::uint32_t value) noexcept;
    void advanceLoadDelay() noexcept;
    void flushLoadDelay() noexcept;
    void clearLoadDelay() noexcept;
    void takeInterrupt() noexcept;

    std::vector<std::byte> ram_;
    std::array<std::byte, scratchpad_size> scratchpad_{};
    std::array<std::byte, mmio_size> mmio_{};
    R3000State state_{};
    R3000MmioBus* mmio_bus_{};
};

}
