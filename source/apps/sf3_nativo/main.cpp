// Syphon Filter 3 PC: arranque, emulacion del R3000 y enlace con el juego original.
#ifndef SF3_INI
#define SF3_INI "sf3_nativo.ini"
#define SF3_PARTIDAS "partidas"
#define SF3_REGISTRO "sf3_nativo_registro.txt"
#endif
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace sf::psx { extern std::vector<std::byte> g_spu_ext; extern std::array<std::uint32_t, 256> g_spu_redirect; }
#include <optional>
#include <cstdint>
namespace sf::psx { extern void (*g_read_watch)(std::uint32_t, std::uint32_t, std::uint32_t); extern std::uint32_t g_read_watch_lo, g_read_watch_hi; extern std::uint64_t g_spu_log_field; extern void (*g_cd_sector_patch)(std::uint32_t, unsigned char*); extern std::uint32_t g_dbg_pc, g_dbg_ra, g_dbg_regs[8], g_dbg_calls[1024], g_dbg_args[1024], g_dbg_ncall; }
#include <ctime>
#include <atomic>
#include <functional>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include "sf/disc/iso9660.hpp"
#include "voice_pitch.hpp"
#include "sf/psx/executable.hpp"
#include "sf/psx/machine.hpp"
#include "sf/psx/r3000_runtime.hpp"
#include "soft_gpu.hpp"
#include "mdec.hpp"
#include "vfs.hpp"
#include "roster.hpp"
#include "launcher_win.hpp"
#include "voces_es.hpp"
#include "textos_es.hpp"
#include "videos_es.hpp"
#define SF3B_SDL 1
#include "native_window.hpp"
#include <filesystem>
#include <memory>
static SoftMdec* g_mdec = nullptr;
static int g_trace = 0;

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <tuple>
#include <type_traits>
#include <set>
#include <string>
#include <vector>

using namespace sf::psx;

namespace sf::psx { extern std::uint32_t sf3_watch_dyn; }
namespace {

FILE* g_log = stderr;
#define LOG(...) std::fprintf(g_log, __VA_ARGS__)
static std::vector<std::string> g_bank_init_order, g_bank_v192_order;

class BinMedia final : public CdRomMedia {
public:
    explicit BinMedia(const std::string& path) : f_(path, std::ios::binary) {
        f_.seekg(0, std::ios::end);
        sectors_ = std::uint32_t(f_.tellg() / 2352);
    }
    std::uint32_t sectorCount() const noexcept override { return std::max(sectors_, virtualDisc().end); }
    std::uint32_t discSectors() const noexcept { return sectors_; }
    bool readDataSector(std::uint32_t lba, std::span<std::byte, sector_size> d) noexcept override {
        if (lba >= sectors_) return virtualDisc().readData(lba, d);
        std::array<std::byte, raw_sector_size> raw;
        if (!readRawSector(lba, raw)) return false;
        std::memcpy(d.data(), raw.data() + 24, sector_size);
        return true;
    }
    bool readRawSector(std::uint32_t lba, std::span<std::byte, raw_sector_size> d) noexcept override {
        if (lba >= sectors_) return virtualDisc().readRaw(lba, d);
        if (lbaLog.size() < 4000) lbaLog.push_back(lba);
        f_.clear();
        f_.seekg(std::streamoff(lba) * 2352);
        f_.read(reinterpret_cast<char*>(d.data()), 2352);
        ++reads;
        return bool(f_);
    }
    std::uint64_t reads{};
    std::vector<std::uint32_t> lbaLog;
private:
    std::ifstream f_;
    std::uint32_t sectors_{};
};

struct GameProfile {
    const char* exe;
    bool ntsc;
    std::uint32_t world_room, room_section, actor_rtpt, actor_rtpt_x, actor_begin, actor_begin_x, object, actor_tris_done;
    std::uint32_t idle_frame_limiter, vsync_wait, cd_search_file;
    std::uint32_t aim_delta_aiming, aim_delta_walking, player_pointer;
    std::uint32_t fx_rtps[7];
    std::uint32_t fx_rtpt[5];
    std::uint32_t vis_check;
};
static const GameProfile kProfileUsa{
    "SCUS_946.40", true,
    0x8001e07cu, 0x8001ec1cu, 0x8001b34cu, 0x8001be14u, 0x8001b10cu, 0x8001bc50u, 0x80018d28u, 0x8001b850u,
    0u  , 0x800f7800u, 0x800fa688u,
    0x80054cd0u, 0x80054f30u, 0x8012d890u,
    {0x800107dcu, 0x80010834u, 0x8009c1acu, 0x8009c6c0u, 0x8009c6e8u, 0x80020f48u, 0x80103b1cu},
    {0x8009bfd8u, 0x8009c030u, 0x8009c088u, 0x80020eb0u, 0x80103af0u},
    0x8001adc4u};
static const GameProfile* g_prof = &kProfileUsa;
static std::map<int, std::uint32_t> g_weapon_tone_addr;
static std::vector<std::string> g_cues;
std::string g_idioma;
std::string g_voces;
static VideosEs g_voces_ru;
static double g_voces_ru_vol[3] = {0.76, 0.61, 0.73};
static std::uint32_t g_xa_rng[3][2] = {};
static TextosEs g_textos;

static bool g_movie_skippable = false, g_movie_logo = false;
static VideosEs g_videos;
static std::uint64_t g_idle_skips = 0;
static std::uint64_t g_idle_epoch = 0;

static bool g_list_redraw = false;
constexpr std::uint32_t kMenuLoopPc = 0x80156c80u, kSetScreenPc = 0x80151a08u;
static std::vector<std::uint64_t> g_rep_marks;
static std::uint64_t g_rep_fields = 0, g_rep_start = 0;

static std::uint32_t g_fx_overlay_rtps = 0;
static std::array<std::uint32_t, 4> g_fx_overlay_rtps_all{};

static std::vector<std::uint64_t> g_hook_bits(1u << 13);
static inline bool hookPc(std::uint32_t pc) { const auto i = (pc & 0x1ffffcu) >> 2; return (g_hook_bits[i >> 6] >> (i & 63u)) & 1u; }
static std::uint32_t g_fade_step = 0, g_pause_wait = 0;

static std::uint32_t g_weapon_pick = 0;

static std::uint32_t g_radar_blip = 0;
static double g_radar_exact[2] = {0, 0};
static bool g_radar_have = false;

static std::uint32_t g_radar_arrow = 0;

static std::uint32_t g_radar_cone = 0;

static std::uint32_t g_cam_step_a = 0, g_cam_step_b = 0, g_cam_C = 0;

static std::uint32_t g_radar_fn = 0;
static std::uint64_t g_radar_field = 0;

static std::uint32_t g_camupd_in = 0;

static constexpr std::uint32_t kDbgTramp = 0x80004000u, kDbgTrampRet = kDbgTramp + 4u * 19u;

static constexpr std::uint32_t kOnTramp = 0x80004100u, kOnTrampRet = kOnTramp + 4u * 16u;

static constexpr std::uint32_t kDbgFrameEnd = 0x8002a250u;

static std::uint32_t g_cam_dir = 0;
static std::uint16_t g_pad_pressed = 0;
static std::uint64_t g_aim_path_field = 0;
static inline void addHook(std::uint32_t pc) { if (!pc) return; const auto i = (pc & 0x1ffffcu) >> 2; g_hook_bits[i >> 6] |= 1ull << (i & 63u); }
static void findFxOverlay(std::span<const std::byte> ram) {
    static const std::uint32_t pat[] = {0x8c860004u, 0x8c870000u, 0x00063022u, 0x00063400u, 0x30e7ffffu, 0x00e63825u, 0x48870000u};
    g_fx_overlay_rtps = 0;
    g_fx_overlay_rtps_all = {};
    std::size_t found = 0;
    for (std::size_t a = 0x40000; a + 0x40 < ram.size() && a < 0x1c0000 && found < g_fx_overlay_rtps_all.size(); a += 4) {
        bool ok = true;
        for (std::size_t k = 0; k < 7 && ok; ++k) { std::uint32_t w; std::memcpy(&w, ram.data() + a + 4 * k, 4); ok = w == pat[k]; }
        if (!ok) continue;
        std::uint32_t r; std::memcpy(&r, ram.data() + a + 0x28, 4);
        if (r == 0x4a180001u) {
            g_fx_overlay_rtps_all[found++] = 0x80000000u + std::uint32_t(a) + 0x28;
            if (!g_fx_overlay_rtps) g_fx_overlay_rtps = 0x80000000u + std::uint32_t(a) + 0x28;
        }
    }
}

class CaptureGpu final : public GpuMmio, public DmaPort {
public:
    SoftGpu soft;
    std::uint32_t readGpu(std::uint32_t off) noexcept override {
        if (off == 0) return soft.gpuread();

        std::uint32_t s = 0x1c000000u | (dma_dir_ << 29) | (field_ ? 0x80000000u : 0u);
        if (dma_dir_ == 1 || dma_dir_ == 2) s |= 0x02000000u;
        return s;
    }
    void writeGpu(std::uint32_t off, std::uint32_t v) noexcept override {
        if (off == 0) { gp0(v); return; }
        const auto cmd = v >> 24;
        ++gp1_count;
        soft.gp1(v);
        if (cmd == 0x04) dma_dir_ = v & 3;
        if (cmd == 0x05) { ++display_flips; disp_x = v & 0x3ff; disp_y = (v >> 10) & 0x1ff; publishFrame(); }
        if (cmd == 0x08) display_mode = v;
        if (cmd == 0x03) display_on = (v & 1) == 0;
    }

    bool readDmaWord(std::uint32_t& value) noexcept override { value = soft.gpuread(); ++dma_reads; return true; }
    std::uint64_t dma_reads{};
    bool writeDmaWord(std::uint32_t value) noexcept override { gp0(value); return true; }
    void toggleField() { field_ = !field_; }
    bool idle() const noexcept { return remaining_ == 0; }

    struct Pic { std::uint16_t x = 0, y = 0, w = 0, h = 0; const std::vector<std::uint16_t>* px = nullptr; };
    Pic menu_pic_, menu_clut_;
    bool menu_pic_injecting_ = false;
    std::uint32_t xfer_xy_ = 0, xfer_wh_ = 0;
    void putImage(const Pic& p) {
        if (!p.px || !p.w || !p.h || std::size_t(p.w) * p.h > p.px->size()) return;
        menu_pic_injecting_ = true;
        gp0(0x01000000u);
        gp0(0xa0000000u); gp0(std::uint32_t(p.x) | (std::uint32_t(p.y) << 16)); gp0(std::uint32_t(p.w) | (std::uint32_t(p.h) << 16));
        const std::size_t n = std::size_t(p.w) * p.h;
        for (std::size_t k = 0; k < n; k += 2) gp0(std::uint32_t((*p.px)[k]) | (k + 1 < n ? std::uint32_t((*p.px)[k + 1]) << 16 : 0u));
        menu_pic_injecting_ = false;
    }
    void showMenuPic() { putImage(menu_clut_); putImage(menu_pic_); }
    template <class F> void state(F&& f) {
        soft.state(f);
        f(dma_dir_); f(remaining_); f(image_words_); f(pending_cmd_); f(field_);
        f(display_flips); f(disp_x); f(disp_y); f(display_mode); f(display_on);
    }

    std::vector<std::vector<std::vector<std::uint32_t>>> segs_;
    std::vector<std::uint32_t> rec_pkt_;
    std::uint32_t rec_left_{}, rec_img_{};
    std::vector<std::vector<std::uint32_t>> hud;
    std::function<void(const std::vector<std::vector<std::uint32_t>>&)> on_world;
    std::function<std::vector<std::uint8_t>(const std::vector<std::vector<std::uint32_t>>&)> on_scope;
    std::uint64_t hud_sequence{};
    bool hud_valid{};
    FILE* pkt_log_ = nullptr;
    int area_x0_ = 0, area_y0_ = 0; std::uint32_t off_w_ = 0xe5000000u;
    int xr_drop_ = 0;
    bool xr_on_ = false; std::uint32_t xr_e5_center_ = 0, xr_e5_plain_ = 0, xr_e5_cur_ = 0;
    void recordWord(std::uint32_t w) {
        if (pkt_log_) std::fprintf(pkt_log_, "%08x\n", w);
        if (rec_img_ > 0) { --rec_img_; return; }
        if (rec_left_ > 0) {
            rec_pkt_.push_back(w);
            if (--rec_left_ == 0) finishPacket();
            return;
        }
        const auto cmd = w >> 24;

        if (cmd == 0xe3) { area_x0_ = int(w & 0x3ff); area_y0_ = int((w >> 10) & 0x1ff); xr_on_ = false; }
        else if (cmd == 0xe5) { off_w_ = w; xr_e5_cur_ = w; }
        else if (cmd == 0xe4) {
            const int x1 = int(w & 0x3ff), y1 = int((w >> 10) & 0x1ff);
            static const bool noFix = std::getenv("SF3N_SIN_MIRA_RAYOSX") != nullptr;

            if (!noFix && (off_w_ & 0xffffffu) == 0 && area_x0_ + x1 == 383 && (area_y0_ + y1 == 719 || area_y0_ + y1 == 239) && x1 - area_x0_ < 200 && y1 - area_y0_ < 120) {
                static const int adjX = BootWindow::xrayCfg("rayosx_ajuste_x", "SF3N_RAYOSX_X", 0), adjY = BootWindow::xrayCfg("rayosx_ajuste_y", "SF3N_RAYOSX_Y", 0);
                const int ox = 192 + adjX, oy = 120 + adjY + (area_y0_ >= 240 ? 240 : 0);
                xr_e5_center_ = 0xe5000000u | (std::uint32_t(ox) & 0x7ffu) | ((std::uint32_t(oy) & 0x7ffu) << 11);

                xr_e5_plain_ = 0xe5000000u | ((std::uint32_t(area_y0_ >= 240 ? 240 : 0) & 0x7ffu) << 11);
                xr_on_ = true; xr_e5_cur_ = off_w_;
            }
        }
        rec_pkt_.assign(1, w);
        rec_left_ = length(cmd) - 1;
        if ((cmd & 0xf8) == 0x48 || (cmd & 0xf8) == 0x58) rec_left_ = 0;
        if (rec_left_ == 0) finishPacket();
    }
    void finishPacket() {
        const auto cmd = rec_pkt_[0] >> 24;
        if (cmd >= 0xa0 && cmd < 0xc0 && rec_pkt_.size() >= 3) {
            const auto wh = rec_pkt_[2];
            rec_img_ = ((wh & 0xffff) * (wh >> 16) + 1) / 2;

            if (frame_building_.size() < 30000) frame_building_.push_back({0xa0000000u, rec_pkt_[1], wh});
            return;
        }
        if (cmd == 0xe3 || segs_.empty()) {

            std::vector<std::vector<std::uint32_t>> carry;
            if (!segs_.empty()) {
                auto& prev = segs_.back();
                while (!prev.empty()) {
                    const auto c = prev.back()[0] >> 24;
                    if (c != 0xe5 && c != 0x00) break;
                    carry.insert(carry.begin(), std::move(prev.back()));
                    prev.pop_back();
                }
            }
            closeSegment();
            segs_.emplace_back(std::move(carry));
        }
        auto& seg = segs_.back();
        if (seg.size() < 40000) seg.push_back(rec_pkt_);
        if (frame_building_.size() < 30000) frame_building_.push_back(rec_pkt_);

        if (!frame_vram_taken_ && cmd >= 0x20 && cmd < 0x80) { frame_vram_build_.assign(soft.vram.begin(), soft.vram.end()); frame_vram_taken_ = true; }
    }

    std::vector<std::vector<std::uint32_t>> frame_building_, menu_frame;
    std::uint64_t menu_frame_seq = 0;
    std::vector<std::uint16_t> frame_vram_build_, menu_frame_vram;
    bool frame_vram_taken_ = false;
    void flipFrame() {
        menu_frame.swap(frame_building_); frame_building_.clear(); ++menu_frame_seq;
        if (frame_vram_taken_) menu_frame_vram.swap(frame_vram_build_); else menu_frame_vram.assign(soft.vram.begin(), soft.vram.end());
        frame_vram_taken_ = false;
    }

    std::vector<std::vector<std::uint32_t>> hud_building_;
    bool seen_world_ = false;
    const bool no_dedup_ = std::getenv("SF3N_HUD_SIN_DEDUP") != nullptr;
    const bool dedup_log_ = std::getenv("SF3N_HUDDUPLOG") != nullptr;
    void closeSegment() {
        if (segs_.empty()) return;
        auto& seg = segs_.back();
        int gt = 0;
        for (const auto& p : seg) { const auto c = p[0] >> 24; if (c >= 0x34 && c <= 0x3f && (c & 0x10)) ++gt; }

        {
            int ax0 = -1, ay0 = -1, ax1 = -1, ay1 = -1, polys = 0;
            for (const auto& p : seg) {
                const auto c = p[0] >> 24;
                if (c == 0xe3) { ax0 = int(p[0] & 0x3ff); ay0 = int((p[0] >> 10) & 0x1ff); }
                else if (c == 0xe4) { ax1 = int(p[0] & 0x3ff); ay1 = int((p[0] >> 10) & 0x1ff); }
                else if (c >= 0x20 && c < 0x40) ++polys;
            }
            const bool small = ax0 >= 0 && ax1 > ax0 && ay1 > ay0 && ax1 - ax0 < 200 && ay1 - ay0 < 120 && polys >= 10;
            const bool au300 = small && ax0 + ax1 == 383 && (ay0 + ay1 == 239 || ay0 + ay1 == 719);
            if (small && !au300) { static int n = 0; if (n++ < 20 && g_log) std::fprintf(g_log, "gpu: pasada de area pequena %d,%d-%d,%d (%d poligonos) sin tratar\n", ax0, ay0, ax1, ay1, polys); }
            if (au300 && !std::getenv("SF3N_SIN_MIRA_NATIVA")) {
                auto& sp = nat::scopePic();
                {
                    std::lock_guard<std::mutex> lk(sp.m);
                    sp.w = ax1 - ax0 + 1; sp.h = ay1 - ay0 + 1; sp.x = ax0; sp.y = ay0 % 240;
                    sp.pix.resize(std::size_t(sp.w) * std::size_t(sp.h));
                    for (int y = 0; y < sp.h; ++y) for (int x = 0; x < sp.w; ++x) {
                        const std::uint16_t v = soft.vram[std::size_t(((ay0 + y) & 511) * 1024 + ((ax0 + x) & 1023))];
                        sp.pix[std::size_t(y) * std::size_t(sp.w) + std::size_t(x)] = v ? v : 0x8000u;
                    }
                    sp.pk.assign(seg.begin(), seg.end());
                    sp.actor = on_scope ? on_scope(seg) : std::vector<std::uint8_t>{};
                    if (static int dv = std::getenv("SF3N_RAYOSX_VRAMDUMP") ? std::atoi(std::getenv("SF3N_RAYOSX_VRAMDUMP")) : -1; dv >= 0 && int(sp.seq) == dv) {
                        if (FILE* f = std::fopen("vram_rayosx.bin", "wb")) { std::fwrite(soft.vram.data(), 2, soft.vram.size(), f); std::fclose(f); }
                        if (FILE* f = std::fopen("pk_rayosx.txt", "w")) { for (const auto& q : seg) { for (auto w : q) std::fprintf(f, "%08x ", w); std::fprintf(f, "\n"); } std::fclose(f); }
                    }
                    ++sp.seq;
                    sp.when = std::chrono::steady_clock::now();
                }
                sp.flip = display_flips;
                segs_.clear();
                return;
            }
        }
        if (static const bool sl = std::getenv("SF3N_SEGLOG") != nullptr; sl) {
            std::fprintf(stderr, "SEG %zu gt %d:", seg.size(), gt);
            int shown = 0;
            for (const auto& p : seg) { const auto c = p[0] >> 24; if ((c >= 0x60 && c < 0x80) || (c >= 0x20 && c < 0x30) || c == 0xe1 || c == 0x02) { if (shown++ < 14) { std::fprintf(stderr, " [%08x", p[0]); for (std::size_t k = 1; k < p.size() && k < 3; ++k) std::fprintf(stderr, " %08x", p[k]); std::fprintf(stderr, "]"); } } }
            std::fprintf(stderr, "\n");
        }

        static const bool trasBorrado = std::getenv("SF3N_MUNDO_SOLO_30") == nullptr;
        bool clearSeg = false;
        for (const auto& p : seg) if ((p[0] >> 24) == 0x02 && p.size() >= 3 && (p[2] & 0xffff) >= 256 && (p[2] >> 16) >= 200) clearSeg = true;
        const bool mundoTras = trasBorrado && gt >= 1 && gt < 30 && after_clear_;
        after_clear_ = clearSeg ? true : (gt >= 1 ? false : after_clear_);
        if (gt >= 30 && relaxed_flip_ == display_flips) { hud_building_.clear(); auto keep = std::move(seg); segs_.clear(); if (on_world) on_world(keep); return; }
        if (mundoTras) relaxed_flip_ = display_flips;
        if (gt >= 30 || mundoTras) {
            if (on_world) on_world(seg);
            if (seen_world_) {

                if (!no_dedup_) {
                    std::set<std::vector<std::uint32_t>> seenp;
                    std::vector<std::uint32_t> st(8, 0);
                    std::size_t dups = 0, n0 = hud_building_.size();
                    std::vector<std::vector<std::uint32_t>> out; out.reserve(n0);
                    for (auto& p : hud_building_) {
                        const auto c = p[0] >> 24;
                        if (c >= 0xe1 && c <= 0xe6) { st[c - 0xe0] = p[0]; out.push_back(std::move(p)); continue; }
                        if (c < 0x20 || c >= 0x80) { out.push_back(std::move(p)); continue; }
                        std::vector<std::uint32_t> key(st.begin(), st.end()); key.insert(key.end(), p.begin(), p.end());
                        if (!seenp.insert(std::move(key)).second) { ++dups; continue; }
                        out.push_back(std::move(p));
                    }
                    hud_building_.swap(out);
                    if (dups && dedup_log_) std::fprintf(stderr, "HUDDUP seq %llu quitados %zu de %zu\n", (unsigned long long)hud_sequence, dups, n0);
                }
                hud.swap(hud_building_); hud_valid = true; ++hud_sequence;
                if (std::getenv("SF3N_HUDLOG") && hud_sequence % 7 == 0) { int sp = 0, fl = 0; for (auto& p : hud) { auto c = p[0] >> 24; if (c >= 0x60 && c < 0x80) ++sp; if (c >= 0x20 && c < 0x60) ++fl; } std::fprintf(stderr, "HUD %llu sprites=%d otros=%d total=%zu\n", (unsigned long long)hud_sequence, sp, fl, hud.size()); }
            }
            hud_building_.clear();
            seen_world_ = true;
            world_seen_flip_ = display_flips;
        } else if (seen_world_ && hud_building_.size() < 4000) {
            for (auto& p : seg) hud_building_.push_back(std::move(p));
        }
        segs_.clear();
    }
    std::uint64_t world_seen_flip_ = 0;
    std::uint64_t room_seen_flip_ = ~0ull;
    std::uint64_t relaxed_flip_ = ~0ull; bool after_clear_ = false;
    void publishFrame() {

        static const bool salaVale = std::getenv("SF3N_HUD_SOLO_MUNDO") == nullptr;
        const bool salas = salaVale && room_seen_flip_ != ~0ull && display_flips <= room_seen_flip_ + 2;
        if (display_flips > world_seen_flip_ + 4 && !salas) { hud_valid = false; seen_world_ = false; hud_building_.clear(); }
        else if (display_flips > world_seen_flip_ + 4) hud_building_.clear();
    }

    void gp0(std::uint32_t w) {
        ++gp0_words;
        if (xr_drop_ > 0) { --xr_drop_; recordWord(w); return; }

        if (xr_on_ && rec_left_ == 0 && rec_img_ == 0) {
            const auto c = w >> 24;

            static const bool keepNum = BootWindow::xrayCfg("rayosx_numero", "SF3N_RAYOSX_NUMERO", 0) != 0;
            if (c >= 0x64 && c < 0x80 && !keepNum) { xr_drop_ = int(length(c)) - 1; recordWord(w); return; }
            if ((c >= 0x20 && c < 0x60) || (c >= 0x60 && c < 0x80)) {
                const bool sprite = c >= 0x64;
                const std::uint32_t want = sprite ? xr_e5_plain_ : xr_e5_center_;
                if (want != xr_e5_cur_) { soft.gp0(want); xr_e5_cur_ = want; }
            }
        }
        soft.gp0(w);
        recordWord(w);
        if (remaining_ > 0) {
            if (image_words_ > 0) {
                --image_words_;
                if (--remaining_ == 0 && !menu_pic_injecting_ && menu_pic_.px &&
                    xfer_xy_ == (std::uint32_t(menu_pic_.x) | (std::uint32_t(menu_pic_.y) << 16)) &&
                    xfer_wh_ == (std::uint32_t(menu_pic_.w) | (std::uint32_t(menu_pic_.h) << 16))) showMenuPic();
                return;
            }
            if (--remaining_ == 0 && pending_cmd_ == 0xa0) {

            }
            if (pending_cmd_ >= 0xa0 && pending_cmd_ < 0xc0 && remaining_ == 1) xfer_xy_ = w;
            if (pending_cmd_ >= 0xa0 && pending_cmd_ < 0xc0 && remaining_ == 0) {
                const auto wh = w;
                xfer_wh_ = wh;
                const std::uint32_t wd = wh & 0xffff, ht = wh >> 16;
                image_words_ = (wd * ht + 1) / 2;
                remaining_ = image_words_;
                ++image_uploads;
            }
            return;
        }
        const auto cmd = w >> 24;
        pending_cmd_ = cmd;
        ++commands[cmd];
        remaining_ = length(cmd) - 1;
        if (cmd >= 0x20 && cmd < 0x80) ++primitives;
    }
    static std::uint32_t length(std::uint32_t c) {
        if (c >= 0x20 && c < 0x40) {
            const bool quad = c & 8, tex = c & 4, gour = c & 0x10;
            const std::uint32_t v = quad ? 4 : 3;
            return 1 + v + (tex ? v : 0) + (gour ? v - 1 : 0);
        }
        if (c >= 0x40 && c < 0x60) return (c & 0x10) ? 4 : 3;
        if (c >= 0x60 && c < 0x80) {
            std::uint32_t n = 2 + ((c & 4) ? 1 : 0);
            if ((c & 0x18) == 0) n += 1;
            return n;
        }
        if (c == 0x02) return 3;
        if (c >= 0x80 && c < 0xa0) return 4;
        if (c >= 0xa0 && c < 0xe0) return 3;
        return 1;
    }

    std::uint64_t gp0_words{}, gp1_count{}, primitives{}, image_uploads{}, display_flips{};
    std::uint32_t disp_x{}, disp_y{}, display_mode{};
    bool display_on{};
    std::map<std::uint32_t, std::uint64_t> commands;
private:
    std::uint32_t dma_dir_{}, remaining_{}, image_words_{}, pending_cmd_{};
    bool field_{};
};

class PadSio final : public GpuMmio {
public:
    explicit PadSio(PsxMachine& m) : m_(m) {}
    std::uint16_t buttons{0xffff};
    bool analog{false};
    std::uint8_t sticks[4]{0x80, 0x80, 0x80, 0x80};

    struct Port2 {
        bool connected{false};
        std::uint16_t buttons{0xffff};
        bool analog{true};
        std::uint8_t sticks[4]{0x80, 0x80, 0x80, 0x80};
        std::uint8_t cmd{0x42};
        bool config{false}, next_config{false};
        std::uint8_t map[6]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        std::uint8_t resp[8]{};
        int len{0};
        std::uint8_t motor_small{0}, motor_large{0};
    } p2;
    std::uint32_t readGpu(std::uint32_t off) noexcept override {
        switch (off) {
        case 0x0: {
            const auto v = rx_full_ ? rx_ : 0xffu;
            rx_full_ = false;
            return v | (v << 8) | (v << 16) | (v << 24);
        }
        case 0x4: {
            std::uint32_t s = 0x5;
            if (rx_full_) s |= 0x2;
            if (irq_) s |= 0x200;
            return s;
        }
        case 0x8: return mode_;
        case 0xa: return ctrl_;
        case 0xe: return baud_;
        default: return 0;
        }
    }
    void writeGpu(std::uint32_t off, std::uint32_t v) noexcept override {
        switch (off) {
        case 0x0: transfer(std::uint8_t(v)); break;
        case 0x8: mode_ = v & 0xffff; break;
        case 0xa:
            if (std::getenv("SF3B_SIOLOG") && exchanges > 200 && exchanges < 240) { std::fprintf(stderr, "SIO ctrl<=%04x\n", v); if (v == 0x1013 && exchanges < 205) g_trace = 400; }
            if (v & 0x40) { ctrl_ = 0; irq_ = false; seq_ = 0; rx_full_ = false; break; }
            if (v & 0x10) irq_ = false;
            ctrl_ = v & ~0x50u;
            if (!(ctrl_ & 0x2)) seq_ = 0;
            break;
        case 0xe: baud_ = v & 0xffff; break;
        default: break;
        }
    }
    std::uint64_t exchanges{};
    template <class F> void state(F&& f) { f(mode_); f(ctrl_); f(baud_); f(rx_); f(rx_full_); f(irq_); f(seq_); f(ack_at_); f(exchanges); }
    void poll() noexcept {
        if (ack_at_ && m_.currentTick() >= ack_at_) { ack_at_ = 0; irq_ = true; m_.pulseInterrupt(InterruptSource::controller); }
    }
private:
    std::uint64_t ack_at_{};
    void transfer(std::uint8_t tx) {
        ++exchanges;
        const bool port2 = (ctrl_ & 0x2000) != 0;
        std::uint8_t rx = 0xff;
        bool ack = false;
        if (!port2) {
            if (std::getenv("SF3N_PADCMDLOG") && seq_ == 1) { static int cnt[256]{}; if (cnt[tx]++ < 3) std::fprintf(stderr, "PADCMD %02x config=%d\n", tx, int(config_)); }
            switch (seq_) {
            case 0: if (tx == 0x01) { rx = 0xff; ack = true; seq_ = 1; } break;
            case 1: {
                cmd_ = tx;
                if ((!config_ && cmd_ != 0x43) || std::getenv("SF3N_PADSIMPLE")) cmd_ = 0x42;
                rx = config_ ? 0xf3 : analog ? 0x73 : 0x41;
                buildResponse();
                ack = true; seq_ = 2;
                break;
            }
            case 2: rx = 0x5a; ack = true; seq_ = 3; break;
            default: {
                const int i = seq_ - 3;
                if (i >= len_) break;
                rx = resp_[i];
                switch (cmd_) {
                case 0x42:
                    if (i < 6) {
                        if (map_[i] == 0x00) motor_small = (tx & 1) ? 255 : 0;
                        else if (map_[i] == 0x01) motor_large = tx;
                    }
                    break;
                case 0x43: if (i == 0) next_config_ = tx == 1; break;
                case 0x44: if (i == 0 && tx <= 1) analog = tx == 1; break;
                case 0x46: if (i == 0 && tx == 1) { resp_[3] = 0x01; resp_[4] = 0x01; resp_[5] = 0x14; } break;
                case 0x4c: if (i == 0 && tx == 1) resp_[3] = 0x07; break;
                case 0x4d: if (i < 6) { map_[i] = tx; if (std::getenv("SF3N_PADCMDLOG")) std::fprintf(stderr, "PAD4D %d=%02x\n", i, tx); } break;
                default: break;
                }
                ack = i < len_ - 1;
                ++seq_;
                if (i == len_ - 1) {
                    if (cmd_ == 0x43) config_ = next_config_;
                    if (cmd_ == 0x4d) { motor_small = 0; motor_large = 0; }
                }
                break;
            }
            }
        }
        else if (p2.connected) transferPort2(tx, rx, ack);
        rx_ = rx; rx_full_ = true;
        if (std::getenv("SF3B_SIOLOG") && exchanges > 200 && exchanges < 240) std::fprintf(stderr, "SIO tx=%02x rx=%02x seq=%d ctrl=%04x ack=%d\n", tx, rx, seq_, ctrl_, int(ack));

        if (ack && (ctrl_ & 0x1000)) ack_at_ = m_.currentTick() + 450;
    }

    void transferPort2(std::uint8_t tx, std::uint8_t& rx, bool& ack) {
        auto& P = p2;
        auto set = [&](std::initializer_list<std::uint8_t> v) { P.len = 0; for (auto b : v) P.resp[P.len++] = b; };
        switch (seq_) {
        case 0: if (tx == 0x01) { rx = 0xff; ack = true; seq_ = 1; } break;
        case 1: {
            P.cmd = tx;
            if (!P.config && P.cmd != 0x43) P.cmd = 0x42;
            rx = P.config ? 0xf3 : P.analog ? 0x73 : 0x41;
            const std::uint8_t lo = std::uint8_t(P.buttons & 0xff), hi = std::uint8_t(P.buttons >> 8);
            switch (P.cmd) {
            case 0x42: case 0x43:
                if (P.config && P.cmd == 0x43) set({0, 0, 0, 0, 0, 0});
                else if (P.analog || P.config) set({lo, hi, P.sticks[0], P.sticks[1], P.sticks[2], P.sticks[3]});
                else set({lo, hi});
                break;
            case 0x44: set({0, 0, 0, 0, 0, 0}); break;
            case 0x45: set({0x01, 0x02, std::uint8_t(P.analog ? 1 : 0), 0x02, 0x01, 0x00}); break;
            case 0x46: set({0x00, 0x00, 0x01, 0x02, 0x00, 0x0a}); break;
            case 0x47: set({0x00, 0x00, 0x02, 0x00, 0x01, 0x00}); break;
            case 0x4c: set({0x00, 0x00, 0x00, 0x04, 0x00, 0x00}); break;
            case 0x4d: set({P.map[0], P.map[1], P.map[2], P.map[3], P.map[4], P.map[5]}); break;
            default: set({0, 0, 0, 0, 0, 0}); break;
            }
            ack = true; seq_ = 2;
            break;
        }
        case 2: rx = 0x5a; ack = true; seq_ = 3; break;
        default: {
            const int i = seq_ - 3;
            if (i >= P.len) break;
            rx = P.resp[i];
            switch (P.cmd) {
            case 0x42:
                if (i < 6) {
                    if (P.map[i] == 0x00) P.motor_small = (tx & 1) ? 255 : 0;
                    else if (P.map[i] == 0x01) P.motor_large = tx;
                }
                break;
            case 0x43: if (i == 0) P.next_config = tx == 1; break;
            case 0x44: if (i == 0 && tx <= 1) P.analog = tx == 1; break;
            case 0x46: if (i == 0 && tx == 1) { P.resp[3] = 0x01; P.resp[4] = 0x01; P.resp[5] = 0x14; } break;
            case 0x4c: if (i == 0 && tx == 1) P.resp[3] = 0x07; break;
            case 0x4d: if (i < 6) P.map[i] = tx; break;
            default: break;
            }
            ack = i < P.len - 1;
            ++seq_;
            if (i == P.len - 1) {
                if (P.cmd == 0x43) P.config = P.next_config;
                if (P.cmd == 0x4d) { P.motor_small = 0; P.motor_large = 0; }
            }
            break;
        }
        }
    }

    void buildResponse() {
        auto set = [&](std::initializer_list<std::uint8_t> v) { len_ = 0; for (auto b : v) resp_[len_++] = b; };
        const std::uint8_t lo = std::uint8_t(buttons & 0xff), hi = std::uint8_t(buttons >> 8);
        switch (cmd_) {
        case 0x42: case 0x43:
            if (config_ && cmd_ == 0x43) set({0, 0, 0, 0, 0, 0});
            else if (analog || config_) set({lo, hi, sticks[0], sticks[1], sticks[2], sticks[3]});
            else set({lo, hi});
            break;
        case 0x44: set({0, 0, 0, 0, 0, 0}); break;
        case 0x45: set({0x01, 0x02, std::uint8_t(analog ? 1 : 0), 0x02, 0x01, 0x00}); break;
        case 0x46: set({0x00, 0x00, 0x01, 0x02, 0x00, 0x0a}); break;
        case 0x47: set({0x00, 0x00, 0x02, 0x00, 0x01, 0x00}); break;
        case 0x4c: set({0x00, 0x00, 0x00, 0x04, 0x00, 0x00}); break;
        case 0x4d: set({map_[0], map_[1], map_[2], map_[3], map_[4], map_[5]}); break;
        default: set({0, 0, 0, 0, 0, 0}); break;
        }
    }
public:

    std::uint8_t motor_small{0}, motor_large{0};
    void legacyState() { config_ = false; const std::uint8_t m[6]{0x00, 0x01, 0xff, 0xff, 0xff, 0xff}; for (int i = 0; i < 6; ++i) map_[i] = m[i]; }
    template <class F> void state2(F&& f) { f(config_); f(next_config_); for (auto& b : map_) f(b); f(analog); }
private:
    std::uint8_t cmd_{0x42};
    bool config_{false}, next_config_{false};
    std::uint8_t map_[6]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    std::uint8_t resp_[8]{};
    int len_{0};
    PsxMachine& m_;
    std::uint32_t mode_{}, ctrl_{}, baud_{};
    std::uint8_t rx_{0xff};
    bool rx_full_{}, irq_{};
    int seq_{};
};

constexpr std::uint32_t kTrapA0 = 0xA0, kTrapB0 = 0xB0, kTrapC0 = 0xC0, kTrapIrq = 0x80;
constexpr std::uint32_t breakInsn(std::uint32_t code) { return 0x0000000Du | (code << 6); }

#include "memcard.hpp"
struct Bios {
    R3000Runtime& cpu;
    PsxMachine& m;
    CaptureGpu& gpu;
    std::uint32_t hook_jmpbuf{};
    std::vector<std::vector<std::uint32_t>> irq_queues = std::vector<std::vector<std::uint32_t>>(4);
    std::vector<R3000State> saved;
    std::uint32_t kernel_heap{0x8000};
    std::uint32_t pad_buf[2]{}, pad_size[2]{};
    bool pad_started{};
    PadSio* pad{};
    std::uint32_t heap_start{}, heap_end{}, heap_cur{};
    std::map<std::string, std::uint64_t> counts;
    std::set<std::string> unknown;
    std::vector<std::string> opened_files;

    MemCard card;

    CardStore store;
    int target_slot = -2;
    std::string alias_name;
    bool store_ok = false;
    int view_saves = 0;
    std::function<std::string()> fog_now;
    int view_show = -1;
    std::string view_name;

    bool game_list = std::getenv("SF3N_LISTA_PC") == nullptr;
    std::vector<std::pair<std::string, int>> view_ids;
    static constexpr int kPagePer = 5;
    int view_page = 0, view_pages = 1;
    std::function<void()> on_dir;
    void rebuildView(int show = -1, const std::string& name = {}) {
        if (game_list) {
            const auto pg = store.pages(kPagePer);
            view_pages = int(pg.size());
            view_page = std::clamp(view_page, 0, view_pages - 1);
            view_ids = store.buildViewIds(card, pg[std::size_t(view_page)]);
            view_saves = int(view_ids.size()); view_show = -1; view_name.clear();
            return;
        }
        view_show = show; view_saves = store.buildView(card, show, name);
        view_name.clear();
        for (const auto& e : card.files()) if (e.name != CardStore::kSettings) view_name = e.name;
    }

    void syncFile(int first) {
        for (const auto& e : card.files()) {
            if (e.first != first) continue;
            const auto bytes = card.readFile(e.name);
            if (e.name == CardStore::kSettings) { store.setSettings(bytes); LOG("tarjeta: ajustes guardados (%zu bytes)\n", bytes.size()); }
            else if (e.name.rfind(CardStore::kPrefix, 0) == 0 && game_list) {

                int id = -1;
                for (const auto& [n, sid] : view_ids) if (n == e.name) id = sid;
                id = store.store(id, e.name, bytes, fog_now ? fog_now() : std::string());
                bool known = false;
                for (auto& [n, sid] : view_ids) if (n == e.name) { sid = id; known = true; }
                if (!known) view_ids.push_back({e.name, id});
                LOG("tarjeta: partida %s guardada en la ranura %d (%zu bytes)\n", e.name.c_str(), id, bytes.size());
            }
            else if (e.name.rfind(CardStore::kPrefix, 0) == 0) {
                const CardStore::Slot* old = target_slot >= 0 ? store.find(target_slot) : nullptr;
                const std::string archivo = old && e.name == alias_name ? old->archivo : e.name;
                target_slot = store.store(old ? target_slot : -1, archivo, bytes, fog_now ? fog_now() : std::string());
                LOG("tarjeta: partida %s guardada en la ranura %d (%zu bytes)\n", archivo.c_str(), target_slot, bytes.size());
            }
            return;
        }
    }
    struct CardFd { int first = -1; std::uint32_t pos = 0; std::uint32_t mode = 0; bool wrote = false; };
    std::map<std::uint32_t, CardFd> card_fds;
    std::uint32_t next_fd = 3;
    std::string dir_pattern;
    int dir_next = 0;
    static bool isCard(const std::string& n) { return n.rfind("bu00:", 0) == 0; }
    void cardEvents(bool ok) {
        const std::uint32_t spec = ok ? 0x0004u : 0x8000u;
        pending_events.push_back({0xF0000011u, spec});
        pending_events.push_back({0xF4000001u, spec});
    }

    bool fillDirentry(std::uint32_t dst) {
        const auto files = card.files();
        while (dir_next < int(files.size())) {
            const auto& f = files[std::size_t(dir_next++)];
            if (!MemCard::match(dir_pattern, f.name)) continue;

            cpu.write32(dst + 20, 0x50);
            for (int i = 0; i <= int(f.name.size()) && i < 21; ++i) cpu.write8(dst + std::uint32_t(i), i < int(f.name.size()) ? std::uint8_t(f.name[std::size_t(i)]) : 0);
            for (int i = int(f.name.size()) + 1; i < 20; ++i) cpu.write8(dst + std::uint32_t(i), 0);
            cpu.write32(dst + 24, f.size);
            cpu.write32(dst + 28, 0);
            cpu.write32(dst + 32, std::uint32_t(f.first));
            cpu.write32(dst + 36, 0);
            return true;
        }
        return false;
    }
    struct Event { std::uint32_t cls{}, spec{}, mode{}, func{}; bool used{}, enabled{}, ready{}; };
    std::vector<Event> events = std::vector<Event>(64);
    bool clear_rcnt[4]{true, true, true, true};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pending_events;
    std::uint64_t srcCount[11]{};
    template <class F> void state(F&& f) {
        f(hook_jmpbuf); f(irq_queues[0]); f(irq_queues[1]); f(irq_queues[2]); f(irq_queues[3]);
        f(saved); f(kernel_heap); f(pad_buf); f(pad_size); f(pad_started);
        f(heap_start); f(heap_end); f(heap_cur); f(events); f(clear_rcnt); f(pending_events);
    }

    void deliverEvent(std::uint32_t cls, std::uint32_t spec) {
        if (std::getenv("SF3N_CARDLOG") && (cls == 0xF0000011u || cls == 0xF4000001u)) LOG("evento %08x %04x (pc=%08x)\n", cls, spec, cpu.state().pc);
        for (auto& e : events) {
            if (!e.used || !e.enabled || e.cls != cls || (e.spec & spec) == 0) continue;
            if (e.mode == 0x2000) e.ready = true;
            else if (e.mode == 0x1000 && e.func) guestCall(e.func, {});
        }
    }

    std::uint32_t reg(int r) const { return cpu.state().gpr[std::size_t(r)]; }
    void setReg(int r, std::uint32_t v) { cpu.setRegister(std::uint8_t(r), v); }
    void jump(std::uint32_t pc) {
        auto s = cpu.state();
        s.pc = pc; s.next_pc = pc + 4; s.branch_delay_slot = false; s.branch_pc = 0;
        cpu.restoreCpuState(s);
    }
    std::string str(std::uint32_t a, std::size_t max = 256) const {
        std::string out;
        for (std::size_t i = 0; i < max; ++i) {
            std::uint8_t c{};
            if (!cpu.read8(a + std::uint32_t(i), c) || !c) break;
            out.push_back(char(c));
        }
        return out;
    }
    void ret(std::uint32_t v0) { setReg(2, v0); jump(reg(31)); }

    std::uint32_t guestCall(std::uint32_t addr, std::initializer_list<std::uint32_t> args, std::uint64_t budget = 20'000'000) {
        const auto before = cpu.state();
        int i = 4;
        for (auto a : args) setReg(i++, a);
        setReg(31, R3000Runtime::return_sentinel);

        {
            const auto sp = reg(29);
            const bool onIsrStack = sp > 0x8000c000u && sp <= 0x8000fff0u;
            setReg(29, onIsrStack ? sp - 64 : 0x8000fff0u - 64);
        }
        jump(addr);
        std::uint64_t n = 0;
        while (cpu.state().pc != R3000Runtime::return_sentinel && n++ < budget) {
            if (pad) pad->poll();
            const auto r = m.step();
            if (r.reason == R3000StopReason::running) continue;
            if (!service(r)) { LOG("guestCall %08x stop %s at %08x\n", addr, std::string(toString(r.reason)).c_str(), r.pc); break; }
        }
        if (n >= budget) { static int bl = 0; if (bl++ < 5) LOG("guestCall %08x NO VUELVE (pc=%08x, %llu instr)\n", addr, cpu.state().pc, (unsigned long long)n); }
        const auto v0 = reg(2);
        auto restore = before;
        cpu.restoreCpuState(restore);
        return v0;
    }

    void interrupt() {

        saved.push_back(cpu.state());
        {

            std::uint32_t st{}, mk{};
            (void)cpu.read32(0x1f801070, st); (void)cpu.read32(0x1f801074, mk);
            for (int b = 0; b < 11; ++b) if (st & mk & (1u << b)) ++srcCount[b];
            for (std::uint32_t t = 0; t < 4; ++t) {
                const std::uint32_t bit = t == 3 ? 0x1u : (0x10u << t);
                if (!(st & mk & bit)) continue;
                if (clear_rcnt[t]) (void)cpu.write32(0x1f801070, ~bit & 0xffffu);
                deliverEvent(0xF2000000u + t, 0x0002u);
            }
        }
        for (int prio = 0; prio < 4; ++prio) {
            for (auto elem : irq_queues[std::size_t(prio)]) {
                std::uint32_t func2{}, func1{};
                cpu.read32(elem + 4, func2);
                cpu.read32(elem + 8, func1);
                if (func1) {
                    const auto v = guestCall(func1, {});
                    if (v && func2) guestCall(func2, {v});
                }
            }
        }
        ++counts["irq"];
        std::uint32_t hookPc{};
        if (hook_jmpbuf) cpu.read32(hook_jmpbuf, hookPc);
        if (hook_jmpbuf && hookPc) {

            std::uint32_t b[12]{};
            for (int k = 0; k < 12; ++k) cpu.read32(hook_jmpbuf + 4u * std::uint32_t(k), b[k]);
            setReg(31, b[0]); setReg(29, b[1]); setReg(30, b[2]);
            for (int k = 0; k < 8; ++k) setReg(16 + k, b[3 + k]);
            setReg(28, b[11]);
            setReg(2, 1);
            jump(b[0]);
        } else {

            cpu.write32(0x1f801070, 0);
            returnFromException();
        }
    }
    void returnFromException() {
        if (saved.empty()) { LOG("RFE without frame\n"); return; }
        auto s = saved.back();
        saved.pop_back();
        s.cop0_cause = cpu.state().cop0_cause & 0x0000ff00u;
        s.cop0_status = (s.cop0_status & ~0x0fu) | ((s.cop0_status >> 2) & 0x0fu);
        const auto epc = s.cop0_epc;
        s.pc = epc; s.next_pc = epc + 4; s.branch_delay_slot = false; s.branch_pc = 0;
        s.load_delay = {}; s.next_load_delay = {};
        cpu.restoreCpuState(s);
    }

    void syscall(std::uint32_t pc) {
        auto s = cpu.state();
        const auto fn = s.gpr[4];
        ++counts["syscall" + std::to_string(fn)];
        if (fn == 1) { s.gpr[2] = (s.cop0_status & 0x401) == 0x401; s.cop0_status &= ~0x401u; }
        else if (fn == 2) { s.cop0_status |= 0x401u; }
        s.pc = pc + 4; s.next_pc = pc + 8; s.branch_delay_slot = false;
        cpu.restoreCpuState(s);
    }

    void unknownCall(const char* table, std::uint32_t fn) {
        char b[32]; std::snprintf(b, sizeof b, "%s:%02x", table, fn);
        if (unknown.insert(b).second) LOG("BIOS sin implementar %s (ra=%08x)\n", b, reg(31));
        ret(0);
    }

    void callA0(std::uint32_t fn) {
        char k[16]; std::snprintf(k, sizeof k, "A0:%02x", fn); ++counts[k];
        const auto a0 = reg(4), a1 = reg(5), a2 = reg(6);
        switch (fn) {
        case 0x13: {
            const std::uint32_t v[12]{reg(31), reg(29), reg(30), reg(16), reg(17), reg(18), reg(19), reg(20), reg(21), reg(22), reg(23), reg(28)};
            for (int k = 0; k < 12; ++k) (void)cpu.write32(a0 + 4u * std::uint32_t(k), v[k]);
            ret(0); return;
        }
        case 0x14: {
            std::uint32_t b[12]{};
            for (int k = 0; k < 12; ++k) (void)cpu.read32(a0 + 4u * std::uint32_t(k), b[k]);
            setReg(31, b[0]); setReg(29, b[1]); setReg(30, b[2]);
            for (int k = 0; k < 8; ++k) setReg(16 + k, b[3 + k]);
            setReg(28, b[11]);
            ret(a1 ? a1 : 1); return;
        }
        case 0x0e: ret(std::uint32_t(std::abs(std::int32_t(a0)))); return;
        case 0x10: case 0x11: ret(std::uint32_t(std::atoi(str(a0).c_str()))); return;
        case 0x15: { auto x = str(a0, 65536), y = str(a1, 65536); x += y; for (std::size_t i = 0; i <= x.size(); ++i) cpu.write8(a0 + std::uint32_t(i), i < x.size() ? std::uint8_t(x[i]) : 0); ret(a0); return; }
        case 0x17: { auto x = str(a0, 65536), y = str(a1, 65536); const int c = x.compare(y); ret(std::uint32_t(c < 0 ? -1 : c > 0 ? 1 : 0)); return; }
        case 0x18: {
            for (std::uint32_t i = 0; i < a2; ++i) {
                std::uint8_t c1{}, c2{}; cpu.read8(a0 + i, c1); cpu.read8(a1 + i, c2);
                if (c1 != c2) { ret(std::uint32_t(int(c1) - int(c2))); return; }
                if (!c1) break;
            }
            ret(0); return;
        }
        case 0x1a: {
            bool end = false;
            for (std::uint32_t i = 0; i < a2; ++i) { std::uint8_t c{}; if (!end) { cpu.read8(a1 + i, c); if (!c) end = true; } cpu.write8(a0 + i, end ? 0 : c); }
            ret(a0); return;
        }
        case 0x1c: case 0x1e: {
            for (std::uint32_t i = 0;; ++i) { std::uint8_t c{}; cpu.read8(a0 + i, c); if (c == std::uint8_t(a1)) { ret(a0 + i); return; } if (!c) { ret(0); return; } }
        }
        case 0x1d: case 0x1f: {
            std::uint32_t found = 0;
            for (std::uint32_t i = 0;; ++i) { std::uint8_t c{}; cpu.read8(a0 + i, c); if (c == std::uint8_t(a1)) found = a0 + i; if (!c) break; }
            ret(found); return;
        }
        case 0x24: { auto x = str(a0, 65536), y = str(a1, 65536); auto p = x.find(y); ret(p == std::string::npos ? 0 : a0 + std::uint32_t(p)); return; }
        case 0x26: ret(std::uint32_t(std::tolower(int(a0 & 0xff)))); return;
        case 0x27: for (std::uint32_t i = 0; i < a2; ++i) { std::uint8_t c{}; cpu.read8(a0 + i, c); cpu.write8(a1 + i, c); } ret(0); return;
        case 0x29: case 0x2d: {
            for (std::uint32_t i = 0; i < a2; ++i) { std::uint8_t c1{}, c2{}; cpu.read8(a0 + i, c1); cpu.read8(a1 + i, c2); if (c1 != c2) { ret(std::uint32_t(int(c1) - int(c2))); return; } }
            ret(0); return;
        }
        case 0x2c: {
            std::vector<std::uint8_t> tmp(a2);
            for (std::uint32_t i = 0; i < a2; ++i) cpu.read8(a1 + i, tmp[i]);
            for (std::uint32_t i = 0; i < a2; ++i) cpu.write8(a0 + i, tmp[i]);
            ret(a0); return;
        }
        case 0x2e: for (std::uint32_t i = 0; i < a2; ++i) { std::uint8_t c{}; cpu.read8(a0 + i, c); if (c == std::uint8_t(a1)) { ret(a0 + i); return; } } ret(0); return;
        case 0x19: { auto x = str(a1, 4096); for (std::size_t i = 0; i <= x.size(); ++i) cpu.write8(a0 + std::uint32_t(i), i < x.size() ? std::uint8_t(x[i]) : 0); ret(a0); return; }
        case 0x1b: ret(std::uint32_t(str(a0, 65536).size())); return;
        case 0x25: ret(std::uint32_t(std::toupper(int(a0 & 0xff)))); return;
        case 0x28: for (std::uint32_t i = 0; i < a1; ++i) cpu.write8(a0 + i, 0); ret(a0); return;
        case 0x2a: for (std::uint32_t i = 0; i < a2; ++i) { std::uint8_t c{}; cpu.read8(a1 + i, c); cpu.write8(a0 + i, c); } ret(a0); return;
        case 0x2b: for (std::uint32_t i = 0; i < a2; ++i) cpu.write8(a0 + i, std::uint8_t(a1)); ret(a0); return;
        case 0x2f: ret(std::uint32_t(std::rand() & 0x7fff)); return;
        case 0x30: std::srand(a0); ret(0); return;
        case 0x33: { const auto sz = (a0 + 7) & ~7u; if (heap_cur + sz > heap_end) { ret(0); return; } const auto p = heap_cur; heap_cur += sz; ret(p); return; }
        case 0x34: ret(0); return;
        case 0x39: heap_start = heap_cur = a0; heap_end = a0 + a1; ret(0); return;
        case 0x3c: case 0x3e: ret(0); return;
        case 0x3f: {
            const auto fmt = str(a0, 1024);
            std::string out;
            std::uint32_t argi = 1;
            auto arg = [&]() -> std::uint32_t {
                const auto i = argi++;
                if (i < 4) return reg(4 + int(i));
                std::uint32_t v{}; (void)cpu.read32(reg(29) + 4 * i, v); return v;
            };
            for (std::size_t i = 0; i < fmt.size(); ++i) {
                if (fmt[i] != '%') { out += fmt[i]; continue; }
                std::string spec = "%";
                while (++i < fmt.size() && std::strchr("0123456789.-lh", fmt[i])) spec += fmt[i];
                if (i >= fmt.size()) break;
                const char c = fmt[i];
                char buf[256];
                if (c == 's') { out += str(arg()); continue; }
                if (c == '%') { out += '%'; continue; }
                spec.erase(std::remove(spec.begin(), spec.end(), 'l'), spec.end());
                spec.erase(std::remove(spec.begin(), spec.end(), 'h'), spec.end());
                spec += (c == 'X' || c == 'x' || c == 'd' || c == 'u' || c == 'c') ? c : 'x';
                std::snprintf(buf, sizeof buf, spec.c_str(), arg());
                out += buf;
            }
            if (out.find("intr timeout") != std::string::npos) {
                for (std::uint32_t t = 0; t < 3; ++t) { std::uint32_t c{}, md{}, tg{}; (void)cpu.read32(0x1f801100 + 16 * t, c); (void)cpu.read32(0x1f801104 + 16 * t, md); (void)cpu.read32(0x1f801108 + 16 * t, tg);
                    static int once = 0; if (once++ < 6) LOG("  timer%u count=%04x mode=%04x target=%04x\n", t, c, md, tg); }
            }
            static std::map<std::string, int> seen;
            if (seen[out]++ < 3) LOG("printf: %s", out.c_str());
            ret(0); return;
        }
        case 0x44: ret(0); return;
        case 0x49: gpu.writeGpu(4, a0); ret(0); return;
        case 0x70: case 0x71: case 0x72: case 0x96: case 0x97: case 0x99: case 0x9f: ret(0); return;
        case 0xa1: LOG("SystemError %c %d\n", char(a0), a1); ret(0); return;
        case 0xab: case 0xac:
            if ((a0 & 0x30) == 0 && card.present()) {
                static int n = 0; if (n++ < 6) LOG("BIOS %s(%02x) -> tarjeta lista\n", fn == 0xab ? "_card_info" : "_card_load", a0);
                if (fn == 0xac) pending_events.push_back({0xF0000011u, 0x0004u});
                pending_events.push_back({0xF4000001u, 0x0004u}); ret(1); return;
            }
            { static int n = 0; if (n++ < 3) LOG("BIOS %s(%02x) -> sin tarjeta\n", fn == 0xab ? "_card_info" : "_card_load", a0); }
            pending_events.push_back({0xF4000001u, 0x0100u}); ret(1); return;
        default: unknownCall("A0", fn); return;
        }
    }
    void callB0(std::uint32_t fn) {
        char k[16]; std::snprintf(k, sizeof k, "B0:%02x", fn); ++counts[k];
        const auto a0 = reg(4), a1 = reg(5), a2 = reg(6), a3 = reg(7);
        if (std::getenv("SF3N_CARDLOG") && ((fn >= 0x32 && fn <= 0x36) || (fn >= 0x41 && fn <= 0x5f))) LOG("B0:%02x(%08x, %08x, %08x) ra=%08x\n", fn, a0, a1, a2, reg(31));
        switch (fn) {
        case 0x00: { const auto p = kernel_heap; kernel_heap += (a0 + 3) & ~3u; ret(p); return; }
        case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: ret(1); return;
        case 0x07: deliverEvent(a0, a1); ret(0); return;
        case 0x08: {
            for (std::size_t i = 0; i < events.size(); ++i) if (!events[i].used) {
                events[i] = Event{a0, a1, a2, a3, true, false, false};
                LOG("OpenEvent %08x spec %04x mode %04x func %08x -> %zu\n", a0, a1, a2, a3, i);
                ret(0xF1000000u | std::uint32_t(i)); return;
            }
            ret(0xffffffffu); return;
        }
        case 0x09: events[a0 & 0x3f].used = false; ret(1); return;
        case 0x0c: events[a0 & 0x3f].enabled = true; ret(1); return;
        case 0x0d: events[a0 & 0x3f].enabled = false; ret(1); return;
        case 0x20: events[a0 & 0x3f].ready = false; ret(1); return;
        case 0x0b: {
            auto& e = events[a0 & 0x3f];

            const bool modeled = (e.cls >> 24) == 0xF2;
            if (!modeled || e.ready) { e.ready = false; ret(1); } else ret(0);
            return;
        }
        case 0x0a: { auto& e = events[a0 & 0x3f]; e.ready = false; ret(1); return; }
        case 0x12: pad_buf[0] = a0; pad_size[0] = a1; pad_buf[1] = a2; pad_size[1] = a3; ret(1); return;
        case 0x13: pad_started = true; ret(1); return;
        case 0x14: pad_started = false; ret(1); return;
        case 0x16: ret(0xffffffffu); return;
        case 0x17: {
            static int dbg = 0;
            if (dbg++ < 3) {
                std::uint32_t ist{}, p1{}, p2{}; (void)cpu.read32(0x1f801070, ist); (void)cpu.read32(0x8011f9ec, p1); (void)cpu.read32(0x8011f9f0, p2);
                std::uint16_t a{}, b{}; (void)cpu.read16(p1, a); (void)cpu.read16(p2, b);
                LOG("RFE: I_STAT=%04x ptrs %08x %08x -> %04x %04x cause=%08x saved=%zu\n", ist, p1, p2, a, b, cpu.state().cop0_cause, saved.size());
            }
            returnFromException(); return;
        }
        case 0x18: hook_jmpbuf = 0; ret(0); return;
        case 0x19: hook_jmpbuf = a0; LOG("HookEntryInt %08x (pc=%08x)\n", a0, [&]{std::uint32_t v{}; cpu.read32(a0, v); return v;}()); ret(0); return;
        case 0x3d: case 0x3f: ret(0); return;
        case 0x32: {
            const auto name = str(a0);
            if (isCard(name) && card.present()) {
                const std::string fname = name.substr(5);
                int first = card.find(fname);

                if (first < 0 && !(a1 & 0x200) && store_ok && game_list && fname.rfind(CardStore::kPrefix, 0) == 0 && fname != CardStore::kSettings) {
                    const auto pg = store.pages(kPagePer);
                    for (std::size_t p = 0; p < pg.size() && first < 0; ++p)
                        for (int id : pg[p]) if (const auto* s = store.find(id); s && s->archivo == fname) { view_page = int(p); rebuildView(); first = card.find(fname); break; }
                }
                if (first < 0 && !(a1 & 0x200) && store_ok && !game_list && view_saves == 1 && fname.rfind(CardStore::kPrefix, 0) == 0 && fname != CardStore::kSettings) {
                    rebuildView(view_show, fname);
                    if (!alias_name.empty()) alias_name = fname;
                    first = card.find(fname);
                }
                if (a1 & 0x200) {
                    first = first >= 0 ? -1 : card.create(fname, int(std::max<std::uint32_t>(1, a1 >> 16)));
                    if (first >= 0) card.flush();
                }
                LOG("BIOS open %s modo %x -> %s\n", name.c_str(), a1, first >= 0 ? "ok" : "no");
                if (first < 0) { ret(0xffffffffu); return; }
                const auto fd = next_fd++;
                card_fds[fd] = CardFd{first, 0, a1};
                ret(fd); return;
            }
            opened_files.push_back(name); LOG("BIOS open %s\n", name.c_str()); ret(0xffffffffu); return;
        }

        case 0x36: {
            if (auto it = card_fds.find(a0); it != card_fds.end() && it->second.wrote) syncFile(it->second.first);
            card_fds.erase(a0); card.flush(); ret(a0); return;
        }
        case 0x33: {
            auto it = card_fds.find(a0);
            if (it == card_fds.end()) { ret(0xffffffffu); return; }
            it->second.pos = a2 == 1 ? it->second.pos + a1 : a1;
            ret(it->second.pos); return;
        }
        case 0x34: case 0x35: {
            auto it = card_fds.find(a0);
            if (it == card_fds.end()) { ret(0xffffffffu); return; }
            auto& f = it->second;
            std::vector<std::uint8_t> buf(a2);
            const bool wr = fn == 0x35;
            if (std::getenv("SF3N_CARDLOG")) LOG("BIOS %s fd %u pos %u len %u\n", wr ? "write" : "read", a0, f.pos, a2);
            if (wr) for (std::uint32_t i = 0; i < a2; ++i) cpu.read8(a1 + i, buf[i]);
            const auto n = card.io(f.first, f.pos, buf.data(), buf.size(), wr);
            if (wr && n) f.wrote = true;
            if (!wr) for (std::size_t i = 0; i < n; ++i) cpu.write8(a1 + std::uint32_t(i), buf[i]);
            f.pos += std::uint32_t(n);
            if (wr) card.flush();

            if (f.mode & 0x8000) { cardEvents(n == a2); ret(0); return; }
            ret(std::uint32_t(n)); return;
        }
        case 0x41: {
            if (isCard(str(a0)) && card.present()) { card.format(); ret(1); } else ret(0);
            return;
        }
        case 0x42: {
            const auto pat = str(a0);
            if (std::getenv("SF3N_CARDLOG")) { std::uint32_t c1{}, c2{}; (void)cpu.read32(reg(29) + 0x20, c1); (void)cpu.read32(reg(29) + 0x28 + 0x2c, c2); LOG("BIOS firstfile %s (llamada desde %08x / %08x)\n", pat.c_str(), c1, c2); }
            if (!isCard(pat) || !card.present()) { ret(0); return; }
            dir_pattern = pat.substr(5);
            if (dir_pattern.empty()) dir_pattern = "*";

            if (dir_pattern == "*" && store_ok) { rebuildView(); target_slot = -2; alias_name.clear(); if (game_list && on_dir) on_dir(); }
            dir_next = 0;

            if (fillDirentry(a1)) { ret(a1); return; }
            cardEvents(true);
            ret(0); return;
        }
        case 0x43: ret(fillDirentry(a0) ? a0 : 0); return;
        case 0x44: {
            ret(0); return;
        }
        case 0x45: {
            const auto name = str(a0);
            const bool ok = isCard(name) && card.erase(name.substr(5));
            if (ok) card.flush();
            LOG("BIOS erase %s -> %d\n", name.c_str(), int(ok));
            ret(ok ? 1 : 0); return;
        }
        case 0x4e: case 0x4f: {
            if ((a0 & 0x30) == 0 && card.present()) {
                std::uint8_t sec[128];
                if (fn == 0x4e) { for (int i = 0; i < 128; ++i) cpu.read8(a2 + std::uint32_t(i), sec[i]); card.writeSector(int(a1), sec); card.flush(); }
                else { card.readSector(int(a1), sec); for (int i = 0; i < 128; ++i) cpu.write8(a2 + std::uint32_t(i), sec[i]); }
                pending_events.push_back({0xF0000011u, 0x0004u}); ret(1); return;
            }
            pending_events.push_back({0xF0000011u, 0x0100u}); pending_events.push_back({0xF4000001u, 0x0100u}); ret(1); return;
        }
        case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x50: ret(1); return;
        case 0x56: ret(0x674); return;
        case 0x57: ret(0x874); return;
        case 0x5b: ret(0); return;
        default: unknownCall("B0", fn); return;
        }
    }
    void callC0(std::uint32_t fn) {
        char k[16]; std::snprintf(k, sizeof k, "C0:%02x", fn); ++counts[k];
        const auto a0 = reg(4), a1 = reg(5);
        switch (fn) {
        case 0x02: {
            auto& q = irq_queues[a0 & 3];
            if (std::find(q.begin(), q.end(), a1) == q.end()) q.insert(q.begin(), a1);
            std::uint32_t f1{}, f2{}; cpu.read32(a1 + 8, f1); cpu.read32(a1 + 4, f2);
            LOG("SysEnqIntRP prio %u elem %08x func1 %08x func2 %08x\n", a0, a1, f1, f2);
            ret(0); return;
        }
        case 0x03: { auto& q = irq_queues[a0 & 3]; q.erase(std::remove(q.begin(), q.end(), a1), q.end()); ret(0); return; }
        case 0x0a: { const bool old = clear_rcnt[a0 & 3]; clear_rcnt[a0 & 3] = a1 != 0; ret(old); return; }
        case 0x00: case 0x01: case 0x07: case 0x08: case 0x0c: case 0x12: case 0x1c: ret(0); return;
        default: unknownCall("C0", fn); return;
        }
    }

    bool service(const R3000RunResult& r) {
        if (r.reason == R3000StopReason::breakpoint) {
            const auto code = (r.instruction >> 6) & 0xfffff;
            const auto t1 = reg(9);
            switch (code) {
            case 1: callA0(t1 & 0xff); return true;
            case 2: callB0(t1 & 0xff); return true;
            case 3: callC0(t1 & 0xff); return true;
            case 4: interrupt(); return true;
            default: LOG("break %x at %08x\n", code, r.pc); return false;
            }
        }
        if (r.reason == R3000StopReason::syscall) { syscall(r.pc); return true; }
        return false;
    }

    void deviceTable() {
        std::uint32_t t{};
        (void)cpu.read32(0x150, t);
        if (t == 0x7f10u) return;
        (void)cpu.write32(0x7f00, 0x00007562u);
        for (std::uint32_t i = 0; i < 0x50; i += 4) (void)cpu.write32(0x7f10 + i, 0);
        (void)cpu.write32(0x7f10, 0x7f00);
        (void)cpu.write32(0x150, 0x7f10);
        (void)cpu.write32(0x154, 0x50);
    }
    void vblank() {
        if (card.present()) deviceTable();
        if (!pending_events.empty() && saved.empty()) {
            auto evs = std::move(pending_events); pending_events.clear();
            for (auto [c, sp] : evs) deliverEvent(c, sp);
        }
        if (pad_started) {
            for (int p = 0; p < 2; ++p) {
                if (!pad_buf[p]) continue;
                const std::uint8_t data[4]{0x00, std::uint8_t(p == 0 ? 0x41 : 0xff), 0xff, 0xff};
                for (int i = 0; i < 4 && std::uint32_t(i) < pad_size[p]; ++i) cpu.write8(pad_buf[p] + std::uint32_t(i), data[i]);
            }
        }
    }
};

}

static void dumpScreen(const SoftGpu& g, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const int w = g.disp_w, h = std::min(g.disp_h, 256);
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char px[3];
            if (g.rgb24) {
                const int bx = g.disp_x * 2 + x * 3;
                auto byteAt = [&](int i) { const auto wv = g.vram[((g.disp_y + y) & 511) * 1024 + ((i / 2) & 1023)]; return (i & 1) ? (wv >> 8) : (wv & 0xff); };
                px[0] = (unsigned char)byteAt(bx); px[1] = (unsigned char)byteAt(bx + 1); px[2] = (unsigned char)byteAt(bx + 2);
            } else {
                const auto c = g.vram[((g.disp_y + y) & 511) * 1024 + ((g.disp_x + x) & 1023)];
                px[0] = (unsigned char)((c & 31) << 3); px[1] = (unsigned char)(((c >> 5) & 31) << 3); px[2] = (unsigned char)(((c >> 10) & 31) << 3);
            }
            std::fwrite(px, 1, 3, f);
        }
    std::fclose(f);
}

struct Ser {
    std::vector<char>* out = nullptr; const char* in = nullptr; std::size_t pos = 0, size = 0; bool ok = true;
    void raw(void* p, std::size_t n) {
        if (out) { const char* c = static_cast<const char*>(p); out->insert(out->end(), c, c + n); }
        else { if (pos + n > size) { ok = false; return; } std::memcpy(p, in + pos, n); pos += n; }
    }
    template <class T> void operator()(T& v) {
        if constexpr (requires { v.size(); v.data(); v.resize(0); }) {
            std::uint64_t n = v.size(); raw(&n, 8);
            if (!out) { if (n > (1u << 28)) { ok = false; return; } v.resize(n); }
            if constexpr (std::is_trivially_copyable_v<typename T::value_type>) raw(v.data(), n * sizeof(typename T::value_type));
            else for (auto& e : v) (*this)(e);
        } else if constexpr (requires { v.first; v.second; }) {
            (*this)(v.first); (*this)(v.second);
        } else {
            static_assert(std::is_trivially_copyable_v<T>);
            raw(&v, sizeof(T));
        }
    }
};

static const auto g_t_arranque = std::chrono::steady_clock::now();
static void marcaArranque(const char* que) {
    if (!g_log) return;
    std::fprintf(g_log, "arranque: %-28s %6.0f ms\n", que, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_t_arranque).count());
    std::fflush(g_log);
}
int main(int argc, char** argv) {
    {
        const char* bp = SDL_GetBasePath();
        const std::string exeDirS = bp ? std::string(bp) : std::filesystem::absolute(argv[0]).parent_path().string();
        for (int i = 1; i < argc; ++i)
            if (std::string(argv[i]) == "--crear-paks") {
                const std::string msg = pakfs::createPaks(exeDirS);
                std::fprintf(stdout, "%s", msg.c_str());
#ifdef SF3B_SDL
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Syphon Filter 3 - PC", msg.c_str(), nullptr);
#endif
                return 0;
            }
        pakfs::init(exeDirS);
    }

    {
        const auto exeDir = std::filesystem::absolute(argv[0]).parent_path();

        std::string dataIni;
        if (const char* e = std::getenv("SF3N_DATOS")) dataIni = std::string(e) + "/" SF3_INI;
#if defined(_WIN32)

        else if (const char* la = std::getenv("LOCALAPPDATA")) {
            namespace fs = std::filesystem;
            std::error_code mec;
            const fs::path old = fs::path(la) / "SF3_NATIVO";
            const fs::path mark = exeDir / "partidas";
            if (fs::exists(old, mec) && !fs::exists(mark, mec)) {
                for (const char* sub : {"partidas", "grabaciones"})
                    if (fs::exists(old / sub, mec)) fs::copy(old / sub, exeDir / sub, fs::copy_options::recursive | fs::copy_options::skip_existing, mec);
                std::vector<std::pair<std::string, std::string>> kv;
                auto readKv = [&](const fs::path& f) {
                    if (FILE* fp = std::fopen(f.string().c_str(), "r")) {
                        char line[1024];
                        while (std::fgets(line, sizeof line, fp)) {
                            std::string l(line);
                            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                            const auto eq = l.find('=');
                            std::string k = eq == std::string::npos ? l : l.substr(0, eq);
                            while (!k.empty() && k.back() == ' ') k.pop_back();
                            if (k == "voces_tono") continue;
                            bool rep = false;
                            if (eq != std::string::npos) for (auto& [kk, ll] : kv) if (kk == k) { ll = l; rep = true; }
                            if (!rep) kv.push_back({eq == std::string::npos ? std::string() : k, l});
                        }
                        std::fclose(fp);
                    }
                };
                readKv(old / SF3_INI);
                readKv(exeDir / SF3_INI);
                if (FILE* fp = std::fopen((exeDir / SF3_INI).string().c_str(), "w")) {
                    for (const auto& [k, l] : kv) if (!l.empty()) std::fprintf(fp, "%s\n", l.c_str());
                    std::fclose(fp);
                }
                fs::create_directories(mark, mec);
            }
        }
#endif
        std::error_code iec;
        const std::string iniFile = (!dataIni.empty() && std::filesystem::exists(dataIni, iec)) ? dataIni : (exeDir / SF3_INI).string();

        const std::string exeIni = (exeDir / SF3_INI).string();
        auto readLang = [](const std::string& path, std::string& out) {
            bool found = false;
            if (FILE* f = std::fopen(path.c_str(), "r")) {
                char line[512];
                while (std::fgets(line, sizeof line, f)) {
                    std::string l(line);
                    while (!l.empty() && l[0] == ' ') l.erase(0, 1);
                    if ((l.rfind("idioma", 0) == 0 || l.rfind("lenguaje", 0) == 0) && l.find('=') != std::string::npos) {
                        std::string v = l.substr(l.find('=') + 1);
                        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
                        while (!v.empty() && v[0] == ' ') v.erase(0, 1);
                        out = v; found = true;
                    }
                }
                std::fclose(f);
            }
            return found;
        };
        bool exeHasLang = false;
        if (const char* e = std::getenv("SF3N_IDIOMA")) g_idioma = e;
        else {
            if (iniFile != exeIni) (void)readLang(iniFile, g_idioma);
            std::string ev;
            if (readLang(exeIni, ev)) { g_idioma = ev; exeHasLang = true; }
        }
        for (auto& c : g_idioma) c = char(std::tolower(static_cast<unsigned char>(c)));
#ifdef SF3B_SDL

        if (g_idioma.empty() && !std::getenv("SF3N_IDIOMA") && !std::getenv("SF3N_TEST")) {
            const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "English"}, {0, 2, "Espa\xc3\xb1ol"}};
            const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, "Syphon Filter 3", "Select language / Selecciona el idioma", 2, buttons, nullptr};
            int pick = 2;
            if (SDL_ShowMessageBox(&box, &pick) != 0 || pick < 1) pick = 2;
            g_idioma = pick == 1 ? "en" : "es";
            std::vector<std::string> targets{!dataIni.empty() ? dataIni : exeIni};
            if (exeHasLang && targets[0] != exeIni) targets.push_back(exeIni);
            for (const auto& target : targets) {
            std::vector<std::string> lines; bool done = false;
            if (FILE* f = std::fopen(target.c_str(), "r")) {
                char line[512];
                while (std::fgets(line, sizeof line, f)) {
                    std::string l(line);
                    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
                    if ((l.rfind("idioma", 0) == 0 || l.rfind("lenguaje", 0) == 0) && l.find('=') != std::string::npos) { if (done) continue; l = "idioma=" + g_idioma; done = true; }
                    lines.push_back(l);
                }
                std::fclose(f);
            }
            if (!done) lines.push_back("idioma=" + g_idioma);
            std::error_code dec; std::filesystem::create_directories(std::filesystem::path(target).parent_path(), dec);
            if (FILE* f = std::fopen(target.c_str(), "w")) { for (const auto& l : lines) std::fprintf(f, "%s\n", l.c_str()); std::fclose(f); }
            }
        }
#endif
        if (g_idioma.empty()) g_idioma = "es";
        if (g_idioma == "ingles" || g_idioma == "english") g_idioma = "en";
        if (g_idioma == "espanol" || g_idioma == "spanish" || g_idioma == "castellano") g_idioma = "es";
        if (g_idioma == "ruso" || g_idioma == "russian" || g_idioma == "rus") g_idioma = "ru";
        if (g_idioma != "en" && g_idioma != "ru") g_idioma = "es";
    }
    g_idioma_actual = &g_idioma;
    std::string cuePath = argc > 1 ? argv[1] : "";
    if (cuePath.empty()) {

        namespace fs = std::filesystem;
        const fs::path exeDir = fs::absolute(argv[0]).parent_path();
        const std::string lang = g_idioma;
        std::vector<fs::path> cues;
        g_cues.clear();
        for (const auto& dir : {exeDir, exeDir.parent_path(), exeDir.parent_path().parent_path(), exeDir.parent_path().parent_path().parent_path(), fs::current_path()}) {
            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
                const auto ext = it->path().extension().string();
                if ((ext == ".cue" || ext == ".CUE") && std::find(cues.begin(), cues.end(), it->path()) == cues.end()) cues.push_back(it->path());
            }
        }

        auto isUsa = [](const fs::path& p) {
            try {
                auto img = sf::disc::Iso9660Image::open(p.string());
                const auto cnf = img.readFile("SYSTEM.CNF");
                const std::string t(reinterpret_cast<const char*>(cnf.data()), cnf.size());
                return t.find("SCUS_946.40") != std::string::npos;
            } catch (...) { return false; }
        };
        (void)lang;
        for (const auto& c : cues) if (isUsa(c)) g_cues.push_back(c.string());
        if (!g_cues.empty()) cuePath = g_cues.front();
    }
#ifdef SF3B_SDL
    const bool windowed = argc <= 2 || std::getenv("SF3N_TEST") != nullptr;
#else
    const bool windowed = false;
#endif
    if (cuePath.empty() && !windowed) { std::fprintf(stderr, "uso: sf3_boot <sf3.cue> [segundos] [log]\n"); return 1; }
    const double seconds = std::getenv("SF3N_TEST") ? std::atof(std::getenv("SF3N_TEST")) : argc > 2 ? std::atof(argv[2]) : (windowed ? 1e9 : 10.0);
    if (argc > 3) g_log = std::fopen(argv[3], "w");
    else if (windowed) { g_log = std::fopen(SF3_REGISTRO, "w"); if (!g_log) g_log = stderr; }
#ifdef SF3B_SDL
    std::unique_ptr<BootWindow> window;
    const bool wantLauncher = windowed && (std::getenv("SF3N_TEST") == nullptr || std::getenv("SF3N_LAUNCHERSHOT")) && std::getenv("SF3N_SIN_LANZADOR") == nullptr && !(argc > 1 && std::string(argv[argc - 1]) == "--sin-lanzador");
#ifdef _WIN32

    if (wantLauncher && std::getenv("SF3N_LANZADOR_JUEGO") == nullptr) {
        const bool play = natlaunch::run(BootWindow::dataDir() + "lanzador_fondo.bmp",
                                         [](const char* k, const char* d) { return BootWindow::iniGet(k, d); },
                                         [](const std::string& k, const std::string& v) { BootWindow::iniSave(k, v); });
        if (!play) return 0;
        { const std::string li = BootWindow::iniGet("idioma", g_idioma.c_str()); g_idioma = li == "en" ? "en" : li == "ru" ? "ru" : "es"; }
    }
#endif
    if (windowed) { window = std::make_unique<BootWindow>(); if (!window->open()) { std::fprintf(stderr, "no se pudo abrir la ventana\n"); return 1; } }
    marcaArranque("ventana abierta");

    std::string bootLoad = std::getenv("SF3N_LOAD") ? std::getenv("SF3N_LOAD") : "";
#ifdef _WIN32
    const bool sdlLauncher = std::getenv("SF3N_LANZADOR_JUEGO") != nullptr;
#else
    const bool sdlLauncher = true;
#endif
    if (window && wantLauncher && sdlLauncher) {
        if (g_cues.empty() && !cuePath.empty()) g_cues.push_back(cuePath);
        g_idioma_mut = &g_idioma;
        if (!window->runLauncher(g_cues, cuePath)) return 0;
        if (!window->boot_load.empty()) bootLoad = window->boot_load;
    }
#endif

    {
        std::string v = std::getenv("SF3N_VOCES") ? std::getenv("SF3N_VOCES") : "";
#ifdef SF3B_SDL
        if (v.empty()) v = BootWindow::iniGet("idioma_voces", "");
#endif
        for (auto& c : v) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (v != "en" && v != "es" && v != "ru") v = g_idioma == "en" ? "en" : "es";
        g_voces = v;
    }
    if (cuePath.empty()) {
        const char* msg = "No se encontro el disco USA de Syphon Filter 3 (SCUS_946.40, .cue y .bin). Ponlo en la carpeta SYPHON3 (o en una subcarpeta, p. ej. SYPHON3\\NTSC). El disco PAL ya no se usa.";
        std::fprintf(stderr, "%s\n", msg);
#ifdef SF3B_SDL
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Syphon Filter 3 - PC", msg, nullptr);
#endif
        return 1;
    }
    auto image = sf::disc::Iso9660Image::open(cuePath);
    marcaArranque("disco abierto");

    std::string bootExe = "?";
    try {
        const auto cnf = image.readFile("SYSTEM.CNF");
        std::string t(reinterpret_cast<const char*>(cnf.data()), cnf.size());
        if (auto b = t.find("BOOT"); b != std::string::npos) {
            auto s0 = t.find('\\', b), e0 = t.find(';', b);
            if (s0 != std::string::npos && e0 != std::string::npos && e0 > s0) bootExe = t.substr(s0 + 1, e0 - s0 - 1);
        }
    } catch (const std::exception&) {}
    if (bootExe != kProfileUsa.exe) {

        const std::string msg = "Este disco es " + bootExe + ". El port solo funciona con el disco USA de Syphon Filter 3 (SCUS_946.40); el castellano ya va incluido (textos_es.pak, voces_es.pak, videos_es_N.pak).";
        LOG("%s\n", msg.c_str());
#ifdef SF3B_SDL
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Syphon Filter 3 - PC", msg.c_str(), nullptr);
#endif
        std::fprintf(stderr, "%s\n", msg.c_str());
        return 1;
    }
    g_prof = &kProfileUsa;
#ifdef SF3B_SDL
    if (window) window->setDiscId(bootExe);
#endif
    auto exeBytes = image.readFile(bootExe);

    if (g_idioma == "ru") {
        const char* bp = SDL_GetBasePath();
        const std::string tp = std::getenv("SF3N_TEXTOS_PAK") ? std::getenv("SF3N_TEXTOS_PAK") : (std::filesystem::path(bp ? bp : "") / "textos_ru.pak").string();
        const int np = TextosEs::patchExe(tp, exeBytes);
        if (np < 0) { LOG("textos rusos: falta %s (textos en ingles)\n", tp.c_str()); g_idioma = "en"; }
        else LOG("textos rusos: %d parches del ejecutable (fuente)\n", np);
    }
    const auto exe = Executable::parse(exeBytes);
#ifdef SF3B_SDL
    if (window && (!window->gameFontReady() || g_idioma == "ru")) window->loadGameFont(exeBytes, true);
#endif
    BinMedia media(image.binaryPath().string());
    marcaArranque("exe del disco leido");
    {

        std::filesystem::path dir;
        if (const char* e = std::getenv("SF3N_PERSONAJES")) dir = e;
        else dir = pakfs::dataPath("personajes");
        marcaArranque("antes de personajes");
        const int n = virtualDisc().load(dir);
        marcaArranque("personajes.pak cargado");
#ifdef SF3B_SDL
        if (!std::getenv("SF3N_SIN_BANCO_ARMAS")) nat::collectWeaponMenuTims(image, g_log, pakfs::dataPath("armas") + "/menu");
#endif
        marcaArranque("fotos de armas");
        virtualDisc().layout(media.discSectors());
        if (n) LOG("personajes: %d archivos en %s\n", n, dir.string().c_str());
        extraRoster().setUsa(true);
        g_roster_ingles = g_idioma != "es";
        if (n) extraRoster().load(dir, g_log);
    }
#ifdef SF3B_SDL
    if (window) { window->image = &image; window->log = g_log; window->setFieldRate(59.94); }
#endif

    R3000Runtime cpu;
    PsxMachine m(cpu);
    CaptureGpu gpu;
    m.setCdRomMedia(&media);
    m.attachGpu(&gpu);
    m.attachDmaPort(DmaChannel::gpu, &gpu);
    PadSio pad(m);
    m.attachSio(&pad);
    static SoftMdec mdec;
    m.attachMdec(&mdec);
    m.attachDmaPort(DmaChannel::mdec_in, &mdec.in);
    m.attachDmaPort(DmaChannel::mdec_out, &mdec.out);
    g_mdec = &mdec;
    cpu.loadExecutable(exe);
    marcaArranque("ejecutable cargado");

    {
        double sp = 4.0;
        if (const char* e = std::getenv("SF3N_CD_RAPIDO")) sp = std::atof(e);
#ifdef SF3B_SDL
        else if (window) sp = BootWindow::iniDouble("cd_rapido", 4.0);
#endif
        CdRomController::data_speedup = unsigned(std::clamp(sp, 1.0, 16.0));
        LOG("CD: lectura de datos x%u\n", CdRomController::data_speedup);
    }
    static VoicePitch voicePitch;
    static std::atomic<double>* voiceSemis = nullptr;
    static double voiceSemisHeadless = 0;
    {
        std::uint32_t xaLo = 0, xaHi = 0;
        try { const auto e = image.find("SCENES1.XA"); xaLo = e.extent_lba; xaHi = e.extent_lba + (e.size + 2047) / 2048; } catch (...) {}
        double semis = 0.0;
        if (const char* e = std::getenv("SF3N_VOCES_TONO")) semis = std::atof(e);
#ifdef SF3B_SDL
        if (window) { window->voice_semitones = semis; voiceSemis = &window->voice_semitones; }
#endif
        voiceSemisHeadless = semis;
        LOG("voces: SCENES1.XA lba %u..%u, tono %+.1f semitonos\n", xaLo, xaHi, semis);
        static std::uint32_t lastLba = 0;

        static VocesEs vocesEs;
        if (xaHi > xaLo) {
            bool want = g_voces == "es";
            if (const char* e = std::getenv("SF3N_VOCES_ES")) want = std::atoi(e) != 0;
            const bool wantText = g_idioma == "es" || g_idioma == "ru";
            const char* basep = SDL_GetBasePath();
            const std::filesystem::path base = basep ? basep : "";
            const auto list = (base / "voces_es.txt").string();
            std::string pak = std::getenv("SF3N_VOCES_PAK") ? std::getenv("SF3N_VOCES_PAK") : (base / "voces_es.pak").string();
            g_textos.setLog(g_log);
            if (wantText) {
                const std::string tp = std::getenv("SF3N_TEXTOS_PAK") ? std::getenv("SF3N_TEXTOS_PAK") : (base / (g_idioma == "ru" ? "textos_ru.pak" : "textos_es.pak")).string();
                g_textos.ru = g_idioma == "ru";
                if (g_textos.ru) {
                    const char* kRu[][2] = {{"Flame Thrower", "ОГНЕМЁТ"}, {"Flame Thrower taken", "ВЗЯТ ОГНЕМЁТ"}, {"Flame Thrower ammo taken", "ОГНЕМЁТ: ВЗЯТО ТОПЛИВО"}, {"Flame Thrower ammo maxed", "ОГНЕМЁТ: МАКС. ТОПЛИВА"},
                        {"Gatling Gun", "ПУЛЕМЁТ ГАТЛИНГА"}, {"Gatling Gun taken", "ВЗЯТ ПУЛЕМЁТ ГАТЛИНГА"}, {"Gatling Gun ammo taken", "ГАТЛИНГ: ВЗЯТЫ ПАТРОНЫ"}, {"Gatling Gun ammo maxed", "ГАТЛИНГ: МАКС. ПАТРОНОВ"},
                        {"Hand Taser", "РУЧНОЙ ШОКЕР"}, {"Hand Taser taken", "ВЗЯТ РУЧНОЙ ШОКЕР"}, {"Hand Taser ammo taken", "ШОКЕР: ВЗЯТ ЗАРЯД"}, {"Hand Taser ammo maxed", "ШОКЕР: МАКС. ЗАРЯДА"},
                        {"Shotgun Stryker", "ДРОБОВИК СТРАЙКЕР"}, {"Shotgun Stryker taken", "ВЗЯТ ДРОБОВИК СТРАЙКЕР"}, {"Shotgun Stryker ammo taken", "СТРАЙКЕР: ВЗЯТЫ ПАТРОНЫ"}, {"Shotgun Stryker ammo maxed", "СТРАЙКЕР: МАКС. ПАТРОНОВ"}};
                    for (const auto& e : kRu) g_textos.addEntry(e[0], rufont::utf8ToRu(e[1]));
                } else {

                g_textos.addEntry("Flame Thrower", "Lanzallamas"); g_textos.addEntry("Flame Thrower taken", "Lanzallamas recogido");
                g_textos.addEntry("Flame Thrower ammo taken", "Lanzallamas: munici\xf3n recogida"); g_textos.addEntry("Flame Thrower ammo maxed", "Lanzallamas: m\xe1x. munici\xf3n");
                }
                if (!g_textos.open(tp, g_log)) LOG("textos castellano: falta %s\n", tp.c_str());
                else g_textos.placeFiles([&](const std::string& n, std::uint32_t& lba, std::uint32_t& size) {
                    try { const auto e = image.find(n); lba = e.extent_lba; size = std::uint32_t(e.size); return true; } catch (...) { return false; } });
            }
            if (want) {
                try {
                    const auto e = image.find("MOVIE1.HOG");
                    int parts = 0;
                    for (int k = 1; k <= 9; ++k) {
                        const auto vp = (base / ("videos_es_" + std::to_string(k) + ".pak")).string();
                        if (!pakfs::exists(vp)) break;
                        if (g_videos.open(vp, e.extent_lba, std::uint32_t((e.size + 2047) / 2048), g_log)) ++parts;
                    }
                    if (!parts) LOG("videos castellano: faltan videos_es_1.pak... (audio de los videos en ingles)\n");
                    else g_videos.loadOwn(pakfs::dataPath("videos_propios"), g_log);
                } catch (...) {}
            }
            if (g_voces == "ru") {

                const char* fn[3] = {"SCENES1.XA", "TRASH.XA", "MOVIE1.HOG"};
                for (int k = 0; k < 3; ++k) try { const auto e = image.find(fn[k]); g_xa_rng[k][0] = e.extent_lba; g_xa_rng[k][1] = e.extent_lba + std::uint32_t((e.size + 2047) / 2048); } catch (...) {}
                int parts = 0;
                for (int k = 1; k <= 9; ++k) {
                    const auto vp = (base / ("voces_ru_" + std::to_string(k) + ".pak")).string();
                    if (!pakfs::exists(vp)) break;
                    if (g_voces_ru.open(vp, 0, 0xffffffffu, g_log)) ++parts;
                }
                double pct = 100.0;
#ifdef SF3B_SDL
                if (window) pct = BootWindow::iniDouble("volumen_voces_ru", 100.0);
#endif
                pct = std::clamp(pct, 0.0, 300.0) / 100.0;
                for (auto& v : g_voces_ru_vol) v *= pct;
                if (!parts) LOG("voces rusas: faltan voces_ru_1.pak... (voces originales en ingles)\n");
                else LOG("voces rusas: %d trozos, volumen %.0f%%\n", parts, pct * 100.0);
            }
            if (!want) LOG("voces castellano: voces %s, textos %s\n", g_voces.c_str(), g_idioma.c_str());
            else if (pakfs::exists(pak) && vocesEs.openPack(pak, xaLo, xaHi, list, g_log)) {}
            else LOG("voces castellano: falta voces_es.pak (voces originales en ingles)\n");
        }

        {
            bool on = true; int delay = 3;
#ifdef SF3B_SDL
            if (window) { on = BootWindow::iniGet("menu_alta_calidad", "1") != "0"; delay = std::atoi(BootWindow::iniGet("titulo_alta_desfase", "3").c_str()); }
#endif
            if (const char* e = std::getenv("SF3N_MENU_ALTA")) on = std::atoi(e) != 0;
            if (const char* e = std::getenv("SF3N_TITULO_DESFASE")) delay = std::atoi(e);
            if (on) try {
                const auto e = image.find("MOVIE1.HOG");

                const char* bp = SDL_GetBasePath();
                if (g_title_hq.open(std::filesystem::path(bp ? bp : ""), e.extent_lba + 1 + 120208, e.extent_lba + 1 + 126728, std::clamp(delay, 0, 30), g_log)) {}
                else LOG("menu de alta calidad: no hay videos_propios\\TITLE_alta_1.mpg (video original)\n");
            } catch (...) {}
        }
        if (vocesEs.active() || g_textos.hasFiles() || g_videos.active() || g_voces_ru.active() || g_title_hq.active()) sf::psx::g_cd_sector_patch = [](std::uint32_t lba, unsigned char* raw) { if (vocesEs.active()) vocesEs.patchSector(lba, raw); g_textos.patchSector(lba, raw); g_videos.patchSector(lba, raw); g_voces_ru.patchSector(lba, raw); g_title_hq.onSector(lba, raw); };
        m.setXaFilter([xaLo, xaHi](std::uint32_t lba, std::span<SpuPcmFrame> pcm) {
            static std::uint64_t calls = 0;
            if (++calls % 20 == 1) LOG("voces: sector XA lba %u (%s)\n", lba, (lba >= xaLo && lba < xaHi) ? "SCENES1" : "otro");
            g_videos.filter(lba, pcm);
            if (g_voces_ru.active() && g_voces_ru.has(lba))
                for (int k = 0; k < 3; ++k) if (lba >= g_xa_rng[k][0] && lba < g_xa_rng[k][1]) {
                    const double g = g_voces_ru_vol[k];
                    for (auto& f : pcm) { f.left = std::int16_t(std::clamp(f.left * g, -32768.0, 32767.0)); f.right = std::int16_t(std::clamp(f.right * g, -32768.0, 32767.0)); }
                    break;
                }
            if (lba < xaLo || lba >= xaHi) return;
            vocesEs.filter(lba, pcm);
            const double st = voiceSemis ? voiceSemis->load() : voiceSemisHeadless;
            if (std::fabs(st) < 0.01) return;
            voicePitch.setFactor(std::pow(2.0, st / 12.0));
            if (lba < lastLba || lba > lastLba + 64) voicePitch.reset();
            lastLba = lba;
            voicePitch.process(pcm);
        });
    }
#ifdef SF3B_SDL
    if (window) window->setWeaponAddrs(0x8012d880u, 0x8012d7e0u);
    if (window) { window->frame_source = &gpu.menu_frame; window->frame_seq = &gpu.menu_frame_seq; window->frame_vram_source = &gpu.menu_frame_vram; }
    if (window) {
        window->initWeaponBank();
        if (window->weapon_bank_.ok) { g_bank_init_order = window->weaponBankOrder(); g_bank_v192_order = window->weaponBankOrderV192(); }
        auto arms = cpu.armsRam();
        for (const auto& bm : window->weapon_bank_.models) {
            const std::size_t o = bm.addr - 0x80600000u;
            if (o + bm.bytes.size() <= arms.size()) std::copy(bm.bytes.begin(), bm.bytes.end(), arms.begin() + std::ptrdiff_t(o));
        }

        if (window->weapon_sounds_.ok) {
            sf::psx::g_spu_ext = window->weapon_sounds_.ext;
            for (const auto& [sent, ext] : window->weapon_sounds_.redirect) sf::psx::g_spu_redirect[sent >> 4] = ext;
            std::uint32_t at = 0x00090000u;
            for (auto& [w, ws] : window->weapon_sounds_.by_weapon) {
                g_weapon_tone_addr[w] = 0x80600000u + at;
                for (const auto& tr : ws.tones) { for (int i = 0; i < 24; ++i) arms[at + std::uint32_t(i)] = std::byte(tr[std::size_t(i)]); at += 24; }
            }
        }
    }
    if (window) { window->setRam(cpu.ram()); window->soft_vram_ = &gpu.soft.vram; window->player_ptr_addr_ = g_prof->player_pointer; window->hud_source = &gpu.hud; window->hud_valid = &gpu.hud_valid; gpu.on_world = [w = window.get()](const auto& seg) { w->onWorldList(seg); }; gpu.on_scope = [w = window.get()](const auto& seg) { return w->classifyActorPolys(seg); }; }
#endif

    auto w32 = [&](std::uint32_t a, std::uint32_t v) { (void)cpu.write32(a, v); };
    w32(kTrapA0, breakInsn(1)); w32(kTrapA0 + 4, 0x03e00008); w32(kTrapA0 + 8, 0);
    w32(kTrapB0, breakInsn(2)); w32(kTrapB0 + 4, 0x03e00008); w32(kTrapB0 + 8, 0);
    w32(kTrapC0, breakInsn(3)); w32(kTrapC0 + 4, 0x03e00008); w32(kTrapC0 + 8, 0);
    w32(kTrapIrq, breakInsn(4));

    w32(0x60, 0x2);

    const auto& h = exe.header();
    cpu.reset(h.initial_pc, h.initial_gp, 0x801ffff0u);
    {
        auto s = cpu.state();
        s.cop0_status = 0x40000000u;
        cpu.restoreCpuState(s);
    }
    LOG("SF3 EXE: pc %08x gp %08x text %08x..%08x\n", h.initial_pc, h.initial_gp, h.text_address, h.text_address + h.text_size);

    Bios bios{cpu, m, gpu};

    if (std::getenv("SF3N_SIN_TARJETA") == nullptr) {
#ifdef SF3B_SDL
        const std::string cardPath = window ? BootWindow::dataDir() + SF3_PARTIDAS "/" + bootExe + "/tarjeta1.mcr" : std::string("tarjeta1.mcr");
#else
        const std::string cardPath = "tarjeta1.mcr";
#endif

        const std::string storeDir = cardPath.substr(0, cardPath.find_last_of("/\\") + 1) + "tarjeta/";
        bios.store.open(storeDir);
        bios.store.importMcr(cardPath, "");
        bios.store_ok = true;
        bios.rebuildView();
        LOG("tarjeta de memoria: %s (%zu partidas, ajustes %s)\n", storeDir.c_str(), bios.store.slots().size(), bios.card.find(CardStore::kSettings) >= 0 ? "si" : "no");
    }
    bios.pad = &pad;
    const bool movieSkip = std::getenv("SF3B_NOMOVIES") != nullptr;
    const std::uint32_t movieSkipResult = std::getenv("SF3B_MOVIERET") ? std::uint32_t(std::atoi(std::getenv("SF3B_MOVIERET"))) : 0u;
    const std::uint64_t ticksPerField = std::uint64_t(PsxMachine::cpu_clock_hz / 59.94);
    const std::uint64_t linesPerField = 263;
    std::uint64_t nextVblank = m.currentTick() + ticksPerField;
    std::uint64_t nextHblank = m.currentTick() + ticksPerField / linesPerField;
    std::uint64_t steps = 0, fields = 0;
    std::uint64_t seen_flips = 0;
    std::map<std::uint32_t, std::uint64_t> hot, isrHot, gameHot;
    std::uint64_t samples = 0, inIsr = 0, irqOff = 0;
    const auto totalFields = std::uint64_t(seconds * 50);
    std::uint64_t lastFlips = 0;

    std::string currentFog;
    bios.fog_now = [&] { return currentFog; };
    auto statePath = [&](int slot) {
        const char* base = SDL_GetBasePath();
        std::string d = std::string(base ? base : "") + "estados";
        std::filesystem::create_directories(d);
        return d + "/estado" + std::to_string(slot) + ".sf3s";
    };
    auto serialize = [&](Ser& sr) {
        std::uint32_t magic = 0x53334653u, version = 6; sr(magic); sr(version);
        if (magic != 0x53334653u || version < 1 || version > 6) { sr.ok = false; return; }
        std::vector<char> fog(currentFog.begin(), currentFog.end()); sr(fog);
        if (!sr.out) currentFog.assign(fog.begin(), fog.end());
        auto cs = cpu.state(); sr(cs);
        std::vector<std::byte> ram(cpu.ram().begin(), cpu.ram().end()); sr(ram);
        std::vector<std::byte> sp(cpu.scratchpad().begin(), cpu.scratchpad().end()); sr(sp);
        std::vector<std::byte> io(cpu.mmio().begin(), cpu.mmio().end()); sr(io);
        auto ms = m.captureState();
        sr(ms.cpu_clock_scale); sr(ms.scheduler); sr(ms.interrupts); sr(ms.dma); sr(ms.cdrom); sr(*ms.spu);
        sr(ms.xa_decoder); sr(ms.timers); sr(ms.pending_cpu_ticks); sr(ms.device_tick_remainder);
        gpu.state(sr); pad.state(sr); bios.state(sr);
        sr(nextVblank); sr(nextHblank); sr(fields);
        if (version >= 2) pad.state2(sr);
        else if (!sr.out) pad.legacyState();

        if (version >= 3) { sr(CdRomController::commands_written); sr(CdRomController::commands_executed); }
        else if (!sr.out) { CdRomController::commands_written = CdRomController::commands_executed = fields < 50 * 20 ? 0 : 1000; }

        std::vector<std::byte> extra;
        if (sr.out) extra.assign(cpu.extraRam().begin(), cpu.extraRam().end());
        if (version >= 4) sr(extra);

        std::vector<std::byte> armsSnd;
        if (sr.out) armsSnd.assign(cpu.armsRam().begin() + 0x70000, cpu.armsRam().begin() + 0xA0000);
        if (version >= 5) sr(armsSnd);
        if (!sr.out && sr.ok) {
            const bool r1 = cpu.restoreRam(ram), r2 = cpu.restoreScratchpad(sp), r3 = cpu.restoreMmio(io), r4 = m.restoreState(ms);
            if (!(r1 && r2 && r3 && r4)) LOG("estado: ram %d scratch %d mmio %d maquina %d\n", int(r1), int(r2), int(r3), int(r4));
            sr.ok = r1 && r2 && r3 && r4;
            cpu.restoreCpuState(cs);
            if (sr.ok) { if (version < 4) extra.assign(sf::psx::R3000Runtime::extra_ram_size, std::byte{0}); (void)cpu.restoreExtraRam(extra); g_textos.resync(cpu); }
            if (sr.ok && version >= 5 && armsSnd.size() == 0x30000) std::copy(armsSnd.begin(), armsSnd.end(), cpu.armsRam().begin() + 0x70000);
#ifdef SF3B_SDL

            if (sr.ok && version >= 5 && window && window->weapon_sounds_.ok) {
                auto arms = cpu.armsRam();
                bool same = true;
                for (const auto& [w, ws] : window->weapon_sounds_.by_weapon) {
                    auto ta = g_weapon_tone_addr.find(w); if (ta == g_weapon_tone_addr.end()) continue;
                    std::size_t o = ta->second - 0x80600000u;
                    for (const auto& tr : ws.tones) { for (int i = 0; i < 24 && same; ++i) if (std::to_integer<std::uint8_t>(arms[o + std::size_t(i)]) != tr[std::size_t(i)]) same = false; o += 24; }
                }
                if (!same) {
                    for (const auto& [w, ws] : window->weapon_sounds_.by_weapon) {
                        auto ta = g_weapon_tone_addr.find(w); if (ta == g_weapon_tone_addr.end()) continue;
                        std::size_t o = ta->second - 0x80600000u;
                        for (const auto& tr : ws.tones) { for (int i = 0; i < 24; ++i) arms[o + std::size_t(i)] = std::byte(tr[std::size_t(i)]); o += 24; }
                    }
                    std::uint32_t mMagic = 0, mBase = 0, mHdr = 0, hdr = 0; std::uint16_t ng = 0;
                    (void)cpu.read32(0x8067fff0u, mMagic); (void)cpu.read32(0x8067fff4u, mBase); (void)cpu.read32(0x8067fff8u, mHdr); (void)cpu.read32(0x8013887cu, hdr);
                    int dead = 0;
                    if (mMagic == 0x42444e53u && mHdr == hdr && hdr) {
                        (void)cpu.read16(hdr + 0x1a, ng);
                        for (std::uint32_t k = mBase; k < ng && k < 256; ++k) { for (int i = 0; i < 8; ++i) (void)cpu.write8(0x80680000u + 8u * k + std::uint32_t(i), 0); ++dead; }
                    }
                    LOG("estado: tonos de disparo del banco distintos a los del estado: reescritos (%d grupos propios anulados)\n", dead);
                }
            }
#endif
        }

        std::vector<char> bankOrder;
        if (sr.out && window) { for (const auto& n : window->weapon_bank_.ok ? window->weaponBankOrder() : std::vector<std::string>{}) { bankOrder.insert(bankOrder.end(), n.begin(), n.end()); bankOrder.push_back(','); } }
        if (version >= 6) sr(bankOrder);
        if (!sr.out && sr.ok && window && window->weapon_bank_.ok && !std::getenv("SF3N_BANCO_SIN_ORDEN")) {
            std::vector<std::string> order;
            if (version >= 6 && !std::getenv("SF3N_PRUEBA_ORDEN_HEUR")) { std::string t(bankOrder.begin(), bankOrder.end()); std::size_t p = 0; while (p < t.size()) { auto q = t.find(',', p); if (q == std::string::npos) q = t.size(); if (q > p) order.push_back(t.substr(p, q - p)); p = q + 1; } }
            else {

                static const auto initOrder = g_bank_init_order;
                const auto la = window->weaponBankLayout(initOrder), lb = window->weaponBankLayout(g_bank_v192_order);
                std::set<std::uint32_t> sa, sb; for (auto& [n, a] : la) sa.insert(a); for (auto& [n, a] : lb) sb.insert(a);
                int ha = 0, hb = 0; const auto r = cpu.ram();
                for (std::size_t o = 0; o + 4 <= r.size(); o += 4) {
                    const std::uint32_t w = std::to_integer<std::uint32_t>(r[o]) | (std::to_integer<std::uint32_t>(r[o + 1]) << 8) | (std::to_integer<std::uint32_t>(r[o + 2]) << 16) | (std::to_integer<std::uint32_t>(r[o + 3]) << 24);
                    if ((w >> 20) != 0x806u) continue;
                    const bool ia = sa.count(w), ib = sb.count(w);
                    if (ia && !ib) ++ha; else if (ib && !ia) ++hb;
                }
                order = hb > ha ? g_bank_v192_order : initOrder;
                LOG("estado: banco de armas sin orden guardado; punteros actual %d v192 %d -> %s\n", ha, hb, hb > ha ? "v192" : "actual");
            }
            if (!order.empty() && window->relayoutWeaponBank(order)) {
                auto arms = cpu.armsRam();
                std::fill(arms.begin(), arms.begin() + 0x70000, std::byte{0});
                for (const auto& bm : window->weapon_bank_.models) { const std::size_t o = bm.addr - 0x80600000u; if (bm.addr && o + bm.bytes.size() <= 0x70000u) std::copy(bm.bytes.begin(), bm.bytes.end(), arms.begin() + std::ptrdiff_t(o)); }
                LOG("estado: banco de armas reordenado como al guardar\n");
            }
        }
    };
    auto saveStateTo = [&](const std::string& path) {
        std::vector<char> buf; Ser sr; sr.out = &buf; serialize(sr);
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { LOG("estado: no se pudo escribir %s\n", path.c_str()); return false; }
        std::fwrite(buf.data(), 1, buf.size(), f); std::fclose(f);
        LOG("estado: guardado %s (%zu bytes, t=%.1fs)\n", path.c_str(), buf.size(), double(fields) / 50.0);
        return true;
    };
    auto loadState = [&](const std::string& path) {
        std::vector<char> buf;
        if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fseek(f, 0, SEEK_END); buf.resize(std::size_t(std::ftell(f))); std::fseek(f, 0, SEEK_SET); if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear(); std::fclose(f); }
        if (buf.empty()) { LOG("estado: no existe %s\n", path.c_str()); return false; }
        Ser sr; sr.in = buf.data(); sr.size = buf.size(); serialize(sr);
        ++g_idle_epoch;
        LOG("estado: %s %s (t=%.1fs)\n", sr.ok ? "cargado" : "ERROR al cargar", path.c_str(), double(fields) / 50.0);
#ifdef SF3B_SDL
        if (sr.ok && window) window->afterStateLoad(currentFog);
#endif
        return sr.ok;
    };
#ifdef SF3B_SDL

    struct CardFlow {
        std::uint64_t load_list = 0, save_list = 0, require = 0, saving = 0;
        std::string typed;
        bool quit_seen = false;
        bool list_fresh = false;
        bool list_on = false;
        bool redraw = false;
        int del_pending = -1;
        std::uint32_t del_cursor = 0;
        int keep_cursor = -1;
        std::vector<char> run_snap, pause_snap;
        std::string run_fog, pause_fog;
        std::uint8_t prev_mode = 0xff;
    } cardFlow;
    auto slotText = [&](const CardStore::Slot& sl) {
        std::string t = sl.nombre.empty() ? std::string("SIN NOMBRE") : sl.nombre;
        if (!sl.mision.empty()) t += "  -  " + BootWindow::missionName(sl.mision);
        if (sl.fecha) t += "  -  " + BootWindow::dateText(std::time_t(sl.fecha));
        return t;
    };
    auto cardPick = [&](bool save) {
        auto slots = bios.store.slots();
        std::vector<std::string> items;
        auto makeItems = [&] {
            items.clear();
            if (save) items.push_back("NUEVA PARTIDA GUARDADA");
            for (const auto& sl : slots) items.push_back(slotText(sl));
        };
        makeItems();
        int choice = 0;
        if (const char* e = std::getenv("SF3N_TARJETA_ELIGE")) choice = std::atoi(e);
        else if (window && std::getenv("SF3N_TEST") == nullptr) {
            window->openCardPicker(save, items);
            while (!window->quit && window->cardPickResult() == -99) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                if (const int d = window->takeCardDelete(); d >= 0) {
                    const int si = save ? d - 1 : d;
                    if (si >= 0 && si < int(slots.size())) {
                        LOG("tarjeta: borrada la ranura %d (%s)\n", slots[std::size_t(si)].id, slots[std::size_t(si)].archivo.c_str());
                        bios.store.remove(slots[std::size_t(si)].id);
                        slots = bios.store.slots();
                        makeItems();
                        window->setCardItems(items);
                    }
                }
            }
            choice = window->cardPickResult();
        }
        const int k = bios.view_saves;
        std::vector<std::pair<std::uint16_t, int>> seq{{0, 20}};
        auto press = [&](std::uint16_t b) { seq.push_back({b, 4}); seq.push_back({0, 4}); };
        if (save) {
            if (choice == 0) { bios.target_slot = -1; for (int i = 0; i < k; ++i) press(0x0040); press(0x4000); }
            else if (choice >= 1 && choice <= int(slots.size())) {
                const int id = slots[std::size_t(choice - 1)].id;
                bios.alias_name = bios.view_name;
                bios.rebuildView(id, bios.view_name);
                bios.target_slot = id;
                press(0x4000);
            } else { for (int i = 0; i <= k; ++i) press(0x0040); press(0x4000); }
        } else {
            if (choice >= 0 && choice < int(slots.size())) { bios.rebuildView(slots[std::size_t(choice)].id, bios.view_name); press(0x4000); }
            else { for (int i = 0; i < k; ++i) press(0x0040); press(0x4000); }
        }
        LOG("tarjeta: lista propia (%s, %zu partidas) -> %d\n", save ? "guardar" : "cargar", slots.size(), choice);
        if (window) window->injectPadSeq(seq);
    };
    auto writeAutosave = [&] {
        if (!window) return;
        const std::string p = window->autoPath();
        if (FILE* f = std::fopen(p.c_str(), "wb")) {
            std::fwrite(cardFlow.pause_snap.data(), 1, cardFlow.pause_snap.size(), f); std::fclose(f);
            window->onStateSaved(p, cardFlow.pause_fog);
            LOG("autoguardado: %s (%s)\n", p.c_str(), cardFlow.pause_fog.c_str());
            window->notify(window->lang() == "en" ? "Autosaved" : "Autoguardado");
        }
        cardFlow.pause_snap.clear();
    };

    constexpr std::uint32_t kListBase = 0x80158f30u, kListCursor = 0x80157424u;

    auto setListTexts = [&] {
        auto& dyn = g_textos.dyn;
        const int pages = bios.view_pages;
        auto withPage = [&](std::string t) {
            if (pages > 1) t += "  (" + std::to_string(bios.view_page + 1) + "/" + std::to_string(pages) + ")";
            if (cardFlow.del_pending >= 0 && cardFlow.del_pending < int(bios.view_ids.size())) {
                const auto& nm = bios.view_ids[std::size_t(cardFlow.del_pending)].first;
                t = "\xbf" "Borrar " + (nm.size() > 12 ? nm.substr(12) : nm) + "? SUPR otra vez: s\xed";
            }
            return t;
        };
        const std::string help = pages > 1 ? "\r\nIZQ/DER: p\xe1gina - SUPR: borrar" : "\r\nSUPR: borrar";
        const std::string ts = withPage("Escoge d\xf3nde guardar") + help, tl = withPage("Escoge una partida para cargar") + help;
        dyn["Choose save slot\r\n"] = ts + "\r\n";
        dyn["Choose save slot\r\n\r\n"] = ts + "\r\n\r\n";
        dyn["Choose saved game to load\r\n"] = tl + "\r\n";
        dyn["Choose saved game to load\r\n\r\n"] = tl + "\r\n\r\n";
    };
    auto showPage = [&](int page) {
        bios.view_page = page;
        bios.rebuildView();
        {

            bios.dir_pattern = "*"; bios.dir_next = 0;
            std::uint32_t dst = kListBase;
            int n = 0, saves = 0, blocks = 0;
            std::uint16_t bits = 0;
            while (n < 15 && bios.fillDirentry(dst)) {
                char nm[21] = {};
                for (int i = 0; i < 20; ++i) { std::uint8_t c = 0; (void)cpu.read8(dst + std::uint32_t(i), c); nm[i] = char(c); }
                const std::string name(nm);
                if (name == CardStore::kSettings) ++blocks;
                else {
                    const std::string shortName = name.size() > 12 ? name.substr(12) : name;
                    for (std::size_t i = 0; i <= shortName.size(); ++i) (void)cpu.write8(dst + std::uint32_t(i), i < shortName.size() ? std::uint8_t(shortName[i]) : 0);
                    bits = std::uint16_t(bits | (1u << n));
                    ++saves; blocks += 2;
                }
                dst += 0x28; ++n;
            }
            for (int k = n; k < 15; ++k, dst += 0x28) for (std::uint32_t i = 0; i < 0x28; i += 4) (void)cpu.write32(dst + i, 0);
            (void)cpu.write8(0x80158dc0u, std::uint8_t(blocks));
            (void)cpu.write8(0x80158dc1u, std::uint8_t(saves));
            (void)cpu.write8(0x80158dc2u, std::uint8_t(bits & 0xff));
            (void)cpu.write8(0x80158dc3u, std::uint8_t(bits >> 8));
            (void)cpu.write32(0x80158dc8u, std::uint32_t(n));
        }

        g_list_redraw = true;
        cardFlow.redraw = true;
        cardFlow.del_pending = -1;
        LOG("tarjeta: lista del juego, pagina %d de %d (%d partidas)\n", bios.view_page + 1, bios.view_pages, bios.view_saves);
    };
    auto listTick = [&] {
        if (!bios.game_list || !window) return;
        const bool saving = cardFlow.save_list >= cardFlow.load_list;
        if (cardFlow.list_on) { std::uint32_t scr = 0; (void)cpu.read32(0x8015741cu, scr); if (scr != 4 && !g_list_redraw) cardFlow.list_on = false; }
        const bool on = cardFlow.list_on;
        window->setGameList(on);
        if (static bool prevOn = false; on != prevOn) { prevOn = on; LOG("tarjeta: lista del juego %s (t=%.2f)\n", on ? "en pantalla" : "fuera", double(fields) / 50.0); }
        auto& dyn = g_textos.dyn;
        static bool wasOn = false;
        if (!on) {
            if (wasOn && !dyn.empty()) dyn.clear();
            wasOn = false;
            (void)window->takeListPage(); (void)window->takeListDelete();
            return;
        }
        wasOn = true;
        if (cardFlow.list_fresh) {
            cardFlow.list_fresh = false;
            if (bios.view_page != 0) showPage(0);
        }
        int req = window->takeListPage();
        bool del = window->takeListDelete();
        if (static const char* lp = std::getenv("SF3N_LISTA_PRUEBA"); lp) {
            std::string s(lp);
            for (std::size_t p = 0; p < s.size();) {
                auto q = s.find(',', p); if (q == std::string::npos) q = s.size();
                const auto it = s.substr(p, q - p); const auto c = it.find(':');
                if (c != std::string::npos && fields == std::uint64_t(std::atof(it.c_str()) * 50)) { const char k = it[c + 1]; if (k == 'p') req = 1; if (k == 'm') req = -1; if (k == 'd') del = true; }
                p = q + 1;
            }
        }
        if (req && bios.view_pages > 1) showPage((bios.view_page + req + bios.view_pages) % bios.view_pages);
        std::uint32_t cur = 0;
        (void)cpu.read32(kListCursor, cur);
        if (cardFlow.keep_cursor >= 0 && !cardFlow.redraw && !g_list_redraw) {
            (void)cpu.write32(kListCursor, std::uint32_t(cardFlow.keep_cursor)); cur = std::uint32_t(cardFlow.keep_cursor); cardFlow.keep_cursor = -1;
        }
        if (cardFlow.del_pending >= 0 && cur != cardFlow.del_cursor && cardFlow.keep_cursor < 0 && !cardFlow.redraw) {
            cardFlow.del_pending = -1; setListTexts(); g_list_redraw = true; cardFlow.redraw = true; cardFlow.keep_cursor = int(cur);
        }
        if (del) {

            int base = 0;
            { char nm[21] = {}; for (int i = 0; i < 20; ++i) { std::uint8_t c = 0; (void)cpu.read8(kListBase + std::uint32_t(i), c); nm[i] = char(c); } if (std::string(nm) == CardStore::kSettings) base = 1; }
            const int si = int(cur) - base;
            if (si >= 0 && si < int(bios.view_ids.size())) {
                if (cardFlow.del_pending == si) {
                    const auto [nm, id] = bios.view_ids[std::size_t(si)];
                    LOG("tarjeta: borrada la ranura %d (%s) desde la lista del juego\n", id, nm.c_str());
                    bios.store.remove(id);
                    showPage(bios.view_page);
                    if (window) window->menuSoundPub(true);
                } else {
                    cardFlow.del_pending = si; cardFlow.del_cursor = cur; if (window) window->menuSoundPub(false);
                    setListTexts(); g_list_redraw = true; cardFlow.redraw = true; cardFlow.keep_cursor = int(cur);
                }
            }
        }
        setListTexts();
    };
    g_textos.on_text = [&](const std::string& s) {
        if (!bios.store_ok) return;
        auto has = [&](const char* t) { return s.find(t) != std::string::npos; };

        if (bios.game_list && (has("Checking MEMORY CARD") || has("Do you wish to save your progress")) && !cardFlow.list_on) {
            bios.view_page = 0; bios.view_pages = int(bios.store.pages(bios.kPagePer).size()); cardFlow.del_pending = -1; setListTexts();
        }
        if (has("Choose save slot") || has("Choose saved game to load")) cardFlow.list_on = true;
        else if (cardFlow.list_on && !(has("-- New Slot --") || has("Cancel") || has("change selection") || has("%x continue") || (s.size() <= 8 && s.find('\r') == std::string::npos))) cardFlow.list_on = false;
        if (has("Choose saved game to load")) {
            if (cardFlow.redraw) cardFlow.redraw = false;
            else if (fields > cardFlow.load_list + 25) { if (!bios.game_list) cardPick(false); else { cardFlow.list_fresh = true; cardFlow.del_pending = -1; } }
            cardFlow.load_list = fields;
        }
        else if (has("Choose save slot")) {
            if (cardFlow.redraw) cardFlow.redraw = false;
            else if (fields > cardFlow.save_list + 25) { if (!bios.game_list) cardPick(true); else { cardFlow.list_fresh = true; cardFlow.del_pending = -1; } }
            cardFlow.save_list = fields;
        }
        else if (has("Now saving")) cardFlow.saving = fields;
        else if (has("This will require") && (bios.target_slot == -1 || bios.game_list)) {
            if (fields > cardFlow.require + 25 && window) window->injectPadSeq({{0, 15}, {0x0020, 4}, {0, 15}, {0x4000, 4}, {0, 4}});
            cardFlow.require = fields;
        }
        else if (has("Do you want to quit?")) cardFlow.quit_seen = true;
        else if (has("Mission Failed")) {
            static std::uint64_t lastMf = 0;
            if (fields > lastMf + 100) { std::uint32_t pl = 0, act = 0; (void)cpu.read32(g_prof->player_pointer, pl); if (pl >= 0x80000000u && pl < 0x80200000u) (void)cpu.read32(pl + 0x1d8, act); LOG("mision fallida: accion del jugador %u (t=%.2fs)\n", act, double(fields) / 50.0); }
            lastMf = fields;
        }
        else if (has("Do you wish to save your progress")) {
            if (cardFlow.quit_seen && !cardFlow.pause_snap.empty()) writeAutosave();
            cardFlow.quit_seen = false;
        }
    };
    bios.on_dir = [&] {
        if (!cardFlow.list_on && bios.view_page != 0) { bios.view_page = 0; bios.rebuildView(); }
        cardFlow.del_pending = -1;
        setListTexts();
    };
    auto cardFieldTick = [&] {
        std::uint8_t mode = 0;
        (void)cpu.read8(0x80121b88u, mode);
        if (mode == 0 && window->inMission() && fields % 10 == 0) {
            cardFlow.run_snap.clear();
            Ser sr; sr.out = &cardFlow.run_snap; serialize(sr);
            cardFlow.run_fog = currentFog;
        }
        if (mode == 9 && cardFlow.prev_mode == 0 && !cardFlow.run_snap.empty()) {
            cardFlow.pause_snap = cardFlow.run_snap; cardFlow.pause_fog = cardFlow.run_fog; cardFlow.quit_seen = false;
        }
        if (mode == 0 && cardFlow.prev_mode != 0) { cardFlow.pause_snap.clear(); cardFlow.quit_seen = false; }
        if (mode == 0) cardFlow.list_on = false;
        cardFlow.prev_mode = mode;
        listTick();

        bool nameOn = false;
        const std::uint64_t lastList = std::max(cardFlow.save_list, cardFlow.require);
        std::uint8_t cur = 0;
        (void)cpu.read8(0x80157424u, cur);
        if (lastList && fields > lastList + 2 && fields < lastList + 50 * 600 && cardFlow.saving < lastList && cur < 8) {
            nameOn = true;
            for (std::uint32_t i = 0; i < 8 && nameOn; ++i) {
                std::uint8_t c0 = 0, c1 = 0, c2 = 0, c3 = 1;
                (void)cpu.read8(0x80158de0u + 4 * i, c0); (void)cpu.read8(0x80158de1u + 4 * i, c1);
                (void)cpu.read8(0x80158de2u + 4 * i, c2); (void)cpu.read8(0x80158de3u + 4 * i, c3);
                nameOn = c0 == ' ' && c2 == ' ' && c3 == 0 && c1 >= 0x20 && c1 < 0x7f;
            }
        }
        window->setNameEntry(nameOn);
        if (static const bool nl = std::getenv("SF3N_NOMBRELOG") != nullptr; nl) { static int last = -1; std::uint8_t c = 0; (void)cpu.read8(0x80158de1u + 4 * cur, c); if (c != last) { LOG("nombre: casilla %u = '%c' (%u) on=%d\n", cur, c, c, int(nameOn)); last = c; } }
        if (static const char* np = std::getenv("SF3N_NOMBRE_PRUEBA"); np) {
            double t = 0; char txt[64] = {};
            if (std::sscanf(np, "%lf:%63s", &t, txt) == 2 && fields == std::uint64_t(t * 50)) { std::string v(txt); for (auto& c : v) { if (c == '|') c = '\n'; if (c == '<') c = '\b'; } window->addTyped(v); }
        }
        if (nameOn) {

            cardFlow.typed += window->takeTyped();
            if (!cardFlow.typed.empty() && window->padSeqIdle()) {
                const char ch = cardFlow.typed.front();
                cardFlow.typed.erase(0, 1);
                std::uint8_t cell = '_';
                (void)cpu.read8(0x80158de1u + 4 * cur, cell);
                std::vector<std::pair<std::uint16_t, int>> seq;
                auto press = [&](std::uint16_t b) { seq.push_back({b, 4}); seq.push_back({0, 4}); };
                if (ch == '\n') press(0x4000);
                else if (ch == '\b') {
                    if (cell == '_' && cur > 0) { press(0x0080); cardFlow.typed.insert(0, 1, '\x01'); }
                    else { (void)cpu.write8(0x80158de1u + 4 * cur, 'A'); press(0x0040); }
                } else if (ch == '\x01') { (void)cpu.write8(0x80158de1u + 4 * cur, 'A'); press(0x0040); }
                else {

                    static const char kCycle[] = "_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
                    const char* p = std::strchr(kCycle, ch);
                    if (p && ch) {
                        const char prev = p == kCycle ? kCycle[36] : p[-1];
                        (void)cpu.write8(0x80158de1u + 4 * cur, std::uint8_t(prev));
                        press(0x0010);
                        if (cur < 7) press(0x0020);
                    }
                }
                if (!seq.empty()) window->injectPadSeqSoft(seq);
            }
        } else { (void)window->takeTyped(); cardFlow.typed.clear(); }
    };

    auto deathTick = [&] {
        static std::uint32_t lastPl = 0, lastAct = 0;
        static std::uint64_t lastLoadField = 0, lastEpoch = ~0ull;
        if (g_idle_epoch != lastEpoch) { lastEpoch = g_idle_epoch; lastLoadField = fields; lastPl = 0; }
        std::uint8_t mode = 0xff;
        (void)cpu.read8(0x80121b88u, mode);
        std::uint32_t pl = 0, act = 0;
        (void)cpu.read32(g_prof->player_pointer, pl);
        if (pl >= 0x80000000u && pl < 0x80200000u) (void)cpu.read32(pl + 0x1d8, act);
        const bool died = mode == 0 && pl && pl == lastPl && (act == 16 || act == 17) && lastAct != 16 && lastAct != 17 && fields > lastLoadField + 2;
        if (static const bool dl = std::getenv("SF3N_GRITOLOG") != nullptr; dl && (act != lastAct || pl != lastPl)) LOG("grito: accion %u (antes %u) pl %08x (antes %08x) modo %u campo %llu carga %llu\n", act, lastAct, pl, lastPl, unsigned(mode), (unsigned long long)fields, (unsigned long long)lastLoadField);
        lastPl = pl; lastAct = act;
        if (!died || !window || std::getenv("SF3N_SIN_GRITO")) return;

        std::string fogStem = currentFog;
        if (auto k = fogStem.find_last_of("/\\"); k != std::string::npos) fogStem = fogStem.substr(k + 1);
        fogStem = fogStem.substr(0, fogStem.find('.'));
        for (auto& c : fogStem) c = char(std::toupper((unsigned char)c));
        std::string lista = BootWindow::iniGet("misiones_mujer");
        if (lista.empty()) lista = "AFGHAN2,JUNGLE2,MCAVES,RIDGE,SNOWCAMP,TRIAGE1,TRIAGE2";
        for (auto& c : lista) c = char(std::toupper((unsigned char)c));
        bool female = false;
        for (std::size_t p0 = 0; p0 <= lista.size();) {
            auto q = lista.find_first_of(", ;", p0); if (q == std::string::npos) q = lista.size();
            if (q > p0 && lista.compare(p0, q - p0, fogStem) == 0) female = true;
            p0 = q + 1;
        }
        if (currentFog.find("FOG2") != std::string::npos) {
            std::uint32_t w = 0; (void)cpu.read32(0x8012d6acu + 8, w);
            female = (w & 0x1000u) != 0;
        }
        const std::string model = fogStem;
        const char* basep = SDL_GetBasePath();
        const std::filesystem::path dir = std::filesystem::path(pakfs::dataPath("sonidos")) / (female ? "muerte_mujer" : "muerte_hombre");
        (void)basep;
        std::vector<std::string> wavs;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            auto ext = e.path().extension().string();
            for (auto& c : ext) c = char(std::tolower((unsigned char)c));
            if (ext == ".wav") wavs.push_back(e.path().string());
        }
        if (wavs.empty()) { LOG("grito: no hay .wav en %s\n", dir.string().c_str()); return; }
        std::sort(wavs.begin(), wavs.end());
        static unsigned seed = unsigned(std::time(nullptr));
        seed = seed * 1103515245u + 12345u;
        const auto& w = wavs[(seed >> 16) % wavs.size()];
        const bool ok = window->playWav(w);
        LOG("grito de muerte (%s, mision %s): %s%s (t=%.2fs)\n", female ? "mujer" : "hombre", model.empty() ? "?" : model.c_str(), w.c_str(), ok ? "" : " (sin salida de audio)", double(fields) / 50.0);
    };
#endif

    struct RecSeg { bool valid = false; std::vector<char> state; std::array<std::uint8_t, 6> pad{}; std::vector<std::uint8_t> log; std::uint64_t start = 0; };
    RecSeg recPrev, recCur;
    bool recNewSeg = true;
    std::array<std::uint8_t, 6> lastPadBytes{0xff, 0xff, 0x80, 0x80, 0x80, 0x80};
    auto recPut32 = [](std::vector<std::uint8_t>& v, std::uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(std::uint8_t(x >> (8 * i))); };

    std::vector<std::uint8_t> repData;
    std::size_t repPos = 0;
    bool replaying = false;
    std::vector<char> repState;
    std::array<std::uint8_t, 6> repPad{};
    if (const char* rp = std::getenv("SF3N_REPETIR")) {
        std::vector<std::uint8_t> f;
        if (FILE* fp = std::fopen(rp, "rb")) { std::fseek(fp, 0, SEEK_END); f.resize(std::size_t(std::ftell(fp))); std::fseek(fp, 0, SEEK_SET); if (std::fread(f.data(), 1, f.size(), fp) != f.size()) f.clear(); std::fclose(fp); }
        auto g32 = [&](std::size_t o) { return o + 4 <= f.size() ? std::uint32_t(f[o]) | (std::uint32_t(f[o + 1]) << 8) | (std::uint32_t(f[o + 2]) << 16) | (std::uint32_t(f[o + 3]) << 24) : 0u; };
        if (f.size() > 32 && std::memcmp(f.data(), "SF3R", 4) == 0) {
            std::size_t o = 8;
            const auto idLen = g32(o); o += 4; const std::string id(reinterpret_cast<const char*>(f.data() + o), idLen); o += idLen;
            const auto cdSpeed = g32(o); o += 4;
            const auto stLen = g32(o); o += 4;
            repState.assign(reinterpret_cast<const char*>(f.data() + o), reinterpret_cast<const char*>(f.data() + o + stLen)); o += stLen;
            for (int i = 0; i < 6; ++i) repPad[std::size_t(i)] = f[o + std::size_t(i)];
            o += 6;
            repData.assign(f.begin() + std::ptrdiff_t(o), f.end());

            {
                std::uint64_t nf = 0;
                for (std::size_t q = 0; q < repData.size();) {
                    const auto tg = repData[q];
                    if (tg == 'F') { ++nf; q += 7; } else if (tg == 'A' || tg == 'Y') q += 9; else if (tg == 'T') q += 5; else if (tg == 'M') { g_rep_marks.push_back(nf); ++q; } else break;
                }
                g_rep_fields = nf;
            }
            CdRomController::data_speedup = cdSpeed ? cdSpeed : CdRomController::data_speedup;
            LOG("repeticion: %s (disco %s, %zu bytes de entradas)\n", rp, id.c_str(), repData.size());
        } else LOG("repeticion: archivo no valido %s\n", rp);
    }
    bool recDiscardPrev = false;
    auto snapshotSegment = [&] {
        recPrev = std::move(recCur);
        if (recDiscardPrev) { recPrev = RecSeg{}; recDiscardPrev = false; }
        recCur = RecSeg{};
        Ser sr; sr.out = &recCur.state; serialize(sr);
        ++g_idle_epoch;
        recCur.pad = lastPadBytes;
        recCur.start = fields;
        recCur.valid = true;
        recNewSeg = false;
    };
    auto writeRecording = [&](const std::string& path) {
        const RecSeg& base = recPrev.valid ? recPrev : recCur;
        if (!base.valid) return false;
        std::vector<std::uint8_t> out = {'S', 'F', '3', 'R'};
        recPut32(out, 1);
        const std::string id = g_prof->exe;
        recPut32(out, std::uint32_t(id.size())); out.insert(out.end(), id.begin(), id.end());
        recPut32(out, CdRomController::data_speedup);
        recPut32(out, std::uint32_t(base.state.size())); out.insert(out.end(), base.state.begin(), base.state.end());
        out.insert(out.end(), base.pad.begin(), base.pad.end());
        out.insert(out.end(), base.log.begin(), base.log.end());
        if (recPrev.valid) out.insert(out.end(), recCur.log.begin(), recCur.log.end());
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        FILE* fp = std::fopen(path.c_str(), "wb");
        if (!fp) return false;
        std::fwrite(out.data(), 1, out.size(), fp);
        std::fclose(fp);
        LOG("grabacion: %s (%.0f s desde t=%.1f)\n", path.c_str(), double(fields - base.start) / 50.0, double(base.start) / 50.0);
        return true;
    };
#ifdef SF3B_SDL
    bool initialLoadDone = bootLoad.empty() && repState.empty();
#else
    bool initialLoadDone = true;
#endif

    std::atomic<bool> emuDone{false};
    auto rebuildHooks = [&] {
        std::fill(g_hook_bits.begin(), g_hook_bits.end(), 0ull);
        const auto& P = *g_prof;
        for (auto a : {TextosEs::kPrint, 0x800a86b4u, TextosEs::kUpdate, P.world_room, P.room_section, P.actor_rtpt, P.actor_rtpt_x, P.actor_begin, P.actor_begin_x,
                       P.object, P.actor_tris_done, P.vis_check, 0x8002cd20u, 0x8002ce14u, 0x80147660u, 0x801474e8u, 0x801068c4u, 0x801068b4u, P.cd_search_file,
                       P.idle_frame_limiter, P.vsync_wait, 0x80147048u, 0x80146ed0u, P.aim_delta_aiming, P.aim_delta_walking})
            addHook(a);
        for (auto a : P.fx_rtps) addHook(a);
        addHook(0x800199d0u);
        addHook(0x80149cc4u);
        addHook(0x8014f268u);
        addHook(0x800582ecu);
        for (std::uint32_t a2 : {0x80148b5cu, 0x80149c04u, 0x8014a124u, 0x8014bbecu, 0x8014beb0u}) addHook(a2);
        addHook(0x8014f0c8u); addHook(0x8014f25cu); addHook(0x80148fb0u); addHook(0x80148d88u); addHook(0x80148e18u); addHook(0x8014a15cu);
        addHook(0x80066b8cu);
        addHook(0x800674ecu); addHook(0x8003f07cu);
        addHook(0x8014bc64u);
        addHook(0x80027ab8u);
        addHook(0x800277acu);
        addHook(0x8007851cu);
        for (auto a : P.fx_rtpt) addHook(a);
        for (auto a : g_fx_overlay_rtps_all) addHook(a);
        addHook(g_fade_step);
        addHook(g_cam_dir);
        addHook(g_pause_wait);
        addHook(g_weapon_pick);
        addHook(g_radar_blip); addHook(g_radar_blip ? g_radar_blip + 8 : 0);
        addHook(g_radar_arrow);
        addHook(g_radar_cone);
        addHook(g_cam_step_a); addHook(g_cam_step_b);
        addHook(g_radar_fn);
        addHook(g_camupd_in);
        addHook(kDbgTrampRet); addHook(kDbgFrameEnd);
        addHook(kMenuLoopPc);
        if (std::getenv("SF3N_LOGDANO")) addHook(0x8008bae0u);
    };
    rebuildHooks();

    const bool dbgEnv = std::getenv("SF3N_VOZLOG") || std::getenv("SF3N_PCTRACE") || std::getenv("SF3N_GTEPROF") || std::getenv("SF3N_GTESCAN") ||
                        std::getenv("SF3N_ACTTRACE") || std::getenv("SF3N_OBJTRACE") || std::getenv("SF3N_CDCALLER") || std::getenv("SF3N_TEXTRET") ||
                        std::getenv("SF3B_MOVTRACE") || std::getenv("SF3N_KILL") || std::getenv("SF3N_WATCH") || std::getenv("SF3N_FINDW");
    const bool genericIdle = std::getenv("SF3N_NOIDLE") == nullptr && std::getenv("SF3N_NOIDLE2") == nullptr;
    auto emulate = [&] {
    while (fields < totalFields) {
        const bool pcHot = dbgEnv || hookPc(cpu.state().pc);
        {
            static std::uint32_t prevPc = 0;
            const auto pc = cpu.state().pc;
            auto region = [](std::uint32_t a) { return a >= 0x801467d8u && a < 0x8014c1d0u ? 1 : a >= 0x801507d8u && a < 0x80158f10u ? 2 : 0; };
            if (((pc ^ 0x80100000u) >> 20) == 0 && region(pc) && region(pc) != region(prevPc)) {
                static std::map<std::uint32_t, int> seen;
                if (seen[pc]++ < 2) LOG("entra en %s %08x desde %08x ra=%08x a0=%08x a1=%08x (t=%.1fs)\n", region(pc) == 1 ? "MOVIE" : "TITLE", pc, prevPc, cpu.state().gpr[31], cpu.state().gpr[4], cpu.state().gpr[5], double(fields) / 50.0);
            }
            prevPc = pc;
        }
        if (pcHot && cpu.state().pc == 0x8008bae0u) {
            const auto& st = cpu.state(); std::uint32_t sk[4] = {};
            for (int k = 0; k < 4; ++k) (void)cpu.read32(st.gpr[29] + 0x10 + 4 * std::uint32_t(k), sk[k]);
            std::uint32_t p1 = 0; (void)cpu.read32(0x8012d79cu, p1); std::uint16_t idx1 = 0; (void)cpu.read16(p1 + 2, idx1);
            LOG("DANO campo %llu a0 %d a1 %d a2 %d a3 %d pila %08x %08x %08x %08x ra %08x (jugador1 indice %d)\n", (unsigned long long)fields, int(std::int16_t(st.gpr[4])), int(std::int16_t(st.gpr[5])), int(std::int16_t(st.gpr[6])), int(std::int16_t(st.gpr[7])), sk[0], sk[1], sk[2], sk[3], st.gpr[31], int(std::int16_t(idx1)));
        }
        if (pcHot) {
        if (g_list_redraw && cpu.state().pc == kMenuLoopPc) {
            g_list_redraw = false;
            std::uint32_t w0 = 0, w1 = 0, w2 = 0, id = 0;
            (void)cpu.read32(kMenuLoopPc, w0); (void)cpu.read32(kSetScreenPc, w1); (void)cpu.read32(0x80151a44u, w2); (void)cpu.read32(0x8015741cu, id);
            if (w0 == 0x3c048016u && w1 == 0x3c028015u && w2 == 0xac30741cu && id < 16) {
                auto s = cpu.state();
                s.gpr[4] = id; s.gpr[31] = kMenuLoopPc;
                s.pc = kSetScreenPc; s.next_pc = kSetScreenPc + 4; s.branch_delay_slot = false; s.branch_pc = 0;
                cpu.restoreCpuState(s);
                LOG("tarjeta: pantalla %u del menu rehecha\n", id);
            } else LOG("tarjeta: no se pudo rehacer la pantalla (codigo %08x %08x %08x)\n", w0, w1, w2);
        }
        if (cpu.state().pc == 0x8002cd20u && std::getenv("SF3B_MOVTRACE")) {
            std::uint32_t st = 0, idx = 0, gp = cpu.state().gpr[28]; (void)cpu.read32(gp + 0x258, st); (void)cpu.read32(0x8012a9f8u, idx);
            std::uint32_t sp = cpu.state().gpr[29], r0=0,r1=0,r2=0; (void)cpu.read32(sp+0x14,r0);(void)cpu.read32(sp+0x1c,r1);(void)cpu.read32(sp+0x24,r2);
            LOG("pelicula pedida: ra=%08x estado=%u idx=%08x pila %08x %08x %08x\n", cpu.state().gpr[31], st, idx, r0, r1, r2);
        }
        if (cpu.state().pc == 0x8002ce14u) {
            static int ml = 0; if (ml++ < 20) LOG("pelicula: devuelve v0=%d (t=%.1fs)\n", int(cpu.state().gpr[2]), double(fields) / 50.0);
        }
        if (g_pause_wait && cpu.state().pc == g_pause_wait && std::getenv("SF3N_FUNDIDO_LENTO") == nullptr) {

            const auto gp = cpu.state().gpr[28];
            std::uint32_t i0 = 0, i1 = 0; (void)cpu.read32(g_pause_wait, i0); (void)cpu.read32(g_pause_wait + 4, i1);
            const auto cnt = gp + std::uint32_t(std::int32_t(std::int16_t(i0 & 0xffffu))), flag = gp + std::uint32_t(std::int32_t(std::int16_t(i1 & 0xffffu)));
            std::uint16_t c = 0; std::uint8_t f = 0; (void)cpu.read16(cnt, c); (void)cpu.read8(flag, f);
            static const int esperaPausa = std::getenv("SF3N_ESPERA_PAUSA") ? std::atoi(std::getenv("SF3N_ESPERA_PAUSA")) : 0x1e;
            if (!f && std::int16_t(c) < esperaPausa) (void)cpu.write16(cnt, std::uint16_t(esperaPausa));
        }
        if (g_fade_step && cpu.state().pc == g_fade_step) {

            static const bool lento = std::getenv("SF3N_FUNDIDO_LENTO") != nullptr;
            const auto& g = cpu.state().gpr;
            std::uint32_t c = 0;
            std::uint32_t prev = 0; (void)cpu.read32(g[31] - 4, prev);

            std::uint32_t gstate = 0; (void)cpu.read32(g[28] + 0x250u, gstate);
            if (static const bool flog = std::getenv("SF3N_FUNDIDO_LOG") != nullptr; flog) { std::uint32_t cc = 0; (void)cpu.read32(g[5], cc); static const double desde = std::atof(std::getenv("SF3N_FUNDIDO_LOG")); static std::map<std::uint32_t, int> nfl; if (double(fields) / 50.0 >= desde && nfl[g[31]]++ < 200) LOG("FUNDIDO t=%.2f ra=%08x dir=%u c=%d prev=%08x estado %u\n", double(fields) / 50.0, g[31], g[4] & 0xffu, int(cc), prev, gstate); }
            static std::uint64_t lastPause = 0;
            if (gstate == 9u || gstate == 7u) lastPause = fields;
            const bool dark = (prev & 0xfc1fffffu) == 0x24050054u;
            if (!lento && cpu.read32(g[5], c)) {
                const auto v = std::int32_t(c);
                if (gstate == 9u) {
                    if ((g[4] & 0xffu) && v < 11 && dark) (void)cpu.write32(g[5], 11u);
                    else if (static const int fin = std::getenv("SF3N_FUNDIDO_FIN") ? std::atoi(std::getenv("SF3N_FUNDIDO_FIN")) : 1; !(g[4] & 0xffu) && v > fin) (void)cpu.write32(g[5], std::uint32_t(fin));
                } else if (dark && !(g[4] & 0xffu) && v > 0 && lastPause && fields - lastPause < 100) {
                    (void)cpu.write32(g[5], 0u);
                }
            }
        }
        if (const std::uint32_t mp = 0x80147660u; cpu.state().pc == mp) {
            std::uint32_t insn = 0; (void)cpu.read32(mp, insn);
            if (insn == 0x27bdffc0u) {
                const auto& g = cpu.state().gpr;
                std::uint32_t sa[4]{}; for (int k = 0; k < 4; ++k) (void)cpu.read32(g[29] + 0x10 + 4 * std::uint32_t(k), sa[k]);
                g_movie_skippable = g[7] != 0 || (g[5] > g[4] && g[5] - g[4] < 1200u);
                g_movie_logo = g[7] == 0 && g[5] > g[4] && g[5] - g[4] < 1200u;
                LOG("pelicula: lba %u..%u %dx%d ra %08x pila %08x %08x %08x %08x (t=%.1fs)%s\n", g[4], g[5], int(g[6]), int(g[7]), g[31], sa[0], sa[1], sa[2], sa[3], double(fields) / 50.0,
                    movieSkip ? " -> omitida (nativa)" : "");
                if (movieSkip) { bios.setReg(2, movieSkipResult); bios.jump(g[31]); continue; }
            }
        }

        if (const auto pc0 = cpu.state().pc; pc0 == (0x801068c4u) && CdRomController::commands_written == 1 && CdRomController::commands_executed >= 1 && fields < 50 * 20 && std::getenv("SF3N_ESPERA_CD") == nullptr) {
            const std::uint32_t counter = 0x8014622cu;
            std::uint32_t v = 0;
            (void)cpu.read32(counter, v);
            if (v < 0x3c0000u) {
                if (v == 0) LOG("arranque: espera inicial del CD adelantada (t=%.1fs)\n", double(fields) / 50.0);
                (void)cpu.write32(counter, 0x3c0000u);
            }
        }
        }
        if (const auto rpc = cpu.state().pc; (rpc & 0xfff00000u) == 0x80100000u && (!extraRoster().mgItems.empty() || !extraRoster().mpItems.empty())) (void)extraRoster().onPc(cpu, rpc);
        if (pcHot) {
        if (cpu.state().pc == g_prof->cd_search_file) {
            const auto fp = cpu.state().gpr[4];
            auto name = bios.str(cpu.state().gpr[5]);
            std::string path = name;
            if (!path.empty() && path[0] == '\\') path = path.substr(1);
            if (auto semi = path.find(';'); semi != std::string::npos) path = path.substr(0, semi);
            for (auto& c : path) if (c == '\\') c = '/';
            std::uint32_t result = 0;
            try {
                std::uint32_t vlba = 0, vsize = 0;
                if (const auto* vf = virtualDisc().find(path)) { vlba = vf->lba; vsize = std::uint32_t(vf->data.size()); }
                const auto e = vlba ? sf::disc::DirectoryEntry{} : image.find(path);
                if (vlba || !e.is_directory) {
                    const auto abs = (vlba ? vlba : e.extent_lba) + 150;
                    auto bcd = [](std::uint32_t v) { return std::uint8_t(((v / 10) << 4) | (v % 10)); };
                    (void)cpu.write8(fp + 0, bcd(abs / 4500));
                    (void)cpu.write8(fp + 1, bcd((abs / 75) % 60));
                    (void)cpu.write8(fp + 2, bcd(abs % 75));
                    (void)cpu.write8(fp + 3, 0);
                    (void)cpu.write32(fp + 4, vlba ? vsize : e.size);
                    for (std::uint32_t i = 0; i < 16; ++i) (void)cpu.write8(fp + 8 + i, i < name.size() && i < 15 ? std::uint8_t(name[i]) : 0);
                    result = fp;
                }
            } catch (const std::exception&) {}
            static std::map<std::string, int> seen;
            if (seen[name]++ < 2) LOG("CdSearchFile \"%s\" -> %s (t=%.1fs)\n", name.c_str(), result ? "ok" : "NO", double(fields) / 50.0);
#ifdef SF3B_SDL
            if (result && (name.find("\\FOG") != std::string::npos)) { g_textos.clearHud(cpu); currentFog = name; }
            if (window && result) window->onMissionFile(name);
#endif
            bios.setReg(2, result);
            bios.jump(cpu.state().gpr[31]);
            continue;
        }
        }
#ifdef SF3B_SDL
        if (window && pcHot) {
            const auto hpc = cpu.state().pc;
            const auto& P = *g_prof;
            if (hpc == TextosEs::kPrint) g_textos.onPrint(cpu);
            else if (hpc == 0x800a86b4u) g_textos.onInitPrims(cpu);
            else if (hpc == TextosEs::kUpdate) g_textos.onUpdate(cpu);
            else if (hpc == g_textos.retWatch()) g_textos.onReturn(cpu);
            if (hpc == P.world_room) { window->onWorldRoom(cpu.state(), cpu.ram()); gpu.room_seen_flip_ = gpu.display_flips; }
            else if (hpc == P.room_section) window->onRoomSection(cpu.state(), cpu.scratchpad(), cpu.ram());
            else if (hpc == P.actor_rtpt || hpc == P.actor_rtpt_x) window->onActorPart(cpu.state());
            if (hpc == 0x800199d0u && std::getenv("SF3N_SIN_MODELO_RECORDADO") == nullptr) {

                static std::unordered_map<std::uint32_t, std::uint32_t> lastModel;
                std::uint32_t ins = 0; (void)cpu.read32(0x800199d0u, ins);
                if (ins == 0x27bdffc8u) {
                    const std::uint32_t a0 = cpu.state().gpr[4], a3 = cpu.state().gpr[7];
                    if (a0 >= 0x80010000u && a0 < 0x80200000u) lastModel[a3] = a0;
                    else if (auto it = lastModel.find(a3); it != lastModel.end()) {
                        static int nlog = 0;
                        if (nlog++ < 5) LOG("personaje %08x sin modelo (a0 %08x): se usa su ultimo modelo %08x (t=%.2f)\n", a3, a0, it->second, double(fields) / 50.0);
                        auto st = cpu.state(); st.gpr[4] = it->second; cpu.restoreCpuState(st);
                    }
                }
            }
            if (hpc == P.actor_begin || hpc == P.actor_begin_x) {

                const std::uint32_t a0 = cpu.state().gpr[4];
                if ((a0 < 0x80010000u || a0 >= 0x80200000u) && std::getenv("SF3N_SIN_GUARDA_MODELO") == nullptr) {
                    static int nlog = 0;
                    if (nlog++ < 5) LOG("personaje sin modelo (a0 %08x, ra %08x, t=%.2f): no se dibuja\n", a0, cpu.state().gpr[31], double(fields) / 50.0);
                    auto st = cpu.state();
                    st.pc = st.gpr[31]; st.next_pc = st.gpr[31] + 4; st.branch_delay_slot = false; st.branch_pc = 0; st.gpr[2] = 0;
                    cpu.restoreCpuState(st);
                } else window->onActorBegin(cpu.state());
            }
            else if (hpc == P.object) window->onObject(cpu.state());
            else if (hpc == P.actor_tris_done) window->onActorTris(cpu.ram());
            else if (hpc == P.vis_check) {

                std::uint32_t insn = 0; (void)cpu.read32(hpc, insn);
                const double f = window->wideFactor();
                if (insn == 0x48800000u && f > 1.01) {
                    const auto a1 = cpu.state().gpr[5];
                    if (a1 > 0 && a1 < 1024) cpu.setRegister(5, std::uint32_t(std::lround(double(a1) * f * 1.15)));
                    if (static int vl = std::getenv("SF3N_VISLOG") ? 30 : 0; vl > 0) { --vl; LOG("VIS a1 %u -> %u (f %.2f)\n", a1, cpu.state().gpr[5], f); }
                }
            }
        }
#endif

        {
            static std::uint32_t lastPc = 0, idlePc = 0;
            static std::uint64_t idleHash = 0;
            static int idleCount = 0;
            static std::uint64_t epochSeen = 0;
            if (epochSeen != g_idle_epoch) { epochSeen = g_idle_epoch; lastPc = idlePc = 0; idleHash = 0; idleCount = 0; }
            const auto& st = cpu.state();
            const auto pcNow = st.pc;
            if (genericIdle && pcNow < lastPc && lastPc - pcNow < 256 && bios.saved.empty() && !st.branch_delay_slot) {
                std::uint64_t h = 1469598103934665603ull;
                for (int k = 1; k < 32; ++k) { h ^= st.gpr[std::size_t(k)]; h *= 1099511628211ull; }
                h ^= st.hi; h *= 1099511628211ull; h ^= st.lo; h *= 1099511628211ull;
                if (pcNow == idlePc && h == idleHash) {
                    if (++idleCount >= 2) {
                        const auto now = m.currentTick();
                        const auto target = std::min(nextHblank, nextVblank);
                        if (target > now) m.advanceTicks(target - now);
                        ++g_idle_skips;
                    }
                } else { idlePc = pcNow; idleHash = h; idleCount = 0; }
            }
            lastPc = pcNow;
        }

        if (pcHot && cpu.state().pc == 0x800582ecu && !std::getenv("SF3N_HUECOS_VIEJO")) {
            std::uint32_t code = 0; (void)cpu.read32(0x800582ecu, code);
            if (code == 0x27bdffe0u) {
                if (std::int32_t(cpu.state().gpr[4]) <= 0) { cpu.setRegister(4, 1); static int nlog = 0; if (nlog++ < 8) LOG("v195: reserva de huecos con cuenta 0 -> 1\n"); }
                for (std::uint32_t i = 0; i < 42;) {
                    std::uint32_t v = 0; (void)cpu.read32(0x80131f84u + 4 * i, v);
                    const std::int32_t sv = std::int32_t(v);
                    if (sv < 0) { ++i; continue; }
                    if (sv == 0) { cpu.write32(0x80131f84u + 4 * i, 1); ++i; continue; }
                    i += std::uint32_t(sv);
                }
            }
        }

        const std::uint32_t pcS = cpu.state().pc;
        const bool taserSite = pcHot && (pcS == 0x800674ecu || pcS == 0x8003f07cu);
        int taserW = 0;
        if (taserSite && window && window->weapon_sounds_.ok) {
            std::uint16_t cw = 0; (void)cpu.read16(0x8012D880u, cw);
            auto ti = window->weapon_sounds_.by_weapon.find(int(cw));
            if ((cw == 23 || cw == 29) && ti != window->weapon_sounds_.by_weapon.end() && ti->second.wav) taserW = int(cw);
            if (static const bool bl1 = std::getenv("SF3N_BANCOLOG") != nullptr; bl1) { static int n = 0; if (n++ < 20) LOG("banco: llamada del taser en %08x, arma en la mano %d, a1 %d%s\n", pcS, int(cw), int(cpu.state().gpr[5]), taserW ? " -> WAV" : ""); }
        }
        if (pcHot && (pcS == 0x80066b8cu || taserW) && window && window->weapon_sounds_.ok) {
            std::uint32_t code = 0; (void)cpu.read32(pcS, code);
            const auto& g = cpu.state().gpr;
            const int w = taserW ? taserW : int(g[17]);
            auto it = window->weapon_sounds_.by_weapon.find(w);
            std::uint32_t np = 0; (void)cpu.read32(0x80132b70u + 36u * std::uint32_t(w), np);
            std::string gname;
            for (int i = 0; i < 16 && np; ++i) { std::uint8_t c = 0; if (!cpu.read8(np + std::uint32_t(i), c) || !c) break; gname += char(c); }
            if (static const bool bl0 = std::getenv("SF3N_BANCOLOG") != nullptr; bl0) { static std::map<std::pair<int, int>, int> seen; if (seen[{w, int(g[5])}]++ < 3) LOG("banco: llamada de disparo arma %d (%s) banco %d programa %d\n", w, gname.c_str(), int(g[4]), int(g[5])); }

            std::uint8_t tableProg = 0xff; if (w >= 1 && w <= 39) (void)cpu.read8(0x80132b70u + 36u * std::uint32_t(w) + 0x12u, tableProg);
            const bool isWav = it != window->weapon_sounds_.by_weapon.end() && it->second.wav;
            if (code == 0x0c023e96u && g[4] == 1 && it != window->weapon_sounds_.by_weapon.end() && (taserW ? int(g[5]) == 17 : isWav ? int(g[5]) == int(tableProg) : int(g[5]) == it->second.program) &&
                g_weapon_tone_addr.count(w) && !gname.empty() && (isWav || std::getenv("SF3N_SONIDO_BANCO_SIEMPRE") || !window->missionHasOwnModel(gname))) {
                std::uint32_t hdr = 0, magic = 0; (void)cpu.read32(0x8013887cu, hdr); (void)cpu.read32(hdr, magic);
                if (magic == 0x4b4e4253u) {

                    std::uint32_t groups = 0, progs = 0, cnts = 0;
                    (void)cpu.read32(hdr + 0x24, groups); (void)cpu.read32(hdr + 0x20, progs); (void)cpu.read32(hdr + 0x18, cnts);
                    const int nprog = int(cnts & 0xffff);
                    int ng = int((cnts >> 16) & 0xffff);
                    const std::uint32_t kGroups = 0x80680000u, kMeta = 0x8067fff0u;
                    std::uint32_t mMagic = 0, mBase = 0, mHdr = 0;
                    (void)cpu.read32(kMeta, mMagic); (void)cpu.read32(kMeta + 4, mBase); (void)cpu.read32(kMeta + 8, mHdr);
                    if (groups != kGroups || mMagic != 0x42444e53u || mHdr != hdr) {
                        if (groups != kGroups) for (int i2 = 0; i2 < ng * 8; ++i2) { std::uint8_t b = 0; (void)cpu.read8(groups + std::uint32_t(i2), b); (void)cpu.write8(kGroups + std::uint32_t(i2), b); }
                        else ng = int(mBase);
                        mBase = std::uint32_t(ng);
                        (void)cpu.write32(kMeta, 0x42444e53u); (void)cpu.write32(kMeta + 4, mBase); (void)cpu.write32(kMeta + 8, hdr);
                        (void)cpu.write32(hdr + 0x24, kGroups); (void)cpu.write16(hdr + 0x1a, std::uint16_t(ng));
                    }
                    const std::uint32_t toneAddr = g_weapon_tone_addr[w];
                    int gi = -1;
                    for (int k = int(mBase); k < ng; ++k) { std::uint32_t pa = 0; (void)cpu.read32(kGroups + 8u * std::uint32_t(k) + 4, pa); if (pa == toneAddr) { gi = k; break; } }
                    if (gi < 0 && ng < 255) {
                        gi = ng++;
                        (void)cpu.write8(kGroups + 8u * std::uint32_t(gi), std::uint8_t(it->second.tones.size()));
                        (void)cpu.write8(kGroups + 8u * std::uint32_t(gi) + 1, 0x7f);
                        (void)cpu.write16(kGroups + 8u * std::uint32_t(gi) + 2, 0);
                        (void)cpu.write32(kGroups + 8u * std::uint32_t(gi) + 4, toneAddr);
                        (void)cpu.write16(hdr + 0x1a, std::uint16_t(ng));
                    }
                    int pw = -1;
                    for (int p = 0; p < nprog && gi >= 0; ++p) {
                        std::uint32_t ty = 0, ph = 0; std::uint8_t gb = 0;
                        (void)cpu.read32(progs + 36u * std::uint32_t(p), ty); (void)cpu.read32(progs + 36u * std::uint32_t(p) + 4, ph); (void)cpu.read8(progs + 36u * std::uint32_t(p) + 13, gb);
                        if (ty != 0 && ph == hdr && int(gb) == gi) { pw = p; break; }
                    }
                    if (pw < 0 && gi >= 0) {
                        for (int p = nprog - 1; p >= 0; --p) { std::uint32_t ty = 0; (void)cpu.read32(progs + 36u * std::uint32_t(p), ty); if (ty == 0) { pw = p; break; } }
                        if (pw >= 0) {
                            const auto& pe = it->second.prog;
                            const std::uint32_t a = progs + 36u * std::uint32_t(pw);
                            for (int i2 = 0; i2 < 36; ++i2) (void)cpu.write8(a + std::uint32_t(i2), pe[std::size_t(i2)]);
                            (void)cpu.write32(a + 4, hdr);
                            (void)cpu.write8(a + 13, std::uint8_t(gi));
                            (void)cpu.write32(a + 0x1c, 0);
                        }
                    }
                    std::map<int, int> progOf, groupOf; if (pw >= 0) { progOf[w] = pw; groupOf[w] = gi; }
                    if (progOf.count(w)) {
                        static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr;
                        if (bl) LOG("banco: disparo arma %d (%s) programa %d -> %d (grupo %d, %zu tonos, de %s)\n", w, gname.c_str(), int(g[5]), progOf[w], groupOf[w], it->second.tones.size(), it->second.source.c_str());
                        bios.setReg(5, std::uint32_t(progOf[w]));
                    }
                }
            }
        }

        static const bool sinPaginas = std::getenv("SF3N_SIN_PAGINAS") != nullptr;
        if (pcHot && !sinPaginas && (cpu.state().pc == 0x8014f0c8u || cpu.state().pc == 0x8014f25cu || cpu.state().pc == 0x80148fb0u ||
                      cpu.state().pc == 0x80148d88u || cpu.state().pc == 0x80148e18u || cpu.state().pc == 0x8014a15cu)) {
            static int listStart = 0, pageTarget = -1;
            const auto pc = cpu.state().pc;
            std::uint32_t code = 0; (void)cpu.read32(pc, code);
            std::uint32_t menuIdx = 0, items = 0; (void)cpu.read32(0x8014f97cu, menuIdx); (void)cpu.read32(0x8014f988u + 32u * menuIdx, items);
            const bool weaponList = items == 0x8014f6d4u;
            if (pc == 0x8014f0c8u && code == 0x3c028011u) listStart = std::max(0, int(cpu.state().gpr[4]));
            else if (pc == 0x8014f25cu && code == 0x0282102bu && cpu.state().gpr[3] == 0x8014f6d4u)
                bios.setReg(2, 0x8014f6d4u + 12u * std::uint32_t(listStart) + 0xc0u);
            else if (pc == 0x80148fb0u && code == 0x1082000du && weaponList) {
                const int cur = int(cpu.state().gpr[16]);
                if (cur >= 0 && (cur < listStart || cur >= listStart + 16)) { pageTarget = cur / 16 * 16; bios.setReg(4, 0xffffffffu); }
            } else if (pc == 0x80148d88u && pageTarget >= 0 && code == 0x3c028015u) {
                if (int(cpu.state().gpr[4]) > 31) bios.setReg(4, 31);
            } else if (pc == 0x80148e18u && pageTarget >= 0 && code == 0x3c048015u) {
                bios.setReg(16, std::uint32_t(pageTarget)); pageTarget = -1;
            } else if (pc == 0x8014a15cu && code == 0x2a220010u) {

                bios.setReg(2, int(cpu.state().gpr[17]) < 40 ? 1u : 0u); bios.jump(0x8014a160u); continue;
            }
        }

        if (pcHot && (cpu.state().pc == 0x80148b5cu || cpu.state().pc == 0x80149c04u || cpu.state().pc == 0x8014a124u ||
                      cpu.state().pc == 0x8014bbecu || cpu.state().pc == 0x8014beb0u) && !std::getenv("SF3N_SIN_REUBICAR")) {
            static constexpr std::uint32_t kRecs = 0x8076f800u, kRecsN = 40u;
            const auto pc = cpu.state().pc;
            std::uint32_t code = 0; (void)cpu.read32(pc, code);
            const std::uint32_t rt = (code >> 16) & 31u;
            if ((code >> 26) == 0x0du && (code & 0xffffu) == 0xdcccu && ((code >> 21) & 31u) == rt) {
                std::uint32_t base = 0; (void)cpu.read32(0x80117688u, base);
                if (pc == 0x8014bbecu && cpu.state().gpr[17] != base) { static int n = 0; if (n++ < 5) LOG("menu: base distinta en 8014bbec (%08x / %08x)\n", cpu.state().gpr[17], base); base = cpu.state().gpr[17]; }
                if (pc == 0x8014beb0u) for (std::uint32_t k = 0; k < kRecsN * 44u; k += 4) (void)cpu.write32(kRecs + k, 0);
                bios.setReg(int(rt), kRecs - base);
                bios.jump(pc + 4); continue;
            }
        }

        if (pcHot && cpu.state().pc == 0x8014f268u) {
            struct WName { std::uint32_t id; const char* gmd; const char* en[4]; const char* es[4]; };
            static const WName kNames[] = {
                {25, "FLAMETRK.GMD", {"Flame Thrower", "Flame Thrower taken", "Flame Thrower ammo taken", "Flame Thrower ammo maxed"}, {"Lanzallamas", "Lanzallamas recogido", "Lanzallamas: munición recogida", "Lanzallamas: máx. munición"}},
                {26, "CHNGUN.GMD", {"Gatling Gun", "Gatling Gun taken", "Gatling Gun ammo taken", "Gatling Gun ammo maxed"}, {"Ametralladora Gatling", "Ametralladora Gatling recogida", "Gatling: munición recogida", "Gatling: máx. munición"}},
                {29, "TASERH.GMD", {"Hand Taser", "Hand Taser taken", "Hand Taser ammo taken", "Hand Taser ammo maxed"}, {"Táser de mano", "Táser de mano recogido", "Táser de mano: carga recogida", "Táser de mano: máx. carga"}},
                {37, "STRYKER.GMD", {"Shotgun Stryker", "Shotgun Stryker taken", "Shotgun Stryker ammo taken", "Shotgun Stryker ammo maxed"}, {"Escopeta Stryker", "Escopeta Stryker recogida", "Stryker: munición recogida", "Stryker: máx. munición"}},
            };
            std::uint32_t code = 0; (void)cpu.read32(0x8014f268u, code);
            auto rstr = [&](std::uint32_t a, std::size_t n) { std::string t; for (std::size_t i = 0; i < n && a; ++i) { std::uint8_t c = 0; if (!cpu.read8(a + std::uint32_t(i), c) || !c) break; t += char(c); } return t; };
            for (std::size_t wi = 0; wi < std::size(kNames) && code == 0x8e22fffcu; ++wi) {
                const auto& W = kNames[wi];
                if (cpu.state().gpr[17] != 0x8014f6d4u + 12u * (W.id - 1u) + 8u) continue;
                std::uint32_t np = 0, grp = 0; (void)cpu.read32(0x80132b70u + 36u * W.id, np); (void)cpu.read32(0x8014f6d4u + 12u * (W.id - 1u) + 4u, grp);
                if (rstr(np, 16) != W.gmd || grp < 0x80000000u || grp >= 0x80200000u) continue;
                const bool es = g_idioma == "es";
                bool dots = true;
                for (std::uint32_t k = 0; k < 4; ++k) { std::uint32_t sp = 0; (void)cpu.read32(grp + 4u * k, sp); const auto t = rstr(sp, 48); if (t != "." && t != W.en[k] && t != W.es[k]) dots = false; }
                if (!dots) continue;
                std::uint32_t at = 0x8076f000u + 0x100u * std::uint32_t(wi);
                for (std::uint32_t k = 0; k < 4; ++k) {
                    std::string t = es ? W.es[k] : W.en[k];

                    std::string l1; for (std::size_t i = 0; i < t.size(); ++i) { const unsigned char c = (unsigned char)t[i]; if (c == 0xc3 && i + 1 < t.size()) { l1 += char((unsigned char)t[i + 1] + 0x40); ++i; } else l1 += char(c); }
                    for (std::size_t i = 0; i <= l1.size(); ++i) (void)cpu.write8(at + std::uint32_t(i), i < l1.size() ? std::uint8_t(l1[i]) : 0);
                    (void)cpu.write32(grp + 4u * k, at);
                    at += std::uint32_t(l1.size() + 1 + 3) & ~3u;
                }
                static int logged = 0; if (logged++ < 8) LOG("armas: textos de %s en el grupo %08x\n", W.gmd, grp);
            }
        }

        static int menuPicId = -1;
        if (pcHot && (cpu.state().pc == 0x80149cc4u || (cpu.state().pc == 0x8014bc64u && menuPicId >= 0)) && !nat::weaponMenuPics().empty()) {
            static std::uint64_t lastField = 0; static int lastId = -1;
            const bool draw = cpu.state().pc == 0x80149cc4u;
            std::uint32_t code = 0; (void)cpu.read32(cpu.state().pc, code);
            if (code == (draw ? 0x0c004a39u : 0x0c0097c4u)) {
                if (draw && fields > lastField + 30) lastId = -1;
                if (draw) lastField = fields;
                const std::uint32_t id = draw ? (cpu.state().gpr[16] & 0xff) : std::uint32_t(menuPicId);
                if (draw) menuPicId = int(id);
                std::uint32_t np = 0; (void)cpu.read32(0x80132b70u + 36u * id, np);
                std::string nm;
                for (int i = 0; i < 12 && np; ++i) { std::uint8_t c = 0; if (!cpu.read8(np + std::uint32_t(i), c) || !c || c == '.') break; nm += char(c); }

                if (!nm.empty() && gpu.idle()) {
                    auto it = nat::weaponMenuPics().find(nm + ".TIM");
                    if (it == nat::weaponMenuPics().end() && id == 0x20) it = nat::weaponMenuPics().find("GASGREN.TIM");
                    if (it == nat::weaponMenuPics().end()) {
                        static nat::WeaponMenuPic blank; static bool made = false;
                        if (!made && !nat::weaponMenuPics().empty()) {
                            made = true; blank = nat::weaponMenuPics().begin()->second;
                            int ti = -1; for (std::size_t k = 0; k < blank.clut.size() && k < 16; ++k) if (blank.clut[k] == 0) { ti = int(k); break; }
                            if (ti < 0) blank.pix.clear();
                            else std::fill(blank.pix.begin(), blank.pix.end(), std::uint16_t(ti * 0x1111));
                        }
                        if (!blank.pix.empty()) { gpu.menu_clut_ = {}; gpu.menu_pic_ = {blank.x, blank.y, blank.w, blank.h, &blank.pix}; if (draw || int(id) != lastId) gpu.showMenuPic(); }
                    }
                    if (it != nat::weaponMenuPics().end()) {
                        const auto& p = it->second;
                        gpu.menu_clut_ = {p.cx, p.cy, p.cw, p.ch, &p.clut};
                        gpu.menu_pic_ = {p.x, p.y, p.w, p.h, &p.pix};
                        if (draw || int(id) != lastId) gpu.showMenuPic();
                        static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr;
                        if (bl && int(id) != lastId) LOG("banco: foto en pantalla id %d %s\n", int(id), nm.c_str());
                    }
                    lastId = int(id);
                }
            }
        }

        if (pcHot && cpu.state().pc == 0x80149cc4u && !sinPaginas) {
            std::uint32_t code = 0; (void)cpu.read32(0x80149cc4u, code);
            if (code == 0x0c004a39u) {
                const std::uint32_t rec = cpu.state().gpr[17] + cpu.state().gpr[5];
                std::uint32_t node = 0; (void)cpu.read32(rec + 0x28, node);
                if (node != 0) { bios.jump(0x80149cccu); continue; }
            }
        }

        if (pcHot && cpu.state().pc == 0x80027ab8u && cpu.state().gpr[31] == 0x800a6aacu && !nat::weaponMenuTims().empty()) {
            const auto& g = cpu.state().gpr;
            char nm[24]{};
            for (int i = 0; i < 23; ++i) { std::uint8_t c = 0; if (!cpu.read8(g[5] + std::uint32_t(i), c) || !c) break; nm[i] = char(c); }
            if (auto it = nat::weaponMenuTims().find(nm); it != nat::weaponMenuTims().end() && g[6] && g[7]) {
                if (const auto* vf = virtualDisc().find(it->second)) {
                    std::uint32_t base = 0; (void)cpu.read32(0x80122700u, base);
                    (void)cpu.write32(g[6], (vf->lba - base) * 2048u);
                    (void)cpu.write32(g[7], std::uint32_t(vf->data.size()));
                    static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr;
                    if (bl) LOG("banco: foto %s -> lba %u (base %u, %zu bytes)\n", nm, vf->lba, base, vf->data.size());
                    bios.setReg(2, 1); bios.jump(g[31]); continue;
                }
            }
        }
        if (pcHot && window && window->weapon_bank_.ok && cpu.state().pc == 0x800277acu) {
            const auto& g = cpu.state().gpr;
            char nm[24]{};
            for (int i = 0; i < 23; ++i) { std::uint8_t c = 0; if (!cpu.read8(g[5] + std::uint32_t(i), c) || !c) break; nm[i] = char(c); }
            if (static const bool tl = std::getenv("SF3N_BUSCALOG") != nullptr; tl) LOG("BUSCA t=%.2f %s hog %08x ra %08x\n", double(fields) / 50.0, nm, g[4], g[31]);
            if (auto it = window->weapon_bank_.by_name.find(nm); it != window->weapon_bank_.by_name.end() && g[6]) {
                const auto addr = window->weapon_bank_.models[it->second].addr;
                (void)cpu.write32(g[6], addr);
                static const bool bl = std::getenv("SF3N_BANCOLOG") != nullptr;
                if (bl) LOG("banco: %s -> %08x (ra %08x)\n", nm, addr, g[31]);
                bios.setReg(2, 0); bios.jump(g[31]); continue;
            }
        }
        if (pcHot && bios.saved.empty()) {
            static const bool idleSkip = std::getenv("SF3N_NOIDLE") == nullptr;
            const auto ipc = cpu.state().pc;
            std::uint32_t expect = 0;
            if (ipc == g_prof->idle_frame_limiter && ipc) expect = 0x3c04f200u;
            else if (ipc == g_prof->vsync_wait) expect = 0x8fa20010u;
            else if (ipc == (0x80147048u)) expect = 0x02602021u;
            std::uint32_t w = 0;
            if (idleSkip && expect && cpu.read32(ipc, w) && w == expect) {
                const auto now = m.currentTick();
                const auto target = std::min(nextHblank, nextVblank);
                if (target > now) {
                    const auto skipped = target - now;
                    m.advanceTicks(skipped);

                    if (ipc == g_prof->vsync_wait) {
                        const auto sp = cpu.state().gpr[29];
                        std::uint32_t left = 0;
                        if (cpu.read32(sp + 0x10, left) && std::int32_t(left) > 1)
                            (void)cpu.write32(sp + 0x10, std::uint32_t(std::max<std::int64_t>(1, std::int64_t(std::int32_t(left)) - std::int64_t(skipped / 12))));
                    } else if (ipc == (0x80147048u)) {
                        auto st = cpu.state();
                        st.gpr[16] += std::uint32_t(skipped / 40);
                        cpu.restoreCpuState(st);
                    }
                }
            }
        }
        if (dbgEnv) {
        static const bool debugHooks = std::getenv("SF3N_KILL") || std::getenv("SF3N_WATCH") || std::getenv("SF3N_FINDW");
        if (debugHooks) {
        {
            static const std::vector<std::uint32_t> kill = [] {
                std::vector<std::uint32_t> v;
                if (const char* e = std::getenv("SF3N_KILL")) { std::string str(e); std::size_t p = 0; while (p < str.size()) { auto q = str.find(',', p); if (q == std::string::npos) q = str.size(); v.push_back(std::uint32_t(std::stoul(str.substr(p, q - p), nullptr, 16))); p = q + 1; } }
                return v;
            }();
            if (!kill.empty() && std::find(kill.begin(), kill.end(), cpu.state().pc) != kill.end()) { bios.setReg(2, 0); bios.jump(cpu.state().gpr[31]); continue; }
        }
        {
            static const std::vector<std::uint32_t> watch = [] { std::vector<std::uint32_t> v; if (const char* e = std::getenv("SF3N_WATCH")) { std::string t(e); std::size_t p = 0; while (p < t.size()) { auto q = t.find(',', p); if (q == std::string::npos) q = t.size(); v.push_back(std::uint32_t(std::stoul(t.substr(p, q - p), nullptr, 16))); p = q + 1; } }
                if (v.size() == 2 && v[1] < 0x10000) { const auto b = v[0], n = v[1]; v.clear(); for (std::uint32_t k = 0; k < n; ++k) v.push_back(b + 4 * k); }
                return v; }();
            static std::vector<std::uint32_t> last(watch.size(), 0xdeadbeef);
            static std::uint32_t prevPcW = 0;
            static std::map<std::pair<std::uint32_t, std::uint32_t>, int> seenw;
            for (std::size_t i = 0; i < watch.size(); ++i) {
                std::uint32_t v{}; (void)cpu.read32(watch[i], v);
                static const std::uint32_t wval = std::getenv("SF3N_WATCHVAL") ? std::uint32_t(std::stoul(std::getenv("SF3N_WATCHVAL"), nullptr, 16)) : 0;
                if (v != last[i] && wval && v != wval) { last[i] = v; continue; }
                if (v != last[i]) { if (last[i] != 0xdeadbeef && seenw[{watch[i], prevPcW}]++ < 4) LOG("WATCH %08x: %08x -> %08x en pc=%08x ra=%08x (t=%.2fs)\n", watch[i], last[i], v, prevPcW, cpu.state().gpr[31], double(fields) / 50.0); last[i] = v; }
            }
            prevPcW = cpu.state().pc;
            if (static const char* const e = std::getenv("SF3N_FINDW"); e && fields == std::uint64_t(std::atof(std::getenv("SF3N_FINDT")) * 50)) {
                static bool done = false;
                if (!done) { done = true; const auto want = std::uint32_t(std::stoul(e, nullptr, 16)); auto ram = cpu.ram(); int n = 0;
                    for (std::size_t a = 0; a + 4 <= ram.size() && n < 40; a += 4) { std::uint32_t v; std::memcpy(&v, ram.data() + a, 4); if (v == want) { std::uint32_t nx = 0; std::memcpy(&nx, ram.data() + a + 4, 4); LOG("FINDW %08x en %08zx (sig %08x)\n", want, a | 0x80000000u, nx); ++n; } } }
            }
        }
        }
        if (static const bool vl2 = std::getenv("SF3N_VOZLOG") != nullptr; vl2 && (cpu.state().pc == 0x800909d8u || cpu.state().pc == 0x80042138u || cpu.state().pc == 0x80049298u || cpu.state().pc == 0x800b53b8u || cpu.state().pc == 0x800b5bf4u)) {
            const auto& g = cpu.state().gpr;
            std::uint32_t lst = 0, act = 0; std::uint32_t gpw = 0;
            (void)cpu.read32(g[28] + 0xe28, act); if (act) (void)cpu.read32(act + 0xc, lst);
            std::uint16_t L[8]{}; for (int k = 0; k < 8; ++k) { std::uint32_t w = 0; if (lst) (void)cpu.read32(lst + 2 * k, w); L[k] = std::uint16_t(w); }
            (void)gpw;
            LOG("VOZCOLA pc %08x a0 %x a1 %08x ra %08x s3 %08x act %08x lista %08x [%u %u %u %u %u %u %u %u]\n", cpu.state().pc, g[4], g[5], g[31], g[19], act, lst, L[0], L[1], L[2], L[3], L[4], L[5], L[6], L[7]);
        }
        if (static const bool vl = std::getenv("SF3N_VOZLOG") != nullptr; vl && cpu.state().pc == (0x80090408u)) {
            const auto& g = cpu.state().gpr;
            LOG("VOZ campo %llu id %u a1 %08x a2 %08x a3 %08x ra %08x s0-s7 %08x %08x %08x %08x %08x %08x %08x %08x llamadas:", (unsigned long long)fields, g[4], g[5], g[6], g[7], g[31], g[16], g[17], g[18], g[19], g[20], g[21], g[22], g[23]);
            for (std::uint32_t k = 1024 - 24; k < 1024; ++k) { const auto i = (sf::psx::g_dbg_ncall + k) & 1023u; LOG(" %08x(%x)", sf::psx::g_dbg_calls[i], sf::psx::g_dbg_args[i]); }
            LOG("\n");
        }
        if (static const std::uint32_t pcTrace = std::getenv("SF3N_PCTRACE") ? std::uint32_t(std::stoul(std::getenv("SF3N_PCTRACE"), nullptr, 16)) : 0; pcTrace && cpu.state().pc == pcTrace && double(fields) / 50.0 >= (std::getenv("SF3N_PCTRACE_DESDE") ? std::atof(std::getenv("SF3N_PCTRACE_DESDE")) : 0.0)) {
            static int n = 0;
            if (n++ < 2000) {
                const auto& g = cpu.state().gpr;
                std::uint32_t m[8]{};
                for (int k = 0; k < 8; ++k) (void)cpu.read32(g[9] + 4 * k, m[k]);
                std::uint32_t sp[8]{};
                for (int k = 0; k < 8; ++k) (void)cpu.read32(g[29] + 0x10 + 4 * k, sp[k]);
                std::uint32_t s3w[4]{};
                for (int k = 0; k < 4; ++k) (void)cpu.read32(g[19] + 4 * k, s3w[k]);
                { std::uint32_t t24 = 0; (void)cpu.read32(g[25] + 0x24, t24); std::uint32_t fp14 = 0; (void)cpu.read32(g[30] + 0x14, fp14); LOG("PCTRACE2 t9=%08x [t9+24]=%08x fp=%08x [fp+14]=%08x\n", g[25], t24, g[30], fp14); }
                LOG("PCTRACE t=%.2f s0=%08x s1=%08x t1=%08x [%d %d %d %d] sp10 [%d %d %d %d] ra=%08x s3=%08x [%08x %08x %08x %08x] a0=%08x a1=%08x a2=%08x a3=%08x\n", double(fields) / 50.0, g[16], g[17], g[9], int(m[0]), int(m[1]), int(m[2]), int(m[3]), int(sp[0]), int(sp[1]), int(sp[2]), int(sp[3]), g[31], g[19], s3w[0], s3w[1], s3w[2], s3w[3], g[4], g[5], g[6], g[7]);
            }
        }
#ifdef SF3B_SDL
        }

        if (window && pcHot && g_radar_cone && cpu.state().pc == g_radar_cone) {
            const auto& g = cpu.state().gpr;
            std::uint32_t c = 0; (void)cpu.read32(g[4] + 0x2e8, c);
            window->radarCone(c, std::int32_t(g[6]), std::int32_t(g[7]), g_radar_exact[0], g_radar_exact[1]);
        }
        if (window && pcHot && g_radar_arrow && cpu.state().pc == g_radar_arrow) {
            const auto& g = cpu.state().gpr;
            std::uint32_t x = 0, z = 0, c = 0;
            (void)cpu.read32(g[29] + 0x10, x); (void)cpu.read32(g[29] + 0x18, z); (void)cpu.read32(g[19] + 0x2e8, c);
            window->radarArrow(c, std::int32_t(x), std::int32_t(z));
        }
        if (window && pcHot && g_radar_blip && (cpu.state().pc == g_radar_blip || cpu.state().pc == g_radar_blip + 8)) {
            const auto& g = cpu.state().gpr;
            if (cpu.state().pc == g_radar_blip) {
                std::int32_t v[3]{}; std::uint32_t t = 0;
                (void)cpu.read32(g[5], t); v[0] = std::int32_t(t); (void)cpu.read32(g[5] + 4, t); v[1] = std::int32_t(t); v[2] = std::int32_t(g[2]);
                for (int r = 0; r < 2; ++r) {
                    double acc = 0;
                    for (int c = 0; c < 3; ++c) { std::uint32_t w = 0; (void)cpu.read32((g[4] + std::uint32_t(2 * (3 * r + c))) & ~3u, w); const std::int16_t m = std::int16_t(((g[4] + std::uint32_t(2 * (3 * r + c))) & 2) ? (w >> 16) : (w & 0xffff)); acc += double(m) * double(v[c]); }
                    g_radar_exact[r] = acc / 4096.0;
                }
                g_radar_have = true;
            } else if (g_radar_have) {
                g_radar_have = false;
                std::uint32_t ix = 0, iy = 0, c = 0;
                (void)cpu.read32(g[29] + 0x10, ix); (void)cpu.read32(g[29] + 0x14, iy); (void)cpu.read32(g[19] + 0x2e8, c);
                window->radarBlip(c, std::int32_t(ix), std::int32_t(iy), g_radar_exact[0], g_radar_exact[1]);
            }
        }
        if (window && pcHot && g_weapon_pick && cpu.state().pc == g_weapon_pick) {
            const auto& g = cpu.state().gpr;

            const int want = window->wheelPick(int(g[16]), g[19] + 0xe4u);
            if (std::getenv("SF3N_FAKEWHEEL")) LOG("RUEDA enganche: armas %08x arma juego %d -> %d (t=%.2fs)\n", g[19] + 0xe4u, int(g[16]), want, double(fields) / 50.0);
            if (want >= 0) cpu.setRegister(16, std::uint32_t(want));
        }
        if (pcHot && g_radar_fn && cpu.state().pc == g_radar_fn) g_radar_field = fields;
        if (static const bool al = std::getenv("SF3N_ACCIONLOG") != nullptr; al && window && g_cam_C) {
            static std::uint64_t lastF = 0;
            if (fields != lastF) { lastF = fields; std::uint32_t pl = 0, act = 0, cy = 0, hd = 0; (void)cpu.read32(g_prof->player_pointer, pl);
              if (pl >= 0x80000000u && pl < 0x80200000u) { (void)cpu.read32(pl + 0x1d8, act); (void)cpu.read32(pl + 0x220, hd); }
              (void)cpu.read32(g_cam_C + 0x8ecu, cy);
              LOG("ACCION campo %llu accion %u rumbo %d camyaw %d\n", (unsigned long long)fields, act, std::int32_t(hd), std::int32_t(cy)); }
        }

        if (window && pcHot) {
            static bool injActive = false;
            static std::remove_cvref_t<decltype(cpu.state())> injSaved{};
            if (injActive && (cpu.state().pc == kDbgTrampRet || cpu.state().pc == kOnTrampRet)) {
                const bool wasHit = cpu.state().pc == kOnTrampRet;
                cpu.restoreCpuState(injSaved);
                injActive = false;
                if (wasHit) LOG("en linea: golpe aplicado\n"); else LOG("depuracion: mision cargada\n");
            } else if (std::uint32_t jw = 0; !injActive && cpu.state().pc == kDbgFrameEnd && fields <= g_radar_field + 30 && cpu.read32(kDbgFrameEnd, jw) && jw == 0x0c008a36u) {
                const int req = window->takeDebugMission();
                if (req >= 0 && req < 39) {
                    auto jal = [](std::uint32_t a) { return 0x0c000000u | ((a >> 2) & 0x03ffffffu); };
                    const std::uint32_t code[] = {
                        0x27bdffe0u, 0xafbf001cu,
                        jal(0x800afacc), 0u,
                        jal(0x8002d364), 0u,
                        jal(0x800907a4), 0u,
                        0x00002021u, jal(0x80062d10), 0x00002821u,
                        0x24040000u | std::uint32_t(req), 0x24050001u, jal(0x8002acbc), 0x24060001u,
                        0x8fbf001cu, 0x27bd0020u, 0x03e00008u, 0u,
                        0u};
                    for (std::size_t k = 0; k < std::size(code); ++k) (void)cpu.write32(kDbgTramp + 4 * std::uint32_t(k), code[k]);
                    injSaved = cpu.state();
                    auto st = cpu.state();
                    st.pc = kDbgTramp; st.next_pc = kDbgTramp + 4; st.branch_delay_slot = false; st.gpr[31] = kDbgTrampRet;
                    cpu.restoreCpuState(st);
                    injActive = true;
                    LOG("depuracion: cargando la mision %d (antes %d)\n", req, window->debug_cur_mission_.load());
                }
            }
        }

        if (replaying && pcHot && g_camupd_in && cpu.state().pc == g_camupd_in && repPos + 9 <= repData.size() && repData[repPos] == 'Y') {
            auto g32 = [&](std::size_t o) { return std::uint32_t(repData[o]) | (std::uint32_t(repData[o + 1]) << 8) | (std::uint32_t(repData[o + 2]) << 16) | (std::uint32_t(repData[o + 3]) << 24); };
            const std::uint32_t o = g32(repPos + 1), yaw = g32(repPos + 5); repPos += 9;
            for (std::uint32_t k : {0x0cu, 0x2cu, 0x3cu, 0x5cu}) (void)cpu.write32(o + k, yaw);
            (void)cpu.write32(o + 0x1c, 0);
            if (!g_cam_C) g_cam_C = o - 0x8e0u;
        }
        if (window && pcHot && g_camupd_in && cpu.state().pc == g_camupd_in && g_cam_C && fields > g_radar_field + 30) {
            std::uint32_t cur = 0; (void)cpu.read32(g_cam_C + 0x8ecu, cur); window->camFreeSync(std::int32_t(cur), false);
            if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) LOG("CAMSINRADAR campo %llu radar %llu\n", (unsigned long long)fields, (unsigned long long)g_radar_field);
        }
        if (window && pcHot && g_camupd_in && cpu.state().pc == g_camupd_in && g_cam_C && fields <= g_radar_field + 30) {

            const std::uint32_t camObj = cpu.state().gpr[4];
            std::uint32_t follow = 0, script = 1, cur = 0;
            (void)cpu.read32(camObj + 0x50, follow); (void)cpu.read32(camObj + 0xe8, script);
            (void)cpu.read32(g_cam_C + 0x8ecu, cur);

            std::uint32_t sflags = 0;
            if (script) (void)cpu.read32(script + 0x2c, sflags);
            bool following = script == 0 || !(sflags & 0x40u);
            (void)follow;

            static std::uint64_t notFollowingSince = 0;
            if (following) notFollowingSince = 0;
            else if (!notFollowingSince) notFollowingSince = fields;
            else if (fields - notFollowingSince > 400 && window->playerSteering()) {
                if (fields - notFollowingSince == 401 || (fields - notFollowingSince) % 3000 == 0) LOG("camara libre: plano especial de mas de 8 s con el jugador en control (guion %08x banderas %08x): se ignora\n", script, sflags);
                following = true;
            }
            if (static const bool fl = std::getenv("SF3N_FREELOG") != nullptr; fl) LOG("CAMOBJ %08x sigue %08x guion %08x banderas %08x campo %llu\n", camObj, follow, script, sflags, (unsigned long long)fields);
            std::int32_t yaw = 0;
            window->setCamFollowing(following);
            if (!replaying && following && window->camFreeYaw(yaw)) {
                const std::uint32_t o = g_cam_C + 0x8e0u;
                for (std::uint32_t k : {0x0cu, 0x2cu, 0x3cu, 0x5cu}) (void)cpu.write32(o + k, std::uint32_t(yaw));
                (void)cpu.write32(o + 0x1c, 0);
                if (recCur.valid) { recCur.log.push_back('Y'); recPut32(recCur.log, o); recPut32(recCur.log, std::uint32_t(yaw)); }
            } else if (!replaying) window->camFreeSync(std::int32_t(cur), following);
        }
        if (window && pcHot && (g_cam_step_a || g_cam_step_b) && (cpu.state().pc == g_cam_step_a || cpu.state().pc == g_cam_step_b)) {
            const std::uint32_t obj = cpu.state().gpr[17];
            if (g_cam_C && (obj == g_cam_C + 0x510u || obj == g_cam_C + 0x3ccu) && fields <= g_radar_field + 6 && window->cameraFirm()) {
                std::uint32_t type = 0; (void)cpu.read32(obj + 8, type);
                for (int k = 0; k < 3; ++k) {
                    if (type == 1 && k == 1) continue;
                    std::uint32_t goal = 0; (void)cpu.read32(obj + 0x5c + 4 * std::uint32_t(k), goal);
                    (void)cpu.write32(obj + 0xc + 4 * std::uint32_t(k), goal);
                    (void)cpu.write32(obj + 0x1c + 4 * std::uint32_t(k), 0);
                    (void)cpu.write32(obj + 0x2c + 4 * std::uint32_t(k), 0);
                }
            }
        }
        if (window && pcHot && g_cam_dir && cpu.state().pc == g_cam_dir) {
            const auto& g = cpu.state().gpr;
            if (g_cam_C != g[6]) window->setCamStruct(g[6]);
            g_cam_C = g[6];
            if (static const bool cl = std::getenv("SF3N_CAMLOG") != nullptr; cl) {
                std::uint32_t tw[3]{}, ew[3]{}, pl = 0, hd = 0, px = 0, pz = 0;
                for (int k = 0; k < 3; ++k) { (void)cpu.read32(g[29] + 0x20 + 4 * k, tw[k]); (void)cpu.read32(g[29] + 0x10 + 4 * k, ew[k]); }
                (void)cpu.read32(g_prof->player_pointer, pl);
                if ((pl >> 24) == 0x80u) { (void)cpu.read32(pl + 0x220, hd); }
                const double cy = std::atan2(double(std::int32_t(tw[0]) - std::int32_t(ew[0])), double(std::int32_t(tw[2]) - std::int32_t(ew[2]))) * 4096.0 / 6.283185307179586;
                double diff = std::fmod(cy - double(std::int32_t(hd) & 4095) + 6144.0 * 4, 4096.0) - 2048.0;
                (void)px; (void)pz;
                std::uint32_t est = 0; (void)cpu.read32(0x80121938u + 0x250u, est);
                LOG("CAMEST %u ", est & 0xff);
                LOG("CAM t=%.2f ojo %d,%d,%d punto %d,%d,%d rumbo %d camara %.0f desfase %.0f\n", double(fields) / 50.0, std::int32_t(ew[0]), std::int32_t(ew[1]), std::int32_t(ew[2]), std::int32_t(tw[0]), std::int32_t(tw[1]), std::int32_t(tw[2]), std::int32_t(hd) & 4095, cy, diff);
            }
            std::uint32_t tw[3]{}, ew[3]{}, player = 0, ay = 0, ap = 0;
            for (int k = 0; k < 3; ++k) { (void)cpu.read32(g[29] + 0x20 + 4 * k, tw[k]); (void)cpu.read32(g[29] + 0x10 + 4 * k, ew[k]); }
            (void)cpu.read32(g_prof->player_pointer, player);
            if ((player >> 24) == 0x80u) { (void)cpu.read32(player + 0x220, ay); (void)cpu.read32(player + 0x224, ap); }

            if (fields - g_aim_path_field > 6) {
                const double dx = double(std::int32_t(tw[0]) - std::int32_t(ew[0])), dz = double(std::int32_t(tw[2]) - std::int32_t(ew[2]));
                if (dx * dx + dz * dz > 1.0) window->setViewYaw(std::atan2(dx, dz) * 4096.0 / 6.283185307179586);
            }

            static const bool camJuego = std::getenv("SF3N_CAM_JUEGO") != nullptr;
            if (!camJuego && (player >> 24) == 0x80u && fields - g_aim_path_field <= 6) {
                const double dx = double(std::int32_t(tw[0]) - std::int32_t(ew[0])), dy = double(std::int32_t(tw[1]) - std::int32_t(ew[1])), dz = double(std::int32_t(tw[2]) - std::int32_t(ew[2]));
                const double L = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (L > 16.0) {
                    const double yaw = double(std::int32_t(ay)) * 6.283185307179586 / 4096.0, pit = -double(std::int32_t(ap)) * 6.283185307179586 / 4096.0;
                    const double n[3] = {std::cos(pit) * std::sin(yaw), std::sin(pit), std::cos(pit) * std::cos(yaw)};

                    const double LL = std::max(L, 4096.0);
                    for (int k = 0; k < 3; ++k) (void)cpu.write32(g[29] + 0x20 + 4 * k, std::uint32_t(std::int32_t(std::lround(double(std::int32_t(ew[std::size_t(k)])) + LL * n[k]))));
                }
            } else if (double pitch = 0; !camJuego && window->camFreePitch(pitch) && pitch != 0.0) {

                const double dx = double(std::int32_t(tw[0]) - std::int32_t(ew[0])), dy = double(std::int32_t(tw[1]) - std::int32_t(ew[1])), dz = double(std::int32_t(tw[2]) - std::int32_t(ew[2]));
                const double h = std::hypot(dx, dz), L = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (h > 16.0) {
                    const double el = std::clamp(std::atan2(-dy, h) + pitch * 6.283185307179586 / 4096.0, -1.2, 1.2), yw = std::atan2(dx, dz);
                    window->setFreeViewElev(el * 4096.0 / 6.283185307179586);
                    const double n[3] = {std::cos(el) * std::sin(yw), -std::sin(el), std::cos(el) * std::cos(yw)};
                    const double LL = std::max(L, 4096.0);
                    for (int k = 0; k < 3; ++k) (void)cpu.write32(g[29] + 0x20 + 4 * k, std::uint32_t(std::int32_t(std::lround(double(std::int32_t(ew[std::size_t(k)])) + LL * n[k]))));
                }
            }
            if (static const bool cl = std::getenv("SF3N_CAMLOG") != nullptr; cl) {
                const double dx = double(std::int32_t(tw[0]) - std::int32_t(ew[0])), dy = double(std::int32_t(tw[1]) - std::int32_t(ew[1])), dz = double(std::int32_t(tw[2]) - std::int32_t(ew[2]));
                LOG("CAM t=%.2f ojo %d %d %d punto %d %d %d dir %.0f %.0f %.0f yawdir %.1f pitchdir %.1f apuntado %d %d s0=%08x\n", double(fields) / 50.0,
                    std::int32_t(ew[0]), std::int32_t(ew[1]), std::int32_t(ew[2]), std::int32_t(tw[0]), std::int32_t(tw[1]), std::int32_t(tw[2]), dx, dy, dz,
                    std::atan2(dx, dz) * 4096.0 / 6.283185307, std::atan2(dy, std::hypot(dx, dz)) * 4096.0 / 6.283185307, std::int32_t(ay), std::int32_t(ap), g[16]);
            }
        }
        if (window && pcHot && (cpu.state().pc == g_prof->aim_delta_aiming || cpu.state().pc == g_prof->aim_delta_walking)) {

            static const bool lateralApunta = std::getenv("SF3N_LATERAL_APUNTA") != nullptr;
            const bool aimPath = cpu.state().pc == g_prof->aim_delta_aiming && (lateralApunta || (g_pad_pressed & 0x0400u) || !(g_pad_pressed & 0x0300u));
            std::uint32_t insn = 0, player = 0;
            const auto& g = cpu.state().gpr;
            if (cpu.read32(cpu.state().pc, insn) && insn == (cpu.state().pc == g_prof->aim_delta_aiming ? 0x8d220000u : 0x8fa20010u) &&
                cpu.read32(g_prof->player_pointer, player) && player == g[16]) {
                int dyaw = 0, dpitch = 0;
                if (replaying) {
                    if (repPos + 9 <= repData.size() && repData[repPos] == 'A') {
                        auto g32 = [&](std::size_t o) { return std::uint32_t(repData[o]) | (std::uint32_t(repData[o + 1]) << 8) | (std::uint32_t(repData[o + 2]) << 16) | (std::uint32_t(repData[o + 3]) << 24); };
                        dyaw = int(std::int32_t(g32(repPos + 1))); dpitch = int(std::int32_t(g32(repPos + 5))); repPos += 9;
                        if (repPos + 5 <= repData.size() && repData[repPos] == 'T') {
                            const std::int32_t turn = std::int32_t(g32(repPos + 1)); repPos += 5;
                            std::uint32_t hd = 0; if (cpu.read32(player + 0x220, hd)) (void)cpu.write32(player + 0x220, std::uint32_t(std::int32_t(hd) + turn));
                        }
                        if (aimPath) g_aim_path_field = fields;
                        if (static const bool al = std::getenv("SF3N_AIMLOG") != nullptr; al && (dyaw || dpitch)) LOG("AIMREP campo %llu dyaw %d dpitch %d camino %s\n", (unsigned long long)fields, dyaw, dpitch, aimPath ? "apuntar" : "andar");
                    }
                } else {
                    window->takeAim(aimPath, dyaw, dpitch);
                    if (aimPath) g_aim_path_field = fields;
                    const int turn = window->takeAimTurn();
                    if (turn != 0) {
                        std::uint32_t hd = 0;
                        if (cpu.read32(player + 0x220, hd)) (void)cpu.write32(player + 0x220, std::uint32_t(std::int32_t(hd) + turn));
                    }
                    if (recCur.valid) { recCur.log.push_back('A'); recPut32(recCur.log, std::uint32_t(dyaw)); recPut32(recCur.log, std::uint32_t(dpitch));
                        if (turn != 0) { recCur.log.push_back('T'); recPut32(recCur.log, std::uint32_t(turn)); } }
                }
                std::uint32_t p0 = 0, y0 = 0;
                if ((dyaw || dpitch) && cpu.read32(g[9], p0) && cpu.read32(g[9] + 4, y0)) {
                    (void)cpu.write32(g[9], std::uint32_t(std::int32_t(p0) + dpitch));
                    (void)cpu.write32(g[9] + 4, std::uint32_t(std::int32_t(y0) + dyaw));
                }
            }
        }
#endif
        static const bool gteProf = std::getenv("SF3N_GTEPROF") != nullptr;
        static std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint64_t> gteHot;
        if (dbgEnv && gteProf) {
            std::uint32_t insn = 0;
            if (cpu.read32(cpu.state().pc, insn) && (insn >> 25) == 0x25u)
                gteHot[{cpu.state().pc, cpu.state().gpr[31]}]++;
            if (fields > 0 && fields % 250 == 0 && !gteHot.empty() && cpu.state().pc != 0) {
                static std::uint64_t lastDump = 0;
                if (lastDump != fields) {
                    lastDump = fields;
                    LOG("--- GTE RTPS/RTPT por pc (ra):");
                    for (auto& [k, n] : gteHot) LOG(" %08x(%08x)x%llu", k.first, k.second, (unsigned long long)n);
                    LOG("\n");
                    gteHot.clear();
                }
            }
        }
        pad.poll();
        if (g_trace > 0) { --g_trace; std::fprintf(stderr, "  pc %08x\n", cpu.state().pc); }

        bool fxHook = false, fxRtpt = false, fxActor = false;
        std::uint32_t fxFp = cpu.state().gpr[30];
        std::array<std::uint32_t, 32> fxCtl, fxIn;
        std::uint32_t fxRa = 0;
        if (window && pcHot) {
            const auto fpc = cpu.state().pc;
            const auto& P = *g_prof;
            if (fpc && (fpc == g_fx_overlay_rtps_all[0] || fpc == g_fx_overlay_rtps_all[1] || fpc == g_fx_overlay_rtps_all[2] || fpc == g_fx_overlay_rtps_all[3])) fxHook = true;
            else if (fpc == P.actor_rtpt || fpc == P.actor_rtpt_x) fxHook = fxRtpt = fxActor = true;
            else {
                static const bool noRtp4 = std::getenv("SF3N_SIN_RTP4") != nullptr;
                for (auto a : P.fx_rtps) if (a && fpc == a && !(noRtp4 && a == P.fx_rtps[6])) { fxHook = true; break; }

                if (static const bool sinPart = std::getenv("SF3N_SIN_PARTICULAS") != nullptr; !fxHook && !sinPart && fpc == 0x8007851cu) fxHook = true;
                if (!fxHook) for (auto a : P.fx_rtpt) if (a && fpc == a && !(noRtp4 && a == P.fx_rtpt[4])) { fxHook = fxRtpt = true; break; }
            }
            if (fxHook) {
                std::uint32_t insn = 0;
                fxHook = cpu.read32(cpu.state().pc, insn) && (insn == 0x4a180001u || insn == 0x4a280030u);
                fxCtl = cpu.state().gte.control; fxIn = cpu.state().gte.data; fxRa = cpu.state().gpr[31];
            }
        }
        if (dbgEnv) if (static const bool cc = std::getenv("SF3N_CDCALLER") != nullptr || std::getenv("SF3N_VOZLOG") != nullptr; dbgEnv && cc) {
            const auto& g = cpu.state().gpr;
            sf::psx::g_dbg_pc = cpu.state().pc; sf::psx::g_dbg_ra = g[31];
            for (int k = 0; k < 4; ++k) { sf::psx::g_dbg_regs[k] = g[4 + k]; sf::psx::g_dbg_regs[4 + k] = g[16 + k]; }
            std::uint32_t ins = 0;
            if (cpu.read32(cpu.state().pc, ins) && ((ins >> 26) == 3u || (ins & 0xfc00003fu) == 9u)) {
                sf::psx::g_dbg_calls[sf::psx::g_dbg_ncall & 1023u] = cpu.state().pc;
                sf::psx::g_dbg_args[sf::psx::g_dbg_ncall & 1023u] = g[4];
                ++sf::psx::g_dbg_ncall;
            }
        }
        const auto tickBefore = m.currentTick();
        static const bool luzPc = std::getenv("SF3N_LUZPC") != nullptr;
        std::uint32_t lpPc = 0; bool lpRtpt = false;
        if (luzPc && window) { std::uint32_t ins = 0; lpPc = cpu.state().pc; if (cpu.read32(lpPc, ins) && ((ins & 0xfe00003fu) == 0x4a000001u || (ins & 0xfe00003fu) == 0x4a000030u)) lpRtpt = (ins & 0x3f) == 0x30; else lpPc = 0; }
        const auto r = m.step();
        if (lpPc) { const auto& d = cpu.state().gte.data; for (int k = lpRtpt ? 0 : 2; k < 3; ++k) window->dbgProj(lpPc, d[std::size_t(12 + k)] & 0x07ff07ffu); }
        if (fxHook) { window->fx_ra_ = fxRa; window->onEffectProject(fxCtl, fxIn, cpu.state().gte.data, fxRtpt, fxActor, fxFp); }
        if (dbgEnv) if (static const char* gs = std::getenv("SF3N_GTESCAN"); dbgEnv && gs) {
            static double t0 = 0, t1 = 0, t2 = 0; static bool parsed = false;
            if (!parsed) { std::sscanf(gs, "%lf:%lf:%lf", &t0, &t1, &t2); parsed = true; }
            static std::map<std::uint32_t, std::uint64_t> base, scan; static std::map<std::uint32_t, std::string> sample; static bool dumped = false;
            const double tt = double(fields) / 50.0;
            if (tt >= t0 && tt < t2) {

                const auto lastPc = cpu.state().pc - 4;
                std::uint32_t insn = 0;
                if (cpu.read32(lastPc, insn) && (insn == 0x4a180001u || insn == 0x4a280030u)) {
                    if (tt < t1) base[lastPc]++;
                    else {
                        scan[lastPc]++;
                        if (!sample.count(lastPc)) { char b[160]; const auto& g = cpu.state().gte.data; std::snprintf(b, sizeof b, "SXY2 %d,%d SZ3 %u ra=%08x t=%.2f", int(std::int16_t(g[14])), int(std::int16_t(g[14] >> 16)), g[19], cpu.state().gpr[31], tt); sample[lastPc] = b; }
                    }
                }
            } else if (tt >= t2 && !dumped) {
                dumped = true;
                for (auto& [pc, n] : scan) LOG("GTESCAN %08x x%llu (base x%llu) %s\n", pc, (unsigned long long)n, (unsigned long long)base[pc], sample[pc].c_str());
            }
        }
        if (dbgEnv) if (static const std::uint32_t actTrace = std::getenv("SF3N_ACTTRACE") ? std::uint32_t(std::stoul(std::getenv("SF3N_ACTTRACE"), nullptr, 16)) : 0; dbgEnv && actTrace) {
            static std::uint32_t curA = 0; static int n = 0;
            const auto& st = cpu.state();
            if (st.pc == 0x8001b144u || st.pc == 0x8001bc88u) { curA = st.gpr[4]; if (curA == actTrace && n < 400) LOG("ACT a0=%08x pc=%08x a1=%08x a2=%08x a3=%08x s0=%08x s1=%08x s2=%08x s3=%08x ra=%08x\n", curA, st.pc, st.gpr[5], st.gpr[6], st.gpr[7], st.gpr[16], st.gpr[17], st.gpr[18], st.gpr[19], st.gpr[31]); }
            if ((st.pc == 0x8001b384u || st.pc == 0x8001be4cu) && curA == actTrace && n < 400) {
                ++n;
                const auto& c = st.gte.control;
                LOG("  RTPT fp-a0=%x ctl %08x %08x T %d %d %d V0 %d,%d,%d\n", st.gpr[30] - curA, c[0], c[1], int(c[5]), int(c[6]), int(c[7]), int(std::int16_t(st.gte.data[0])), int(std::int16_t(st.gte.data[0] >> 16)), int(std::int16_t(st.gte.data[1])));
            }
        }
        if (dbgEnv) if (static const std::uint32_t objTrace = std::getenv("SF3N_OBJTRACE") ? std::uint32_t(std::stoul(std::getenv("SF3N_OBJTRACE"), nullptr, 16)) : 0; dbgEnv && objTrace) {
            static std::uint32_t curObj = 0;
            static int n = 0;
            const auto& st = cpu.state();
            if (st.pc == 0x80018d60u) { curObj = st.gpr[4]; if (curObj == objTrace && n < 60) LOG("OBJ a0=%08x a1=%08x a2=%08x a3=%08x\n", st.gpr[4], st.gpr[5], st.gpr[6], st.gpr[7]); }
            if (st.pc == 0x80018ee4u && curObj == objTrace && n < 60) {
                const auto& g = st.gte.data;
                LOG("   SXY %d,%d %d,%d %d,%d SZ %u %u %u ctl %08x %08x %08x %08x %08x T %d %d %d\n", int(std::int16_t(g[12])), int(std::int16_t(g[12] >> 16)), int(std::int16_t(g[13])), int(std::int16_t(g[13] >> 16)), int(std::int16_t(g[14])), int(std::int16_t(g[14] >> 16)), g[17], g[18], g[19],
                    st.gte.control[0], st.gte.control[1], st.gte.control[2], st.gte.control[3], st.gte.control[4], int(st.gte.control[5]), int(st.gte.control[6]), int(st.gte.control[7]));
            }
            if (st.pc == 0x80018d64u && curObj == objTrace && n < 60) LOG("   entrada ctl %08x %08x %08x %08x %08x T %d %d %d\n", st.gte.control[0], st.gte.control[1], st.gte.control[2], st.gte.control[3], st.gte.control[4], int(st.gte.control[5]), int(st.gte.control[6]), int(st.gte.control[7]));
            if (st.pc == 0x80018ee0u && curObj == objTrace && n < 60) {
                const auto& g = st.gte.data;
                LOG("OBJRTPT V0 %d,%d,%d V1 %d,%d,%d V2 %d,%d,%d  tri@%08x\n",
                    int(std::int16_t(g[0])), int(std::int16_t(g[0] >> 16)), int(std::int16_t(g[1])), int(std::int16_t(g[2])), int(std::int16_t(g[2] >> 16)), int(std::int16_t(g[3])),
                    int(std::int16_t(g[4])), int(std::int16_t(g[4] >> 16)), int(std::int16_t(g[5])), st.gpr[17]);
                ++n;
            }
        }
        ++steps;
#ifdef SF3B_SDL
        if (window && gpu.display_flips != seen_flips) { seen_flips = gpu.display_flips; gpu.flipFrame(); window->onFlip(); }
#endif
        { const auto tk = m.currentTick(); if (tk - tickBefore > 100000) { static int jl = 0; if (jl++ < 10) LOG("SALTO de reloj +%llu en pc=%08x instr=%08x (%s)\n", (unsigned long long)(tk - tickBefore), r.pc, r.instruction, std::string(toString(r.reason)).c_str()); } }
        if ((steps & 0x3ff) == 0) hot[r.pc & ~0xffu]++;
        if ((steps & 0xff) == 0 && fields > 600 && !(r.pc >= 0x800f0000u && r.pc < 0x80110000u) && bios.saved.empty()) gameHot[r.pc & ~0xfu]++;
        if (!bios.saved.empty() && (steps & 0x3f) == 0) isrHot[r.pc & ~0xfu]++;
        if ((steps & 0xff) == 0) { ++samples; if (!bios.saved.empty()) ++inIsr; if (!(cpu.state().cop0_status & 1)) ++irqOff; }
        if (r.reason != R3000StopReason::running) {
            if (!bios.service(r)) {
                for (int k = 0; k < 32; k += 4) LOG("  r%-2d %08x %08x %08x %08x\n", k, cpu.state().gpr[k], cpu.state().gpr[k+1], cpu.state().gpr[k+2], cpu.state().gpr[k+3]);
                LOG("PARADA: %s en %08x (instr %08x) tras %llu instrucciones\n", std::string(toString(r.reason)).c_str(), r.pc,
                    r.instruction, (unsigned long long)steps);
                break;
            }
        }
        if (m.currentTick() >= nextHblank) {
            nextHblank += ticksPerField / linesPerField;
            m.setHBlank(true); m.setHBlank(false);
        }
        if (m.currentTick() >= nextVblank) {
            nextVblank += ticksPerField;
            ++fields;
            gpu.toggleField();

            {
                static std::vector<std::tuple<double, std::uint16_t, double>> script = [] {
                    std::vector<std::tuple<double, std::uint16_t, double>> v;
                    if (const char* e = std::getenv("SF3B_PAD")) {
                        std::string str(e); std::size_t p = 0;
                        while (p < str.size()) {
                            auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
                            auto item = str.substr(p, q - p); auto c = item.find(':');
                            const double t = std::atof(item.substr(0, c).c_str()); auto b = item.substr(c + 1); double dur = 0.2; if (auto c2 = b.find(':'); c2 != std::string::npos) { dur = std::atof(b.substr(c2 + 1).c_str()); b = b.substr(0, c2); }
                            std::uint16_t bit = b == "start" ? 0x0008 : b == "cross" ? 0x4000 : b == "circle" ? 0x2000 : b == "square" ? 0x8000 : b == "triangle" ? 0x1000 :
                                                b == "up" ? 0x0010 : b == "right" ? 0x0020 : b == "down" ? 0x0040 : b == "left" ? 0x0080 : b == "l1" ? 0x0400 : b == "r1" ? 0x0800 : b == "l2" ? 0x0100 : b == "r2" ? 0x0200 : b == "select" ? 0x0001 : 0;
                            v.push_back({t, bit, dur}); p = q + 1;
                        }
                    }
                    return v;
                }();
                std::uint16_t pressed = 0;
                const double now = double(fields) / 50.0;
                extraRoster().perField(cpu, gpu.soft.vram);
                if (const char* e = std::getenv("SF3N_DUMPVRAM")) {
                    std::string t(e); std::size_t p = 0;
                    while (p < t.size()) { auto q = t.find(',', p); if (q == std::string::npos) q = t.size();
                        if (fields == std::uint64_t(std::atof(t.substr(p, q - p).c_str()) * 50 + 0.5)) { char nm[64]; std::snprintf(nm, sizeof nm, "vram_%06llu.bin", (unsigned long long)fields);
                            if (FILE* f = std::fopen(nm, "wb")) { std::fwrite(gpu.soft.vram.data(), sizeof(gpu.soft.vram[0]), gpu.soft.vram.size(), f); std::fclose(f); } }
                        p = q + 1; }
                }
                g_textos.perField(cpu);
                if (static bool envDone = false; window && !envDone) { envDone = true;
                    if (const char* t = std::getenv("SF3N_TRUCOS")) { const std::string ts(t);
                        if (ts.find("god") != std::string::npos) window->cheat_god_ = true;
                        if (ts.find("armas") != std::string::npos) window->cheat_weapons_ = true;
                        if (ts.find("invis") != std::string::npos) window->cheat_invis_ = true;
                        if (ts.find("todo") != std::string::npos) window->cheat_unlock_req_ = true; } }
                if (window) {
                    auto r16 = [&](std::uint32_t a) { std::uint16_t v = 0; (void)cpu.read16(a, v); return v; };
                    auto w16 = [&](std::uint32_t a, std::uint16_t v) { (void)cpu.write16(a, v); };
                    if (window->cheat_unlock_req_.exchange(false)) {
                        w16(0x8014F9C4u, 0x0013);
                        w16(0x801223F4u, 0xFFFF);
                        for (std::uint32_t a = 0x8012D73Cu; a <= 0x8012D740u; ++a) (void)cpu.write8(a, 5);
                        w16(0x8015C794u, 0xFFFF);
                        LOG("trucos: todo desbloqueado\n");
                    }

                    static bool godOn = false; static std::uint16_t godOrig[3]{};
                    static const std::uint32_t godA[3] = {0x8005AEF4u, 0x8005AEF6u, 0x8005AEFAu};
                    static const std::uint16_t godV[3] = {0x0258, 0x2404, 0xA4C4};
                    const bool wantGod = window->cheat_god_.load();
                    if (wantGod && !godOn && r16(0x8005AEFCu) == 0xAAAB) { for (int i = 0; i < 3; ++i) { godOrig[i] = r16(godA[i]); w16(godA[i], godV[i]); } godOn = true; LOG("trucos: invulnerable\n"); }
                    else if (godOn && !wantGod) { for (int i = 0; i < 3; ++i) w16(godA[i], godOrig[i]); godOn = false; LOG("trucos: invulnerable quitado\n"); }
                    else if (godOn && r16(godA[1]) != godV[1] && r16(0x8005AEFCu) == 0xAAAB) for (int i = 0; i < 3; ++i) { godOrig[i] = r16(godA[i]); w16(godA[i], godV[i]); }

                    if (window->cheat_weapons_.load() && window->inMission()) {
                        static std::uint64_t lastFill = 0;

                        const bool flame = window->weapon_bank_.ok && window->weapon_bank_.by_name.count("FLAMETRK.GMD") && !std::getenv("SF3N_SIN_LANZALLAMAS");
                        const bool gatling = window->weapon_bank_.ok && window->weapon_bank_.by_name.count("CHNGUN.GMD");
                        const std::uint16_t hiMask = std::uint16_t(0xF9FF | (flame ? 0x0200 : 0) | (gatling ? 0x0400 : 0));

                        const bool stryker = window->weapon_bank_.ok && window->weapon_bank_.by_name.count("STRYKER.GMD") && !std::getenv("SF3N_SIN_STRYKER");
                        const std::uint16_t dcMask = std::uint16_t(0x005F | (stryker ? 0x0020 : 0));
                        const bool had = r16(0x8012D7D8u) == 0xFFFF && r16(0x8012D7DAu) == hiMask && r16(0x8012D7DCu) == dcMask;
                        w16(0x8012D7D8u, 0xFFFF); w16(0x8012D7DAu, hiMask); w16(0x8012D7DCu, dcMask);
                        if (!had || fields > lastFill + 50 * 30) {
                            for (std::uint32_t k = 0; k < 32; ++k) { const std::uint32_t a = 0x8012D7E2u + 4 * k; if (r16(a) < 99) w16(a, 99); }
                            if (stryker && r16(0x8012D7E2u + 4 * 37) < 99) w16(0x8012D7E2u + 4 * 37, 99);
                            lastFill = fields;
                        }
                    }

                    static bool invisOn = false;
                    static const int invisMode = std::getenv("SF3N_INVIS_MODO") ? std::atoi(std::getenv("SF3N_INVIS_MODO")) : 3;
                    if (window->cheat_invis_.load()) {
                        if (invisMode & 1) w16(0x80122604u, 1);
                        if ((invisMode & 2) && r16(0x8002B49Eu) == 0x1440) w16(0x8002B49Eu, 0x1000);
                        invisOn = true;
                    } else if (invisOn) {
                        if (invisMode & 1) w16(0x80122604u, 0);
                        if ((invisMode & 2) && r16(0x8002B49Eu) == 0x1000) w16(0x8002B49Eu, 0x1440);
                        invisOn = false;
                    }
                }
                for (auto& [t, b, d] : script) if (now >= t && now < t + d) pressed |= b;
#ifdef SF3B_SDL
                if (window) {
                    {
                        static std::vector<SpuPcmFrame> pcm(8192);
                        static std::uint64_t pcmTotal = 0, pcmLoud = 0;
                        int fieldPeak = 0;
                        for (;;) {
                            const auto n = m.spu().takePcm(pcm);
                            if (n == 0) break;
                            for (std::size_t k = 0; k < n; ++k) { if (pcm[k].left || pcm[k].right) ++pcmLoud; fieldPeak = std::max({fieldPeak, std::abs(int(pcm[k].left)), std::abs(int(pcm[k].right))}); }
                            pcmTotal += n;
                            window->queueAudio(reinterpret_cast<const std::int16_t*>(pcm.data()), n);
                            if (static FILE* raw = std::getenv("SF3N_AUDIODUMP") ? std::fopen(std::getenv("SF3N_AUDIODUMP"), "wb") : nullptr; raw) std::fwrite(pcm.data(), 4, n, raw);
                            if (n < pcm.size()) break;
                        }
                        if (fields % 500 == 0) LOG("audio: %llu muestras, %llu con sonido\n", (unsigned long long)pcmTotal, (unsigned long long)pcmLoud);
                        window->setTurboHints(fieldPeak, g_mdec ? g_mdec->blocks : 0);
                    }
                    window->setScriptedButtons(pressed);
                    if (!replaying) window->freeBodyTick([&](std::uint32_t ad) { std::uint32_t v = 0; (void)cpu.read32(ad, v); return v; },
                                                         [&](std::uint32_t ad, std::uint32_t v) { (void)cpu.write32(ad, v); },
                                                         [&](std::uint32_t ad, std::uint16_t v) { (void)cpu.write16(ad, v); });
                    window->field(gpu.soft); pressed = window->buttons();
                    if (window->quit) {

                        if (window->inMission() && std::getenv("SF3N_TEST") == nullptr) { const auto p = window->continuePath(); if (saveStateTo(p)) window->onStateSaved(p, currentFog); }
                        break;
                    }
                    sf::psx::g_spu_log_field = fields;
                    if (const char* d = std::getenv("SF3N_SHOTS")) {
                        static const int every = std::getenv("SF3N_SHOT_EVERY") ? std::atoi(std::getenv("SF3N_SHOT_EVERY")) : 50;
                        static const double from = std::getenv("SF3N_SHOT_FROM") ? std::atof(std::getenv("SF3N_SHOT_FROM")) : 0.0;
                        bool repShot = false;
                        static const std::uint64_t fAntes = std::getenv("SF3N_FOTOS_ANTES") ? std::atoll(std::getenv("SF3N_FOTOS_ANTES")) : 60, fPaso = std::getenv("SF3N_FOTOS_PASO") ? std::atoll(std::getenv("SF3N_FOTOS_PASO")) : 15;
                        static const std::uint64_t fDespues = std::getenv("SF3N_FOTOS_DESPUES") ? std::atoll(std::getenv("SF3N_FOTOS_DESPUES")) : 0;
                        if (g_rep_start) for (auto mk : g_rep_marks) { const auto mf = g_rep_start + mk; if (fields + fAntes >= mf && fields <= mf + fDespues && (mf + fDespues - fields) % fPaso == 0) repShot = true; }
                        if (std::getenv("SF3N_FOTOS_MARCAS") ? repShot : (double(fields) / 50.0 >= from && fields % every == 0)) {
                            char nm[256]; std::snprintf(nm, sizeof nm, "%s/n%06llu.ppm", d, (unsigned long long)fields); window->screenshot(nm);
                            if (std::getenv("SF3N_FOTO_ESPERA")) for (int w = 0; w < 20000 && window->shotPending(); ++w) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            std::snprintf(nm, sizeof nm, "%s/g%06llu.ppm", d, (unsigned long long)fields); dumpScreen(gpu.soft, nm);
                        }
                    }
                }
#endif

                if (static const char* rd = std::getenv("SF3N_RAMDUMP"); rd) {
                    static unsigned long long r0 = 0, r1 = 0, rs = 1; static bool rp = std::sscanf(rd, "%llu:%llu:%llu", &r0, &r1, &rs) == 3;
                    if (rp && rs && fields >= r0 && fields <= r1 && (fields - r0) % rs == 0) {
                        char nm[64]; std::snprintf(nm, sizeof nm, "ram_%llu.bin", (unsigned long long)fields);
                        if (std::FILE* f = std::fopen(nm, "wb")) { const auto r = cpu.ram(); std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }
                    }
                }

                if (static const bool vv = std::getenv("SF3N_VIGILAR_VIDA") != nullptr; vv) {
                    std::uint32_t p = 0, st = 0;
                    if (cpu.read32(0x8012d79cu, p) && (p & 0xffe00000u) == 0x80000000u && cpu.read32(p + 0x18, st) && (st & 0xffe00000u) == 0x80000000u) sf::psx::sf3_watch_dyn = st + 6;
                }

                if (static const char* pk = std::getenv("SF3N_POKE16"); pk) {
                    std::string all = pk; std::size_t p0 = 0;
                    while (p0 <= all.size()) { std::size_t q = all.find(',', p0); if (q == std::string::npos) q = all.size();
                        unsigned long long fc = 0; unsigned ad = 0; int v = 0;
                        if (std::sscanf(all.substr(p0, q - p0).c_str(), "%llu:%x:%d", &fc, &ad, &v) == 3 && fields == fc) { (void)cpu.write16(ad, std::uint16_t(v)); LOG("POKE16 campo %llu %08x = %d\n", fc, ad, v); }
                        p0 = q + 1; }
                }
                std::optional<std::array<std::uint8_t, 4>> repSticks;
                if (replaying) {

                    bool got = false;
                    while (repPos < repData.size() && !got) {
                        const auto tag = repData[repPos];
                        if (tag == 'M') { LOG("REPETICION MARCA campo %llu (t=%.2fs)\n", (unsigned long long)fields, double(fields) / 50.0); ++repPos; continue; }
                        if (tag == 'A') { repPos += 9; continue; }
                        if (tag == 'Y') { repPos += 9; continue; }
                        if (tag == 'T') { repPos += 5; continue; }
                        if (tag == 'F' && repPos + 7 <= repData.size()) {
                            pressed = std::uint16_t(repData[repPos + 1] | (repData[repPos + 2] << 8));
                            repSticks = std::array<std::uint8_t, 4>{repData[repPos + 3], repData[repPos + 4], repData[repPos + 5], repData[repPos + 6]};
                            repPos += 7; got = true; continue;
                        }
                        repPos = repData.size();
                    }
                    if (static const long long hasta = std::getenv("SF3N_REPETIR_HASTA") ? std::atoll(std::getenv("SF3N_REPETIR_HASTA")) : -1; hasta > 0 && (long long)fields >= hasta) got = false, repPos = repData.size();
                    if (!got) {
                        replaying = false;
                        LOG("REPETICION FIN campo %llu (t=%.2fs)\n", (unsigned long long)fields, double(fields) / 50.0);
#ifdef SF3B_SDL
                        if (window && std::getenv("SF3N_FOTOS_MARCAS")) window->quit = true;
#endif
                    }
                }
                pad.buttons = std::uint16_t(0xffff & ~pressed);
                g_pad_pressed = pressed;
                {
                    static std::uint16_t prevPressed = 0;
                    const std::uint16_t fresh = std::uint16_t(pressed & ~prevPressed);
                    prevPressed = pressed;

                    const std::uint32_t mp = 0x80147660u, activa = 0x8014af10u;
                    std::uint32_t insn = 0; (void)cpu.read32(mp, insn);
                    std::uint8_t act = 0; (void)cpu.read8(activa, act);
                    if (act && g_movie_skippable && (pressed & 0x4008u) && insn == 0x27bdffc0u && std::getenv("SF3N_SIN_SALTAR") == nullptr) {
                        auto ram = cpu.ram();
                        auto w = [&](std::uint32_t a) { std::uint32_t v; std::memcpy(&v, ram.data() + (a & 0x1fffffu), 4); return v; };
                        int n = 0;
                        for (std::uint32_t a = 0x80140000u; a + 24 <= 0x80200000u && a + 24 <= 0x80000000u + ram.size(); a += 4) {
                            if ((w(a) >> 16) != 0x8c42u || (w(a + 4) >> 16) != 0x3c03u || (w(a + 8) >> 16) != 0x8c63u || w(a + 12) != 0x27bdffe0u || w(a + 16) != 0xafbf001cu || w(a + 20) != 0x0043102bu) continue;
                            const std::uint32_t thr = (w(a + 4) << 16) + std::uint32_t(std::int32_t(std::int16_t(w(a + 8) & 0xffffu)));
                            std::uint32_t old = 0; (void)cpu.read32(thr, old);
                            if (old != 0) { (void)cpu.write32(thr, 0); ++n; }
                        }

                        static std::uint32_t cuenta = 0;
                        if (!cuenta)
                            for (std::uint32_t a = 0x80010000u; a + 16 <= 0x80100000u; a += 4)
                                if ((w(a) >> 16) == 0x9782u && w(a + 8) == 0x24430001u && w(a + 12) == 0x2c420029u) { cuenta = cpu.state().gpr[28] + std::uint32_t(std::int32_t(std::int16_t(w(a) & 0xffffu))); break; }
                        if (cuenta) { std::uint16_t c = 0; (void)cpu.read16(cuenta, c); if (c < 0x29u) { (void)cpu.write16(cuenta, 0x29); ++n; } }
                        if (n && (fresh & 0x4008u)) LOG("pelicula: salto permitido ya (t=%.1fs)\n", double(fields) / 50.0);
                        if (pressed & 0x0008u) pad.buttons = std::uint16_t(pad.buttons & ~0x4000u);

                        if (g_movie_logo && (fresh & 0x4008u)) { (void)cpu.write8(0x8014af1bu, 1); LOG("pelicula: logo saltado (t=%.1fs)\n", double(fields) / 50.0); }
                    }
                }
                pad.analog = std::getenv("SF3N_DIGITAL") == nullptr;
                {

                    static std::vector<std::tuple<double, std::uint16_t, double>> script2 = [] {
                        std::vector<std::tuple<double, std::uint16_t, double>> v;
                        if (const char* e = std::getenv("SF3B_PAD2")) {
                            std::string str(e); std::size_t p = 0;
                            while (p < str.size()) {
                                auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
                                auto item = str.substr(p, q - p); auto c = item.find(':');
                                const double t = std::atof(item.substr(0, c).c_str()); auto b = item.substr(c + 1); double dur = 0.2; if (auto c2 = b.find(':'); c2 != std::string::npos) { dur = std::atof(b.substr(c2 + 1).c_str()); b = b.substr(0, c2); }
                                std::uint16_t bit = b == "start" ? 0x0008 : b == "cross" ? 0x4000 : b == "circle" ? 0x2000 : b == "square" ? 0x8000 : b == "triangle" ? 0x1000 :
                                                    b == "up" ? 0x0010 : b == "right" ? 0x0020 : b == "down" ? 0x0040 : b == "left" ? 0x0080 : b == "l1" ? 0x0400 : b == "r1" ? 0x0800 : b == "select" ? 0x0001 : 0;
                                v.push_back({t, bit, dur}); p = q + 1;
                            }
                        }
                        return v;
                    }();
                    std::uint16_t pressed2 = 0;
                    for (auto& [t, b, d] : script2) if (now >= t && now < t + d) pressed2 |= b;
                    bool connected2 = !script2.empty();
                    std::array<std::uint8_t, 4> st2{128, 128, 128, 128};
#ifdef SF3B_SDL
                    if (window) {

                        const auto& ra = extraRoster().A;
                        std::uint8_t mpMode = 0; std::uint32_t op = 0; (void)cpu.read8(ra.mode, mpMode); (void)cpu.read32(ra.hookRight, op);
                        const bool title2 = op == ExtraRoster::kHookRightOp;
                        window->setTwoPlayer(mpMode == 1 && (title2 || currentFog.find("FOG2") != std::string::npos));
                        connected2 = connected2 || window->p2Connected();
                        pressed2 |= window->buttons2();
                        st2 = window->sticks2();
                    }
#endif
                    pad.p2.connected = connected2;
                    pad.p2.buttons = std::uint16_t(0xffff & ~pressed2);
                    for (int k = 0; k < 4; ++k) pad.p2.sticks[k] = st2[std::size_t(k)];
                }
                if (std::getenv("SF3N_RUMBLELOG") && (pad.motor_small || pad.motor_large)) { static int rl = 0; if (rl++ < 300) LOG("vibracion t=%.2f pequeno=%d grande=%d\n", double(fields) / 50.0, pad.motor_small, pad.motor_large); }
#ifdef SF3B_SDL
                if (window) window->setRumble(pad.motor_small, pad.motor_large);
#endif
                if (pad.analog) {
                    auto st = window ? window->sticks() : std::array<std::uint8_t, 4>{128, 128, 128, 128};
                    if (repSticks) st = *repSticks;
                    for (int k = 0; k < 4; ++k) pad.sticks[k] = st[std::size_t(k)];

                    if (const char* e = std::getenv("SF3N_STICK")) {
                        std::string str(e); std::size_t p = 0;
                        while (p < str.size()) {
                            auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
                            double t0 = 0, t1 = 0; int ax = 0, val = 0x80;
                            if (std::sscanf(str.substr(p, q - p).c_str(), "%lf:%lf:%d:%d", &t0, &t1, &ax, &val) == 4 && double(fields) / 50.0 >= t0 && double(fields) / 50.0 < t1 && ax >= 0 && ax < 4) pad.sticks[ax] = std::uint8_t(val);
                            p = q + 1;
                        }
                    }
                }
                lastPadBytes = {std::uint8_t(~pad.buttons & 0xff), std::uint8_t((~pad.buttons >> 8) & 0xff), pad.sticks[0], pad.sticks[1], pad.sticks[2], pad.sticks[3]};
                if (recCur.valid && !replaying) {
                    recCur.log.push_back('F');
                    recCur.log.insert(recCur.log.end(), lastPadBytes.begin(), lastPadBytes.end());
                }
            }
            if (const char* e = std::getenv("SF3N_PKTDUMP")) {
                const auto f0 = std::uint64_t(std::atof(e) * 50 + 0.5);
                if (fields == f0 && !gpu.pkt_log_) gpu.pkt_log_ = std::fopen("gp0_campo.txt", "w");
                else if (fields == f0 + 12 && gpu.pkt_log_) { std::fclose(gpu.pkt_log_); gpu.pkt_log_ = nullptr; }
            }
            if (const char* e = std::getenv("SF3N_HUDDUMP"); e && fields == std::uint64_t(std::atof(e) * 50 + 0.5)) {
                for (const auto& p : gpu.hud) { LOG("HUD"); for (auto w : p) LOG(" %08x", w); LOG("\n"); }
            }
            if (const char* e = std::getenv("SF3N_READWATCH_RANGO")) {
                static bool on = false;
                unsigned lo = 0, hi = 0; double t0 = 0;
                if (!on && std::sscanf(e, "%x:%x:%lf", &lo, &hi, &t0) == 3 && double(fields) / 50.0 >= t0) {
                    on = true; sf::psx::g_read_watch_lo = lo; sf::psx::g_read_watch_hi = hi;
                    sf::psx::g_read_watch = [](std::uint32_t a, std::uint32_t pc, std::uint32_t ra) {
                        static std::map<std::pair<std::uint32_t, std::uint32_t>, int> seen;
                        if (seen[{pc, ra}]++ < 3) LOG("READWATCH %06x pc %08x ra %08x\n", a, pc, ra);
                    };
                }
            }
            if (const char* e = std::getenv("SF3N_READWATCH_STR")) {
                static std::string needle(e);
                static std::uint64_t lastScan = 0;
                if (fields - lastScan >= 25) {
                    lastScan = fields;
                    auto r = cpu.ram();
                    const auto* b = reinterpret_cast<const char*>(r.data());
                    std::string_view hay(b, r.size());
                    const auto pos = hay.find(needle);
                    if (pos != std::string_view::npos && (sf::psx::g_read_watch_lo != std::uint32_t(pos) )) {
                        sf::psx::g_read_watch_lo = std::uint32_t(pos); sf::psx::g_read_watch_hi = std::uint32_t(pos + needle.size());
                        LOG("READWATCH texto en %06x (campo %llu)\n", unsigned(pos), (unsigned long long)fields);
                        sf::psx::g_read_watch = [](std::uint32_t a, std::uint32_t pc, std::uint32_t ra) {
                            static std::map<std::pair<std::uint32_t, std::uint32_t>, int> seen;
                            if (seen[{pc, ra}]++ == 0) LOG("READWATCH %06x pc %08x ra %08x\n", a, pc, ra);
                        };
                    }
                }
            }
#ifdef SF3B_SDL
#ifdef SF3B_SDL
            if (window) window->game_field_ = fields;
#endif
            if (const char* e = std::getenv("SF3N_DUMPRAM")) {
                static std::vector<std::uint64_t> at = [e] { std::vector<std::uint64_t> v; std::string t(e); std::size_t p = 0; while (p < t.size()) { auto q = t.find(',', p); if (q == std::string::npos) q = t.size(); v.push_back(std::uint64_t(std::atof(t.substr(p, q - p).c_str()) * 50 + 0.5)); p = q + 1; } return v; }();
                if (std::find(at.begin(), at.end(), fields) != at.end()) {
                    char nm[64]; std::snprintf(nm, sizeof nm, "ram_%06llu.bin", (unsigned long long)fields);
                    if (FILE* f = std::fopen(nm, "wb")) { auto r = cpu.ram(); std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }
                    std::snprintf(nm, sizeof nm, "extra_%06llu.bin", (unsigned long long)fields);
                    if (FILE* f = std::fopen(nm, "wb")) { auto r = cpu.extraRam(); std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }
                    std::snprintf(nm, sizeof nm, "armas_%06llu.bin", (unsigned long long)fields);
                    if (FILE* f = std::fopen(nm, "wb")) { auto r = cpu.armsRam(); std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }
                }
            }
            if (const char* e = std::getenv("SF3N_POKE16")) {
                std::string str(e); std::size_t p = 0;
                while (p < str.size()) {
                    auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
                    double t = 0; unsigned addr = 0; int delta = 0;
                    if (std::sscanf(str.substr(p, q - p).c_str(), "%lf:%x:%d", &t, &addr, &delta) == 3 && fields == std::uint64_t(t * 50 + 0.5)) {
                        std::uint16_t v = 0; (void)cpu.read16(addr, v); (void)cpu.write16(addr, std::uint16_t(v + delta));
                        LOG("POKE %08x %d -> %d\n", addr, int(std::int16_t(v)), int(std::int16_t(v + delta)));
                    }
                    p = q + 1;
                }
            }
            if (const char* e = std::getenv("SF3N_FAKEMOUSE"); e && window) {
                std::string str(e); std::size_t p = 0;
                while (p < str.size()) {
                    auto q = str.find(',', p); if (q == std::string::npos) q = str.size();
                    double t0 = 0, t1 = 0, dx = 0, dy = 0;
                    if (std::sscanf(str.substr(p, q - p).c_str(), "%lf:%lf:%lf:%lf", &t0, &t1, &dx, &dy) == 4 && double(fields) / 50.0 >= t0 && double(fields) / 50.0 < t1) window->addMouseTest(dx, dy);
                    p = q + 1;
                }
            }
            if (!g_cam_dir && fields % 50 == 11) {
                static const std::uint32_t pat[20] = {0x8e0600a4u, 0u, 0x8cc208e0u, 0u, 0x14400004u, 0u, 0x8cc208e8u, 0u, 0xafa20030u, 0x8cc208e8u, 0x8cc308ecu, 0x8cc408f0u, 0x8cc508f4u, 0xafa20030u, 0xafa30034u, 0xafa40038u, 0xafa5003cu, 0x8fa20020u, 0x8fa30010u, 0u};
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 80 <= 0x100000u && a + 80 <= ram.size(); a += 4) {
                    bool ok = true;
                    for (int k = 0; k < 20 && ok; ++k) { if (k == 7) continue; std::uint32_t v; std::memcpy(&v, ram.data() + a + 4 * k, 4); ok = v == pat[k]; }
                    if (ok) { g_cam_dir = 0x80000000u + a + 0x44; LOG("camara: direccion en %08x\n", g_cam_dir); rebuildHooks(); break; }
                }
            }
            if (window && fields % 10 == 3) {
                std::uint16_t cur = 0xffff;
                (void)cpu.read16(0x8012e2c4u, cur);
                window->setDebugMissions(true, std::int16_t(cur));
            }
            if (!g_camupd_in && fields % 50 == 27) {
                static const std::uint32_t pat[10] = {0x27bdffe8u, 0xafb00010u, 0x00808021u, 0xafbf0014u, 0x3c011f80u, 0xac3d03fcu, 0x3c1d1f80u, 0x37bd03dcu, 0x02002021u, 0u};
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 40 <= 0x100000u && a + 40 <= ram.size(); a += 4)
                    if (std::memcmp(ram.data() + a, pat, 36) == 0) { std::uint32_t j = 0; std::memcpy(&j, ram.data() + a + 36, 4); if ((j >> 26) == 3u) { g_camupd_in = 0x80000000u + a; LOG("camara: actualizacion en %08x\n", g_camupd_in); rebuildHooks(); break; } }
            }
            if (!g_radar_fn && fields % 50 == 25) {
                static const std::uint32_t pat[8] = {0x27bdff40u, 0xafb200b0u, 0x00809021u, 0xafbf00bcu, 0xafb400b8u, 0xafb300b4u, 0xafb100acu, 0xafb000a8u};
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 32 <= 0x100000u && a + 32 <= ram.size(); a += 4)
                    if (std::memcmp(ram.data() + a, pat, 32) == 0) { g_radar_fn = 0x80000000u + a; LOG("radar: funcion en %08x\n", g_radar_fn); rebuildHooks(); break; }
            }
            if (!g_cam_step_a && fields % 50 == 23) {
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 12 <= 0x100000u && a + 12 <= ram.size(); a += 4) {
                    std::uint32_t w[3]; std::memcpy(w, ram.data() + a, 12);
                    if (w[0] == 0xae220038u && (w[1] >> 26) == 2u && w[2] == 0x02401021u) g_cam_step_a = 0x80000000u + a + 4;
                    if (w[0] == 0xae220038u && w[1] == 0x8e220010u && w[2] == 0x3c108013u) g_cam_step_b = 0x80000000u + a + 4;
                }
                if (g_cam_step_a && g_cam_step_b) { LOG("camara: pasos en %08x %08x\n", g_cam_step_a, g_cam_step_b); rebuildHooks(); }
                else g_cam_step_a = g_cam_step_b = 0;
            }
            if (!g_radar_cone && fields % 50 == 21) {
                static const std::uint32_t pat[8] = {0x27bdff88u, 0xafbe0070u, 0x0080f021u, 0x00e02021u, 0xafb00050u, 0x000581c0u, 0x00c02821u, 0x261002ecu};
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 32 <= 0x100000u && a + 32 <= ram.size(); a += 4) {
                    if (std::memcmp(ram.data() + a, pat, 32) == 0) { g_radar_cone = 0x80000000u + a; LOG("radar: cono en %08x\n", g_radar_cone); rebuildHooks(); break; }
                }
            }
            if (!g_radar_arrow && fields % 50 == 19) {
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 20 <= 0x100000u && a + 20 <= ram.size(); a += 4) {
                    std::uint32_t w[5]; std::memcpy(w, ram.data() + a, 20);
                    if ((w[0] >> 26) == 3u && w[1] == 0x00a03021u && w[2] == 0x267202e8u && w[3] == 0x8fa40018u && w[4] == 0x8fa50010u) { g_radar_arrow = 0x80000000u + a + 8; LOG("radar: flecha en %08x\n", g_radar_arrow); rebuildHooks(); break; }
                }
            }
            if (!g_radar_blip && fields % 50 == 17) {
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 20 <= 0x100000u && a + 20 <= ram.size(); a += 4) {
                    std::uint32_t w[5]; std::memcpy(w, ram.data() + a, 20);
                    if ((w[0] >> 26) == 3u && w[1] == 0xafa20018u && w[2] == 0x8fb20010u && w[3] == 0x8fb10014u && w[4] == 0x03d2102au) { g_radar_blip = 0x80000000u + a; LOG("radar: puntos en %08x\n", g_radar_blip); rebuildHooks(); break; }
                }
            }
            if (!g_weapon_pick && fields % 50 == 13) {
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 16 <= 0x100000u && a + 16 <= ram.size(); a += 4) {
                    std::uint32_t w[4]; std::memcpy(w, ram.data() + a, 16);
                    if (w[0] == 0x02a08021u && (w[1] >> 26) == 2u && w[2] == 0xae9000a8u && w[3] == 0xae7000e4u) { g_weapon_pick = 0x80000000u + a + 4; LOG("armas: eleccion en %08x\n", g_weapon_pick); if (window) window->setWeaponPickHooked(); rebuildHooks(); break; }
                }
            }
            if (!g_fade_step && fields % 50 == 9) {
                static const std::uint32_t pat[12] = {0x308400ffu, 0x10800008u, 0u, 0x8ca30000u, 0u, 0x2862000cu, 0x10400007u, 0x24620001u, 0u, 0xaca20000u, 0x8ca20000u, 0u};
                auto ram = cpu.ram();
                for (std::uint32_t a = 0x10000u; a + 48 <= 0x100000u && a + 48 <= ram.size(); a += 4) {
                    bool ok = true;
                    for (int k = 0; k < 12 && ok; ++k) { if (k == 8) continue; std::uint32_t v; std::memcpy(&v, ram.data() + a + 4 * k, 4); ok = v == pat[k]; }
                    if (ok) { g_fade_step = 0x80000000u + a; LOG("fundidos: paso en %08x\n", g_fade_step); rebuildHooks(); break; }
                }
                for (std::uint32_t a = 0x10000u; a + 40 <= 0x100000u && a + 40 <= ram.size(); a += 4) {
                    std::uint32_t v[10]; std::memcpy(v, ram.data() + a, 40);
                    if ((v[0] >> 16) == 0x9782u && (v[1] >> 16) == 0x9383u && v[2] == 0x27bdffe8u && v[3] == 0xafbf0010u && v[4] == 0x24420001u && v[5] == (0xa7820000u | (v[0] & 0xffffu)) && v[9] == 0x2842001fu) {
                        g_pause_wait = 0x80000000u + a; LOG("pausa: espera en %08x\n", g_pause_wait); rebuildHooks(); break; }
                }
            }
            if (window && fields % 50 == 7) { const auto before = g_fx_overlay_rtps_all; findFxOverlay(cpu.ram()); if (before != g_fx_overlay_rtps_all) rebuildHooks(); }
            if (!initialLoadDone && fields > 5) {
                initialLoadDone = true;
                if (!repState.empty()) {
                    Ser sr; sr.in = repState.data(); sr.size = repState.size(); serialize(sr);
                    ++g_idle_epoch;
                    if (sr.ok && window) window->afterStateLoad(currentFog);
                    pad.buttons = std::uint16_t(~(repPad[0] | (repPad[1] << 8)));
                    for (int k = 0; k < 4; ++k) pad.sticks[k] = repPad[std::size_t(2 + k)];
                    replaying = sr.ok;
                    g_rep_start = fields;
                    {
                        std::string ms;
                        for (auto mk : g_rep_marks) { char b[48]; std::snprintf(b, sizeof b, " %llu(t=%.2f)", (unsigned long long)(fields + mk), double(fields + mk) / 50.0); ms += b; }
                        LOG("repeticion: %llu campos, marcas en%s\n", (unsigned long long)g_rep_fields, ms.c_str());
                    }
                    LOG("repeticion: estado %s, empieza en t=%.2fs\n", sr.ok ? "cargado" : "NO VALIDO", double(fields) / 50.0);
                } else loadState(bootLoad);
                recNewSeg = true; recDiscardPrev = true;
            }
            if (window && !replaying && std::getenv("SF3N_NO_GRABAR") == nullptr) {
                static const std::uint64_t segFields = std::getenv("SF3N_SEG_SEG") ? std::uint64_t(std::atof(std::getenv("SF3N_SEG_SEG")) * 50) : 50 * 60;
                if (recNewSeg || !recCur.valid || fields - recCur.start >= segFields) snapshotSegment();
                static const double markAt = std::getenv("SF3N_MARCAR_AT") ? std::atof(std::getenv("SF3N_MARCAR_AT")) : -1;
                if (window->takeMarkRequest() || (markAt > 0 && fields == std::uint64_t(markAt * 50))) {
                    std::time_t now = std::time(nullptr);
                    std::tm tm{};
#ifdef _WIN32
                    localtime_s(&tm, &now);
#else
                    localtime_r(&now, &tm);
#endif
                    char nm[64];
                    std::snprintf(nm, sizeof nm, "error_%04d%02d%02d_%02d%02d%02d.sf3r", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                    recCur.log.push_back('M');
                    const std::string path = BootWindow::dataDir() + "grabaciones/" + nm;
                    window->notify(writeRecording(path) ? std::string("Error marcado: ") + nm : std::string("No se pudo guardar la grabacion"));
                    recCur.log.pop_back();

                    recCur.log.push_back('M');
                }
            }
            if (window) {
                if (const char* e = std::getenv("SF3N_SAVE_AT")) { if (fields == std::uint64_t(std::atof(e) * 50)) saveStateTo(statePath(1)); }
                if (const char* e = std::getenv("SF3N_TESTSAVE")) {
                    const auto t = std::uint64_t(std::atof(e) * 50);
                    if (fields == t) window->requestSave(window->slotPath(1));
                    if (fields == t + 100) window->requestLoad(window->slotPath(1));
                }
                if (const int slot = window->takeSaveRequest()) { (void)slot; window->requestSave(window->quickPath()); }
                if (const int slot = window->takeLoadRequest()) { (void)slot; window->requestLoad(window->quickPath()); }
                if (const auto p = window->takeSavePath(); !p.empty()) {
                    const bool ok = saveStateTo(p);
                    if (ok) window->onStateSaved(p, currentFog);
                    if (p.find("mision_") == std::string::npos) window->notify(ok ? "Partida guardada" : "No se pudo guardar la partida");
                }
                {

                    bool top = false;
                    {
                        std::uint32_t a = 0, b = 0; std::uint8_t lvl = 0, act = 0;
                        (void)cpu.read32(0x8015450cu, a); (void)cpu.read32(0x80154510u, b); (void)cpu.read8(0x80157420u, lvl); (void)cpu.read8(0x8014af10u, act);
                        top = a == 0x3c028012u && b == 0x8c4223b8u && lvl == 1 && act && !g_movie_skippable;
                    }
                    static bool prevTop = false;
                    if (top != prevTop && std::getenv("SF3N_TITLELOG")) { std::uint8_t lvl = 0, act = 0; (void)cpu.read8(0x80157420u, lvl); (void)cpu.read8(0x8014af10u, act); LOG("titulo: %d (nivel %u video %u saltable %d) t=%.2f\n", int(top), lvl, act, int(g_movie_skippable), double(fields) / 50.0); }
                    if (top && !prevTop && std::getenv("SF3N_SIN_OPCIONES") == nullptr) { if (saveStateTo(window->titlePath())) LOG("titulo: estado guardado (t=%.1fs)\n", double(fields) / 50.0); }
                    prevTop = top;
                    window->setTitleTop(top && std::getenv("SF3N_SIN_OPCIONES") == nullptr);
                    { std::uint8_t nv = 0; (void)cpu.read8(0x80121a80u, nv); window->setNightVision(nv != 0); }
                    {
                        std::uint32_t a = 0, b = 0; std::uint8_t lvl = 0, act = 0;
                        (void)cpu.read32(0x8015450cu, a); (void)cpu.read32(0x80154510u, b); (void)cpu.read8(0x80157420u, lvl); (void)cpu.read8(0x8014af10u, act);
                        window->setTitleSub(a == 0x3c028012u && b == 0x8c4223b8u && lvl == 2 && act && !g_movie_skippable && std::getenv("SF3N_SIN_OPCIONES") == nullptr);
                        static int backTries = 0; static std::uint64_t backAt = 0; static bool backPend = false;
                        if (window->takeSubBack()) { window->injectPadSeqSoft({{0, 2}, {0x1000, 10}, {0, 4}}); backPend = true; backTries = 1; backAt = fields; }
                        if (backPend) {
                            if (lvl != 2) backPend = false;
                            else if (fields > backAt + 25 && window->padSeqIdle()) { if (backTries++ < 10) { window->injectPadSeqSoft({{0, 2}, {0x1000, 10}, {0, 4}}); backAt = fields; } else backPend = false; }
                        }
                    }

                    static int pendGo = -1, pendLvl = 0, pendTries = 0; static std::uint64_t pendAt = 0;
                    auto titleLevel = [&] {
                        std::uint32_t a = 0, b = 0; std::uint8_t lvl = 0, act = 0;
                        (void)cpu.read32(0x8015450cu, a); (void)cpu.read32(0x80154510u, b); (void)cpu.read8(0x80157420u, lvl); (void)cpu.read8(0x8014af10u, act);
                        return (a == 0x3c028012u && b == 0x8c4223b8u && act && !g_movie_skippable) ? int(lvl) : 0;
                    };
                    auto pressGo = [&](int go, int lvl) {
                        (void)cpu.write8(0x80157424u, std::uint8_t(go));

                        static const int p0 = std::getenv("SF3N_TITULO_PULSA") ? std::atoi(std::getenv("SF3N_TITULO_PULSA")) : 10;
                        window->injectPadSeqSoft({{0, 2}, {0x4000, p0}, {0, 4}});
                        pendGo = go; pendLvl = lvl; pendAt = fields; ++pendTries;
                    };
                    if (const int go = window->takeTitleGo(); go >= 0) { pendTries = 0; pressGo(go, 1); LOG("titulo: %s\n", go ? "Cargar Juego" : "Juego Nuevo"); }
                    if (const int go = window->takeSubGo(); go >= 0) { pendTries = 0; pressGo(go, 2); LOG("titulo: submenu opcion %d\n", go); }
                    if (pendGo >= 0) {
                        if (titleLevel() != pendLvl) pendGo = -1;
                        else if (fields > pendAt + 8 && window->padSeqIdle()) { if (pendTries < 30) pressGo(pendGo, pendLvl); else pendGo = -1; }
                    }
                    if (bios.store_ok) cardFieldTick();
                    deathTick();
                    if (static const double tp = std::getenv("SF3N_TARJETA_UI_PRUEBA") ? std::atof(std::getenv("SF3N_TARJETA_UI_PRUEBA")) : -1; tp > 0 && fields == std::uint64_t(tp * 50)) {
                        std::vector<std::string> it{"NUEVA PARTIDA GUARDADA"};
                        for (const auto& sl : bios.store.slots()) it.push_back(slotText(sl));
                        window->openCardPicker(true, it);
                        std::thread([w = window.get()] { std::this_thread::sleep_for(std::chrono::seconds(3)); w->screenshot("lista.ppm"); std::this_thread::sleep_for(std::chrono::seconds(2)); std::_Exit(0); }).detach();
                    }
                    if (window->takeTitleRestore()) { const bool ok = loadState(window->titlePath()); LOG("titulo: vuelta de opciones, estado %s\n", ok ? "cargado" : "no cargado"); recNewSeg = true; recDiscardPrev = true; }
                }
                if (const auto p = window->takeLoadPath(); !p.empty()) { window->notify(loadState(p) ? "Partida cargada" : "No se pudo cargar la partida"); recNewSeg = true; recDiscardPrev = true; }
            }
#endif
            bios.vblank();
            m.pulseVBlank();
            if (fields >= 520 && fields < 530) {
                std::uint32_t ist{}, imk{}; (void)cpu.read32(0x1f801070, ist); (void)cpu.read32(0x1f801074, imk);
                LOG("  campo %llu pc=%08x SR=%08x CAUSE=%08x I_STAT=%04x I_MASK=%04x isr=%zu\n", (unsigned long long)fields, cpu.state().pc, cpu.state().cop0_status, cpu.state().cop0_cause, ist, imk, bios.saved.size());
            }
            if (fields % 50 == 0 && std::getenv("SF3B_SHOTS")) {
                char name[128]; std::snprintf(name, sizeof name, "%s/boot_%03llu.ppm", std::getenv("SF3B_SHOTS"), (unsigned long long)(fields / 50));
                dumpScreen(gpu.soft, name);
                std::snprintf(name, sizeof name, "%s/vram_%03llu.bin", std::getenv("SF3B_SHOTS"), (unsigned long long)(fields / 50));
                if (FILE* vf = std::fopen(name, "wb")) { std::fwrite(gpu.soft.vram.data(), 2, gpu.soft.vram.size(), vf); std::fclose(vf); }
                LOG("     pantalla: %d,%d %dx%d %s on=%d | DMA0/1 MDEC chcr %08x %08x\n", gpu.soft.disp_x, gpu.soft.disp_y, gpu.soft.disp_w, gpu.soft.disp_h, gpu.soft.rgb24 ? "24bit" : "15bit", gpu.display_on,
                    [&]{std::uint32_t v{}; (void)cpu.read32(0x1f801088, v); return v;}(), [&]{std::uint32_t v{}; (void)cpu.read32(0x1f801098, v); return v;}());
            }
            if (fields % 50 == 0) {
                LOG("esperas saltadas: %llu\n", (unsigned long long)g_idle_skips);
                LOG("t=%3llus pc=%08x sectores CD=%llu flips=%llu(+%llu) prims=%llu cargas VRAM=%llu irq=%llu\n",
                    (unsigned long long)(fields / 50), cpu.state().pc, (unsigned long long)media.reads,
                    (unsigned long long)gpu.display_flips, (unsigned long long)(gpu.display_flips - lastFlips),
                    (unsigned long long)gpu.primitives, (unsigned long long)gpu.image_uploads,
                    (unsigned long long)bios.counts["irq"]);
                lastFlips = gpu.display_flips;
                if (std::getenv("SF3N_CARDLOG")) { const auto sp = cpu.state().gpr[29]; std::string st; for (int k = 0; k < 24; ++k) { std::uint32_t v = 0; (void)cpu.read32(sp + 4u * std::uint32_t(k), v); if ((v & 0xff000000u) == 0x80000000u) { char b[16]; std::snprintf(b, sizeof b, " %08x", v); st += b; } } LOG("     ra=%08x pila:%s\n", cpu.state().gpr[31], st.c_str()); }
                LOG("     mando: %llu bytes, MDEC %llu cuadros %llu macrobloques\n", (unsigned long long)pad.exchanges,
                    (unsigned long long)g_mdec->frames, (unsigned long long)g_mdec->blocks);
                LOG("     fuentes IRQ: vbl=%llu cd=%llu dma=%llu t2=%llu spu=%llu\n", (unsigned long long)bios.srcCount[0], (unsigned long long)bios.srcCount[2], (unsigned long long)bios.srcCount[3], (unsigned long long)bios.srcCount[6], (unsigned long long)bios.srcCount[9]);
                LOG("     en ISR %.0f%%, interrupciones desactivadas %.0f%%\n", 100.0 * inIsr / std::max<std::uint64_t>(1, samples), 100.0 * irqOff / std::max<std::uint64_t>(1, samples));
                samples = inIsr = irqOff = 0;
                std::uint32_t ist{}, imk{}; (void)cpu.read32(0x1f801070, ist); (void)cpu.read32(0x1f801074, imk);
                { std::uint32_t p1{}, p2{}; std::uint16_t en{}, flag{}; (void)cpu.read32(0x8011e9ec, p1); (void)cpu.read32(0x8011e9f0, p2); (void)cpu.read16(0x8011e990, en); (void)cpu.read16(0x8011e960, flag);
                  LOG("     trapIntr: ptrStat=%08x ptrMask=%08x enabled=%04x busy=%04x\n", p1, p2, en, flag); }
                { std::uint32_t b0{}, b1{}; (void)cpu.read32(bios.hook_jmpbuf, b0); (void)cpu.read32(bios.hook_jmpbuf + 4, b1); LOG("     hook buf pc=%08x sp=%08x\n", b0, b1); }
                LOG("     SR=%08x CAUSE=%08x I_STAT=%04x I_MASK=%04x\n", cpu.state().cop0_status, cpu.state().cop0_cause, ist, imk);
            }
        }
    }
    emuDone = true;
    };
#ifdef SF3B_SDL
    if (window) {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
        std::thread emuThread(emulate);
        marcaArranque("empieza el dibujo");
        window->runRenderLoop(emuDone);
        if (std::getenv("SF3N_TRITEXLOG")) window->reportTriTex();
        window->quit = true;
        emuThread.join();
        if (!window->restart_lang_.empty()) {

            const std::string exe = std::filesystem::absolute(argv[0]).string();
            const std::string lang = window->restart_lang_;
            LOG("reinicio: idioma %s\n", lang.c_str());
            if (g_log && g_log != stderr) std::fflush(g_log);
            window.reset();
#ifdef _WIN32
            _putenv_s("SF3N_IDIOMA", lang.c_str());
            _putenv_s("SF3N_SIN_LANZADOR", "1");
            std::string cmd = "\"" + exe + "\" \"" + cuePath + "\"";
            STARTUPINFOA si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
            std::vector<char> buf(cmd.begin(), cmd.end()); buf.push_back(0);
            const std::string dir = std::filesystem::absolute(argv[0]).parent_path().string();
            if (CreateProcessA(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
#else
            setenv("SF3N_IDIOMA", lang.c_str(), 1);
            setenv("SF3N_SIN_LANZADOR", "1", 1);
            if (fork() == 0) { execl(exe.c_str(), exe.c_str(), cuePath.c_str(), (char*)nullptr); _exit(1); }
#endif
            return 0;
        }
    } else
#endif
        emulate();
    if (const char* d = std::getenv("SF3B_SHOTS")) {
        std::string p = std::string(d) + "/ram_final.bin";
        if (FILE* f = std::fopen(p.c_str(), "wb")) { auto r = cpu.ram(); std::fwrite(r.data(), 1, r.size(), f); std::fclose(f); }
    }
    { std::map<std::uint32_t,int> u; for (auto l : media.lbaLog) u[l]++; LOG("--- LBAs distintos (%zu):", u.size()); for (auto& [l, n] : u) LOG(" %u(x%d)", l, n); LOG("\n"); }
    { LOG("--- trapIntr callbacks:"); for (std::uint32_t b = 0; b < 11; ++b) { std::uint32_t f{}; (void)cpu.read32(0x8011e964 + 4 * b, f); LOG(" [%u]=%08x", b, f); }
      std::uint16_t en{}; (void)cpu.read16(0x8011e990, en); LOG(" enabled=%04x\n", en); }
    LOG("--- resumen: %llu instrucciones, %llu campos\n", (unsigned long long)steps, (unsigned long long)fields);
    for (auto& [k, v] : bios.counts) LOG("  %s x%llu\n", k.c_str(), (unsigned long long)v);
    std::vector<std::pair<std::uint64_t, std::uint32_t>> hs;
    for (auto& [a, n] : hot) hs.push_back({n, a});
    std::sort(hs.rbegin(), hs.rend());
    LOG("--- zonas calientes:\n");
    for (std::size_t i = 0; i < std::min<std::size_t>(12, hs.size()); ++i) LOG("  %08x x%llu\n", hs[i].second, (unsigned long long)hs[i].first);
    { std::vector<std::pair<std::uint64_t, std::uint32_t>> v; for (auto& [a, n] : isrHot) v.push_back({n, a}); std::sort(v.rbegin(), v.rend());
      LOG("--- dentro de interrupciones:\n"); for (std::size_t i = 0; i < std::min<std::size_t>(10, v.size()); ++i) LOG("  %08x x%llu\n", v[i].second, (unsigned long long)v[i].first); }
    { std::vector<std::pair<std::uint64_t, std::uint32_t>> v; for (auto& [a, n] : gameHot) v.push_back({n, a}); std::sort(v.rbegin(), v.rend());
      LOG("--- codigo del juego (sin libs):\n"); for (std::size_t i = 0; i < std::min<std::size_t>(16, v.size()); ++i) LOG("  %08x x%llu\n", v[i].second, (unsigned long long)v[i].first); }
    LOG("--- comandos GPU:");
    for (auto& [c, n] : gpu.commands) LOG(" %02x:%llu", c, (unsigned long long)n);
    LOG("\n");
    return 0;
}
