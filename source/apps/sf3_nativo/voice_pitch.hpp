#pragma once

#include "sf/psx/spu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

class VoicePitch {
 public:
  void setFactor(double f) {
    f = std::clamp(f, 0.5, 2.0);
    if (std::fabs(f - factor_) > 1e-6) { factor_ = f; reset(); }
  }
  double factor() const { return factor_; }

  void reset() {
    in_.clear();
    in_base_ = 0;
    a_nom_ = 0;
    s_prev_ = -1;
    olaLv_.assign(std::size_t(N), 0.f);
    olaRv_.assign(std::size_t(N), 0.f);
    st_.clear();
    rp_ = 0;
    out_.clear();
  }

  void process(std::span<sf::psx::SpuPcmFrame> pcm) {
    if (std::fabs(factor_ - 1.0) < 1e-3) return;
    if (window_.empty()) {
      window_.resize(std::size_t(N));
      for (int i = 0; i < N; ++i) window_[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * i / N));
      olaLv_.assign(std::size_t(N), 0.f);
      olaRv_.assign(std::size_t(N), 0.f);
    }
    for (const auto& f : pcm) in_.push_back({float(f.left), float(f.right)});
    stretch();
    resample();
    for (auto& f : pcm) {
      if (!out_.empty()) {
        const auto s = out_.front();
        out_.pop_front();
        f.left = std::int16_t(std::clamp(std::lround(s[0]), -32768L, 32767L));
        f.right = std::int16_t(std::clamp(std::lround(s[1]), -32768L, 32767L));
      } else {
        f.left = f.right = 0;
      }
    }

    while (out_.size() > std::size_t(4 * N)) out_.pop_front();
  }

 private:
  using S = std::array<float, 2>;
  static constexpr int N = 1536;
  static constexpr int Hs = N / 2;
  static constexpr int D = 320;

  float mono(long i) const { const auto& s = in_[std::size_t(i - in_base_)]; return s[0] + s[1]; }

  void stretch() {
    const double Ha = Hs / factor_;
    for (;;) {
      const long nom = long(std::lround(a_nom_));
      long best = nom;
      if (s_prev_ >= 0) {
        const long lo = std::max<long>(nom - D, in_base_);
        if (nom + D + N > in_base_ + long(in_.size())) return;
        const long tmpl = s_prev_ + Hs;
        double bestc = -1e300;
        for (long s = lo; s <= nom + D; s += 2) {
          double c = 0, e = 1e-9;
          for (int k = 0; k < Hs; k += 2) { const double x = mono(s + k); c += x * mono(tmpl + k); e += x * x; }
          c /= std::sqrt(e);
          if (c > bestc) { bestc = c; best = s; }
        }
      } else {
        if (nom + N > in_base_ + long(in_.size())) return;
      }
      for (int k = 0; k < N; ++k) {
        const auto& x = in_[std::size_t(best + k - in_base_)];
        olaL_(k) += window_[std::size_t(k)] * x[0];
        olaR_(k) += window_[std::size_t(k)] * x[1];
      }
      for (int k = 0; k < Hs; ++k) st_.push_back({olaL_(k), olaR_(k)});

      for (int k = 0; k < N - Hs; ++k) { olaL_(k) = olaL_(k + Hs); olaR_(k) = olaR_(k + Hs); }
      for (int k = N - Hs; k < N; ++k) { olaL_(k) = 0; olaR_(k) = 0; }
      s_prev_ = best;
      a_nom_ += Ha;

      const long keep = std::min<long>(long(std::floor(a_nom_)) - D - 2, s_prev_ + Hs);
      while (in_base_ < keep && !in_.empty()) { in_.pop_front(); ++in_base_; }
    }
  }

  float& olaL_(int k) { if (olaLv_.size() != std::size_t(N)) olaLv_.assign(std::size_t(N), 0.f); return olaLv_[std::size_t(k)]; }
  float& olaR_(int k) { if (olaRv_.size() != std::size_t(N)) olaRv_.assign(std::size_t(N), 0.f); return olaRv_[std::size_t(k)]; }

  void resample() {
    while (rp_ + 1.0 < double(st_.size())) {
      const auto i = std::size_t(rp_);
      const float t = float(rp_ - double(i));
      const auto& a = st_[i];
      const auto& b = st_[i + 1];
      out_.push_back({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t});
      rp_ += factor_;
    }
    const auto drop = std::size_t(rp_);
    for (std::size_t k = 0; k < drop && !st_.empty(); ++k) st_.pop_front();
    rp_ -= double(drop);
  }

  double factor_ = 1.0;
  std::vector<float> window_, olaLv_, olaRv_;
  std::deque<S> in_;
  long in_base_ = 0;
  double a_nom_ = 0;
  long s_prev_ = -1;
  std::deque<S> st_;
  double rp_ = 0;
  std::deque<S> out_;
};
