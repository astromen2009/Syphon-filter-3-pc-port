#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sf::psx {

struct RootTimerState {
    std::uint16_t counter{};
    std::uint16_t target{};
    std::uint16_t mode{0x0400U};
    std::uint8_t divider_remainder{};
    bool one_shot_fired{};
    bool pulse_restore_pending{};
    bool gate_active{};
    bool sync_released{};

    bool operator==(const RootTimerState&) const = default;
};

struct RootTimersState {
    std::array<RootTimerState, 3> timers{};

    bool operator==(const RootTimersState&) const = default;
};

class RootTimers final {
public:
    using IrqMask = std::uint8_t;

    static constexpr std::size_t timer_count = 3U;

    RootTimers() noexcept;

    void reset() noexcept;
    [[nodiscard]] const RootTimersState& state() const noexcept { return state_; }
    [[nodiscard]] RootTimersState snapshot() const noexcept { return state_; }
    void restoreState(const RootTimersState& state) noexcept;

    [[nodiscard]] std::uint16_t readCounter(std::size_t channel) const noexcept;
    [[nodiscard]] std::uint16_t readMode(std::size_t channel) noexcept;
    [[nodiscard]] std::uint16_t peekMode(std::size_t channel) const noexcept;
    [[nodiscard]] std::uint16_t readTarget(std::size_t channel) const noexcept;

    void writeCounter(std::size_t channel, std::uint16_t value) noexcept;
    void writeMode(std::size_t channel, std::uint16_t value) noexcept;
    void writeTarget(std::size_t channel, std::uint16_t value) noexcept;

    [[nodiscard]] IrqMask advanceCpuCycles(std::uint64_t cycles) noexcept;
    [[nodiscard]] IrqMask advanceDotClocks(std::uint64_t clocks) noexcept;

    [[nodiscard]] IrqMask setHBlank(bool active) noexcept;
    [[nodiscard]] IrqMask setVBlank(bool active) noexcept;

private:
    [[nodiscard]] bool shouldCount(std::size_t channel) const noexcept;
    [[nodiscard]] IrqMask advanceTimer(
        std::size_t channel,
        std::uint64_t ticks) noexcept;
    [[nodiscard]] IrqMask advanceTimerOne(std::size_t channel) noexcept;
    [[nodiscard]] IrqMask raiseInterruptEvents(
        std::size_t channel,
        std::uint64_t event_count) noexcept;
    void applyGate(std::size_t channel, bool active) noexcept;
    void restorePendingPulses() noexcept;

    RootTimersState state_{};
};

}
