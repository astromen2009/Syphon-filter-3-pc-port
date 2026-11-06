#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace sf::psx {

class CdRomMedia {
public:
  static constexpr std::size_t sector_size = 2048U;
  static constexpr std::size_t raw_sector_size = 2352U;

  virtual ~CdRomMedia() = default;
  [[nodiscard]] virtual std::uint32_t sectorCount() const noexcept = 0;
  [[nodiscard]] virtual bool
  readDataSector(std::uint32_t lba,
                 std::span<std::byte, sector_size> destination) noexcept = 0;

  [[nodiscard]] virtual bool
  readRawSector(std::uint32_t lba,
                std::span<std::byte, raw_sector_size> destination) noexcept;
};

class CdRomXaAudioSink {
public:
  virtual ~CdRomXaAudioSink() = default;
  virtual void consumeXaSector(
      std::span<const std::byte, CdRomMedia::raw_sector_size> sector,
      bool muted) noexcept = 0;

  virtual void resetXaStream() noexcept {}
  virtual void
  setXaOutputMixer(std::array<std::uint8_t, 4U>) noexcept {}
};

enum class CdRomCommandPhase : std::uint8_t {
  idle,
  execute,
  complete,
};

struct CdRomEventSchedule {
  std::uint64_t generation{1U};
  std::uint32_t delay_ticks{};
  std::uint8_t pending{};

  bool operator==(const CdRomEventSchedule &) const = default;
};

struct CdRomState {
  static constexpr std::size_t fifo_capacity = 16U;
  static constexpr std::size_t raw_sector_size = 2340U;

  std::array<std::uint8_t, fifo_capacity> parameters{};
  std::array<std::uint8_t, fifo_capacity> response{};
  std::array<std::byte, raw_sector_size> data{};

  std::array<std::uint8_t, 4U> cd_volume_matrix{0x80U, 0U, 0U, 0x80U};
  std::array<std::uint8_t, 4U> pending_cd_volume_matrix{0x80U, 0U, 0U,
                                                        0x80U};
  CdRomEventSchedule command_event{};
  CdRomEventSchedule sector_event{};

  std::uint32_t target_lba{};
  std::uint32_t current_lba{};
  std::uint16_t data_begin{};
  std::uint16_t data_position{};
  std::uint16_t data_end{};

  std::uint8_t parameter_count{};
  std::uint8_t response_position{};
  std::uint8_t response_count{};
  std::uint8_t index{};
  std::uint8_t mode{0x20U};
  std::uint8_t filter_file{};
  std::uint8_t filter_channel{};
  std::uint8_t xa_current_file{};
  std::uint8_t xa_current_channel{};
  std::uint8_t xa_current_set{};
  std::uint8_t interrupt_enable{};
  std::uint8_t interrupt_flags{};
  std::uint8_t pending_command{};
  CdRomCommandPhase command_phase{CdRomCommandPhase::idle};

  std::uint8_t motor_on{};
  std::uint8_t reading{};
  std::uint8_t seeking{};
  std::uint8_t muted{};
  std::uint8_t adpcm_muted{};
  std::uint8_t setloc_pending{};
  std::uint8_t data_valid{};
  std::uint8_t data_requested{};

  bool operator==(const CdRomState &) const = default;
};

class CdRomController final {
public:
  static constexpr std::uint32_t register_span = 4U;
  static constexpr std::uint32_t cpu_clock_hz = 33'868'800U;
  static constexpr std::uint32_t command_delay_ticks = 25'000U;
  static constexpr std::uint32_t init_delay_ticks = 80'000U;
  static constexpr std::uint32_t interrupt_retry_ticks = 1'000U;
  static constexpr std::uint32_t short_completion_ticks = 0x00001e00U;
  static constexpr std::uint32_t seek_completion_ticks =
      2U * (cpu_clock_hz / 75U);
  static constexpr std::uint32_t pause_single_speed_ticks = 0x0021181cU;
  static constexpr std::uint32_t pause_double_speed_ticks = 0x0010bd93U;
  static constexpr std::uint32_t stop_single_speed_ticks = 0x00d38acaU;
  static constexpr std::uint32_t stop_double_speed_ticks = 0x018a6076U;
  static constexpr std::uint32_t sector_single_speed_ticks = cpu_clock_hz / 75U;
  static constexpr std::uint32_t sector_double_speed_ticks =
      cpu_clock_hz / 150U;

  inline static std::uint32_t data_speedup = 1U;
  inline static std::uint64_t commands_written = 0;
  inline static std::uint64_t commands_executed = 0;

  explicit CdRomController(CdRomMedia *media = nullptr) noexcept;

  void reset() noexcept;
  void setMedia(CdRomMedia *media) noexcept;
  void setXaAudioSink(CdRomXaAudioSink *sink) noexcept;
  [[nodiscard]] CdRomMedia *media() const noexcept { return media_; }

  [[nodiscard]] bool readRegister(std::uint32_t offset,
                                  std::uint8_t &value) noexcept;
  [[nodiscard]] bool writeRegister(std::uint32_t offset,
                                   std::uint8_t value) noexcept;

  [[nodiscard]] bool interruptLine() const noexcept;
  [[nodiscard]] bool dmaRequest() const noexcept;
  [[nodiscard]] bool canReadDmaWords(std::uint64_t word_count) const noexcept;
  [[nodiscard]] bool readDmaWord(std::uint32_t &value) noexcept;

  [[nodiscard]] CdRomEventSchedule commandSchedule() const noexcept {
    return state_.command_event;
  }
  [[nodiscard]] CdRomEventSchedule sectorSchedule() const noexcept {
    return state_.sector_event;
  }
  void eventCommand(std::uint64_t generation) noexcept;
  void eventSector(std::uint64_t generation) noexcept;
  void eventCommand() noexcept;
  void eventSector() noexcept;

  [[nodiscard]] std::uint8_t mode() const noexcept { return state_.mode; }
  [[nodiscard]] std::uint8_t filterFile() const noexcept {
    return state_.filter_file;
  }
  [[nodiscard]] std::uint8_t filterChannel() const noexcept {
    return state_.filter_channel;
  }
  [[nodiscard]] std::uint32_t currentLba() const noexcept {
    return state_.current_lba;
  }
  [[nodiscard]] bool reading() const noexcept { return state_.reading != 0U; }
  [[nodiscard]] bool muted() const noexcept { return state_.muted != 0U; }

  [[nodiscard]] CdRomState captureState() const noexcept { return state_; }
  [[nodiscard]] bool validateState(const CdRomState &state) const noexcept;
  [[nodiscard]] bool restoreState(const CdRomState &state) noexcept;

private:
  static constexpr std::uint8_t interrupt_data_ready = 1U;
  static constexpr std::uint8_t interrupt_complete = 2U;
  static constexpr std::uint8_t interrupt_acknowledge = 3U;
  static constexpr std::uint8_t interrupt_error = 5U;

  [[nodiscard]] static std::uint64_t
  nextGeneration(std::uint64_t generation) noexcept;
  static void armEvent(CdRomEventSchedule &event,
                       std::uint32_t delay_ticks) noexcept;
  static void cancelEvent(CdRomEventSchedule &event) noexcept;
  [[nodiscard]] static bool consumeEvent(CdRomEventSchedule &event,
                                         std::uint64_t generation) noexcept;

  [[nodiscard]] std::uint8_t statusByte(bool error = false) const noexcept;
  [[nodiscard]] std::uint32_t commandCompletionDelay() const noexcept;
  [[nodiscard]] std::uint32_t sectorDelay() const noexcept;
  [[nodiscard]] bool parametersMatch(std::uint8_t count) const noexcept;
  [[nodiscard]] bool decodeSetloc(std::uint32_t &lba) const noexcept;

  void writeCommand(std::uint8_t command) noexcept;
  void executeCommand() noexcept;
  void completeCommand() noexcept;
  void acknowledgeInterrupt(std::uint8_t value) noexcept;
  void scheduleUnblockedWork() noexcept;

  void clearParameters() noexcept;
  void clearResponse() noexcept;
  void clearData() noexcept;
  void resetXaStream() noexcept;
  void setResponse(std::uint8_t interrupt,
                   std::span<const std::uint8_t> bytes) noexcept;
  void setStatusResponse(std::uint8_t interrupt) noexcept;
  void setErrorResponse(std::uint8_t code) noexcept;
  [[nodiscard]] std::uint8_t readResponseByte() noexcept;
  [[nodiscard]] std::uint8_t readDataByte() noexcept;
  void finishDataReadIfNeeded() noexcept;
  enum class SectorLoadResult : std::uint8_t {
    error,
    data,
    xa_audio,
  };

  [[nodiscard]] SectorLoadResult loadSector() noexcept;

  CdRomMedia *media_{};
  CdRomXaAudioSink *xa_audio_sink_{};
  CdRomState state_{};
};

}
