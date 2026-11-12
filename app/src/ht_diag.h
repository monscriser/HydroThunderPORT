// Per-track performance telemetry - DIAGNOSTIC BUILD ONLY (CMake -DHT_DIAG=ON).
//
// Purpose: the player races every track in order (base game, then DLC) and this
// records what each track costs, so optimizations can be aimed per track.
//
// Session folder: <settings>/diag/<date-time>/  (Linux: ~/.local/share/
// HydroThunderRecompiled/diag/...). Files:
//   info.txt     machine, OS, driver env, build, config copy
//   tracks.csv   one row per track load (and per restart of the same track)
//   frames.csv   EVERY guest frame: time, frame time, track segment
//   system.csv   1 Hz: CPU/RAM/swap/disk IO/page faults + AMD GPU busy,
//                clocks, VRAM/GTT, temperatures, power, battery (sysfs)
//   threads.csv  1 Hz: CPU% of every busy thread (by name)
//   opens.csv    every file the game opens (time + guest path)
//   profile.csv  CPU sampling profile (perf_event, 1 sample per ms of thread CPU),
//                per track segment and thread, as module+offset (self / inclusive)
//   hitches.csv  for every frame >= 50 ms: what each busy thread was executing
//   (game.log also gets [ht-shader-time] per pipeline compile and, 30/55/85/120 s
//    into each track, one fully traced frame: every draw with its GPU time.)
//   game.log     live mirror of the game log, which carries the 2 s
//                [ht-perf]/[ht-cpu]/[ht-gpu]/[ht-draw]/[ht-paint]/[ht-vram]
//                reports plus [ht-track] markers written here.
// tools/diag/analyze_diag.py turns a session folder into a per-track report.
//
// Track detection: each track opens its own sound bank (Audio/Xbox360/
// <Track>.fsb, DLC Pack01TrackNN.fsb) through the VFS; the SDK hook
// rex::ht_diag::SetFileOpenObserver (tools/wsl/diag/patch_diag_hooks.py) reports
// every guest open. Frame times come from rex::ht_diag::SetSwapObserver, called
// once per guest frame on the GPU command thread (lock-free ring, no I/O there).

#pragma once

#if defined(HT_DIAG_BUILD) && HT_DIAG_BUILD

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <set>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/ht_diag_hooks.h>
#include <rex/logging.h>

#include "ht_library_paths.h"
#include "ht_version.h"

#include <deque>
#include <tuple>
#include <unordered_map>

#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#endif

namespace ht::diag {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct KnownTrack {
  const char* id;    // lower-case sound bank stem
  const char* name;  // shown in reports
};
// Names confirmed by the pt-BR string tables; City / Hydroelectric and the DLC
// banks are reported by their file id (their menu names are not tied to a file).
inline constexpr KnownTrack kTracks[] = {
    {"tropical", "Ilha dos Monstros (Tropical)"},
    {"babylon", "Babilonia Perdida (Babylon)"},
    {"area51", "Area 51 (Area51)"},
    {"asgard", "Invasao de Asgard (Asgard)"},
    {"sewers", "Esgotos de Paris (Sewers)"},
    {"stadium", "Taca Tsunami (Stadium)"},
    {"city", "City"},
    {"hydroelectric", "Hydroelectric"},
};

inline std::string Lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// "Pack01Track02" / "pack1track2" -> DLC track id; "" otherwise. The game opens the DLC
// banks as "Pack01:\Audio\Xbox360Pack01Track03.fsb" (no separator), so the stem may carry
// a "xbox360" prefix (seen on the ROG Ally run, 11/11/2026).
inline std::string DlcTrackName(std::string stem) {
  if (stem.rfind("xbox360", 0) == 0) stem = stem.substr(7);
  const auto p = stem.find("pack");
  const auto t = stem.find("track");
  if (p != 0 || t == std::string::npos || t <= 4 || t + 5 >= stem.size()) return {};
  for (size_t i = 4; i < t; ++i) if (!std::isdigit(static_cast<unsigned char>(stem[i]))) return {};
  for (size_t i = t + 5; i < stem.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(stem[i]))) return {};
  return fmt::format("DLC Pack{} Pista {}", stem.substr(4, t - 4), stem.substr(t + 5));
}

// Settings the diagnostic build forces before the GPU starts.
inline void ConfigureCvars() {
  rex::cvar::SetFlagByName("ht_perf_log", "true");                     // 2 s GPU/CPU reports
  rex::cvar::SetFlagByName("vulkan_trace_shader_compilation", "true");  // [ht-shader-time] per pipeline
}

// Seconds after a track loads at which one full frame is traced (every draw with its
// GPU time, render targets, resolves, barriers, shaders). Several, because the
// first one may still fall on the countdown.
inline constexpr double kTraceAfter[] = {30.0, 55.0, 85.0, 120.0};
inline constexpr double kTraceBoot[] = {90.0};

class Telemetry {
 public:
  static Telemetry& Get() {
    static Telemetry instance;
    return instance;
  }

  void Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) return;
    t0_ = Clock::now();
#if defined(__linux__)
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    mono0_ns_ = int64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
    try {
      const std::time_t now = std::time(nullptr);
      char stamp[32] = {};
      std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
      dir_ = ht::SettingsDirectory() / "diag" / stamp;
      fs::create_directories(dir_);
      Open(frames_, "frames.csv", "t_s,frame_ms,segment\n");
      Open(tracks_, "tracks.csv", "t_s,segment,event,track_id,track_name,guest_path\n");
      Open(opens_, "opens.csv", "t_s,segment,guest_path\n");
      Open(system_, "system.csv",
           "t_s,segment,proc_cpu_pct,sys_cpu_pct,cores,rss_mb,proc_swap_mb,sys_mem_used_mb,"
           "sys_mem_avail_mb,sys_swap_used_mb,majflt_per_s,io_read_mb_s,io_write_mb_s,"
           "cpu_mhz_avg,cpu_mhz_max,gpu_busy_pct,gpu_sclk_mhz,gpu_mclk_mhz,vram_used_mb,"
           "vram_total_mb,gtt_used_mb,gpu_temp_c,cpu_temp_c,gpu_power_w,battery_w,battery_pct,"
           "fan_rpm,threads\n");
      Open(threads_, "threads.csv", "t_s,segment,tid,name,cpu_pct\n");
      Open(hitches_, "hitches.csv",
           "hitch_t_s,segment,frame_ms,thread,module,offset,caller_module,caller_offset,samples,sym\n");
      WriteInfo();
    } catch (const std::exception& e) {
      REXLOG_WARN("[ht-diag] could not create the session folder: {}", e.what());
      return;
    }
    rex::ht_diag::SetSwapObserver(&Telemetry::OnSwapThunk);
    rex::ht_diag::SetFileOpenObserver(&Telemetry::OnOpenThunk);
    REXLOG_WARN("[ht-diag] per-track telemetry ON ({}) -> {}", HT_BUILD_STAMP, dir_.string());
    REXLOG_WARN("[ht-track] t=0.0s segment=0 event=start track=boot name=\"Inicio / menus\"");
    worker_ = std::thread([this] { Run(); });
    worker_.detach();
  }

 private:
  // ---- producer side (game threads) ------------------------------------
  static void OnSwapThunk() { Get().OnSwap(); }
  static void OnOpenThunk(std::string_view path) { Get().OnOpen(path); }

  static constexpr size_t kRing = 1 << 15;  // ~4 min at 120 FPS between drains
  std::array<int64_t, kRing> ring_{};
  std::atomic<uint64_t> ring_head_{0}, ring_tail_{0};
  std::atomic<uint64_t> ring_dropped_{0};

  void OnSwap() {
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0_).count();
    const uint64_t head = ring_head_.load(std::memory_order_relaxed);
    if (head - ring_tail_.load(std::memory_order_acquire) >= kRing) {
      ring_dropped_.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    ring_[head % kRing] = ns;
    ring_head_.store(head + 1, std::memory_order_release);
  }

  void OnOpen(std::string_view path) {
    const double t = Seconds();
    std::string full(path);
    std::lock_guard lock(mutex_);
    pending_opens_.push_back({t, full});
    // File name without folders or extension.
    auto slash = full.find_last_of("\\/:");
    std::string name = Lower(slash == std::string::npos ? full : full.substr(slash + 1));
    if (name.size() < 5 || name.compare(name.size() - 4, 4, ".fsb") != 0) return;
    const std::string stem = name.substr(0, name.size() - 4);
    std::string display;
    for (const auto& k : kTracks) if (stem == k.id) display = k.name;
    if (display.empty()) display = DlcTrackName(stem);
    if (display.empty()) {
      pending_events_.push_back({t, segment_, "fsb", stem, "", full});
      return;
    }
    if (stem == current_track_) {
      // Same bank again: restart / retry of the same track, keep the segment.
      pending_events_.push_back({t, segment_, "reload", stem, display, full});
      REXLOG_WARN("[ht-track] t={:.1f}s segment={} event=reload track={} name=\"{}\"", t, segment_, stem, display);
      return;
    }
    ++segment_;
    current_track_ = stem;
    segment_starts_.push_back({t, segment_});
    pending_events_.push_back({t, segment_, "enter", stem, display, full});
    REXLOG_WARN("[ht-track] t={:.1f}s segment={} event=enter track={} name=\"{}\"", t, segment_, stem, display);
    copy_log_now_ = true;
  }

  // ---- consumer side (telemetry thread) ---------------------------------
  struct OpenRow { double t; std::string path; };
  struct EventRow { double t; int segment; std::string event, id, name, path; };

  void Run() {
    auto next_sample = Clock::now();
    auto next_log = Clock::now();
    int64_t last_frame_ns = -1;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      // Frames.
      std::vector<std::pair<double, int>> starts;
      std::vector<OpenRow> opens;
      std::vector<EventRow> events;
      {
        std::lock_guard lock(mutex_);
        starts = segment_starts_;
        opens.swap(pending_opens_);
        events.swap(pending_events_);
      }
      ScheduleTraces(starts);
#if defined(__linux__)
      ProfDrain(starts);  // before frames: a hitch's samples must already be here
#endif
      const uint64_t head = ring_head_.load(std::memory_order_acquire);
      uint64_t tail = ring_tail_.load(std::memory_order_relaxed);
      std::string buf;
      for (; tail != head; ++tail) {
        const int64_t ns = ring_[tail % kRing];
        if (last_frame_ns >= 0) {
          const double t = double(ns) * 1e-9;
          const double ms = double(ns - last_frame_ns) * 1e-6;
          int seg = 0;
          for (const auto& [ts, s] : starts) if (t >= ts) seg = s;
          buf += fmt::format("{:.4f},{:.3f},{}\n", t, ms, seg);
#if defined(__linux__)
          if (ms >= 50.0) DumpHitch(t, ms, seg);
#endif
        }
        last_frame_ns = ns;
      }
      ring_tail_.store(tail, std::memory_order_release);
      frames_ << buf;
      for (const auto& o : opens) {
        int seg = 0;
        for (const auto& [ts, s] : starts) if (o.t >= ts) seg = s;
        opens_ << fmt::format("{:.3f},{},\"{}\"\n", o.t, seg, Csv(o.path));
      }
      for (const auto& e : events)
        tracks_ << fmt::format("{:.3f},{},{},{},\"{}\",\"{}\"\n", e.t, e.segment, e.event, e.id,
                               Csv(e.name), Csv(e.path));
      const auto now = Clock::now();
      if (now >= next_sample) {
        next_sample = now + std::chrono::seconds(1);
        int seg = 0;
        for (const auto& [ts, s] : starts) seg = s;
        Sample(seg);
        frames_.flush(); opens_.flush(); tracks_.flush(); system_.flush(); threads_.flush(); hitches_.flush();
#if defined(__linux__)
        if (now >= next_profile_ || seg != profile_seg_) {
          next_profile_ = now + std::chrono::seconds(30);
          profile_seg_ = seg;
          WriteProfile();
        }
#endif
      }
      if (now >= next_log || copy_log_now_.exchange(false)) {
        next_log = now + std::chrono::seconds(2);
        MirrorLog();
      }
    }
  }

  // Appends what is new in the game log to <session>/game.log (cheap: only new bytes).
  void MirrorLog() {
    try {
      const fs::path logs = ht::SettingsDirectory() / "logs";
      if (log_source_.empty() || !fs::exists(log_source_)) {
        fs::file_time_type best{};
        for (const auto& e : fs::directory_iterator(logs)) {
          const auto n = e.path().filename().string();
          if (!e.is_regular_file() || n.rfind("hydrothunder", 0) != 0 || e.path().extension() != ".log") continue;
          if (log_source_.empty() || e.last_write_time() > best) { best = e.last_write_time(); log_source_ = e.path(); }
        }
        if (log_source_.empty() && fs::exists(logs / "rex_log.txt")) log_source_ = logs / "rex_log.txt";
        if (log_source_.empty()) return;
        log_offset_ = 0;
        std::ofstream(dir_ / "info.txt", std::ios::app) << "game log source: " << log_source_.string() << "\n";
      }
      std::ifstream in(log_source_, std::ios::binary);
      in.seekg(0, std::ios::end);
      const std::streamoff size = in.tellg();
      if (size <= log_offset_) return;
      in.seekg(log_offset_);
      std::string chunk(size_t(size - log_offset_), '\0');
      in.read(chunk.data(), std::streamsize(chunk.size()));
      std::ofstream out(dir_ / "game.log", std::ios::binary | std::ios::app);
      out.write(chunk.data(), std::streamsize(in.gcount()));
      log_offset_ += in.gcount();
    } catch (...) {
    }
  }

  void Sample(int seg) {
    const double t = Seconds();
#if defined(__linux__)
    const double wall = prev_wall_ > 0 ? t - prev_wall_ : 0.0;
    prev_wall_ = t;
    const long hz = sysconf(_SC_CLK_TCK);
    const long cores = sysconf(_SC_NPROCESSORS_ONLN);
    // Threads (and process total = sum of threads, so exited threads do not skew it).
    std::map<long, std::pair<std::string, uint64_t>> now_threads;
    double proc_ticks_delta = 0.0;
    std::string thread_rows;
    for (const auto& e : fs::directory_iterator("/proc/self/task", fs::directory_options::skip_permission_denied)) {
      const long tid = std::atol(e.path().filename().string().c_str());
      std::string stat = ReadSmall(e.path() / "stat");
      const auto rp = stat.rfind(')');
      if (rp == std::string::npos) continue;
      std::istringstream ss(stat.substr(rp + 2));
      std::string field;
      uint64_t ut = 0, st = 0;
      for (int i = 3; i <= 15 && ss >> field; ++i) {
        if (i == 14) ut = std::strtoull(field.c_str(), nullptr, 10);
        if (i == 15) st = std::strtoull(field.c_str(), nullptr, 10);
      }
      std::string name = ReadSmall(e.path() / "comm");
      while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
      const uint64_t ticks = ut + st;
      now_threads[tid] = {name, ticks};
      auto it = prev_threads_.find(tid);
      if (it != prev_threads_.end() && wall > 0 && ticks >= it->second.second) {
        const double d = double(ticks - it->second.second);
        proc_ticks_delta += d;
        const double pct = 100.0 * d / double(hz) / wall;
        if (pct >= 0.5) thread_rows += fmt::format("{:.3f},{},{},\"{}\",{:.1f}\n", t, seg, tid, Csv(name), pct);
        if (pct >= 2.0 && !profiled_.count(tid)) ProfOpen(tid);
      }
    }
    prev_threads_.swap(now_threads);
    threads_ << thread_rows;
    const double proc_cpu = wall > 0 ? 100.0 * proc_ticks_delta / double(hz) / wall : 0.0;

    // System CPU.
    double sys_cpu = 0.0;
    {
      std::istringstream ss(ReadSmall("/proc/stat"));
      std::string cpu;
      uint64_t v[10] = {};
      ss >> cpu;
      for (auto& x : v) ss >> x;
      uint64_t total = 0;
      for (auto x : v) total += x;
      const uint64_t idle = v[3] + v[4];
      if (prev_cpu_total_ && total > prev_cpu_total_)
        sys_cpu = 100.0 * (1.0 - double(idle - prev_cpu_idle_) / double(total - prev_cpu_total_));
      prev_cpu_total_ = total;
      prev_cpu_idle_ = idle;
    }
    // Memory.
    const auto status = KeyValues(ReadSmall("/proc/self/status"));
    const auto meminfo = KeyValues(ReadSmall("/proc/meminfo"));
    auto kb = [](const std::map<std::string, double>& m, const char* k) {
      auto it = m.find(k);
      return it == m.end() ? -1.0 : it->second / 1024.0;
    };
    const double rss = kb(status, "VmRSS"), pswap = kb(status, "VmSwap");
    const double mtotal = kb(meminfo, "MemTotal"), mavail = kb(meminfo, "MemAvailable");
    const double swap_used = kb(meminfo, "SwapTotal") - kb(meminfo, "SwapFree");
    // Page faults (disk stalls) and IO.
    double majflt_s = 0, io_r = 0, io_w = 0;
    {
      std::string stat = ReadSmall("/proc/self/stat");
      const auto rp = stat.rfind(')');
      if (rp != std::string::npos) {
        std::istringstream ss(stat.substr(rp + 2));
        std::string field;
        uint64_t maj = 0;
        for (int i = 3; i <= 12 && ss >> field; ++i) if (i == 12) maj = std::strtoull(field.c_str(), nullptr, 10);
        if (wall > 0 && maj >= prev_majflt_) majflt_s = double(maj - prev_majflt_) / wall;
        prev_majflt_ = maj;
      }
      const auto io = KeyValues(ReadSmall("/proc/self/io"));
      const double rb = io.count("read_bytes") ? io.at("read_bytes") : 0;
      const double wb = io.count("write_bytes") ? io.at("write_bytes") : 0;
      if (wall > 0 && prev_io_r_ >= 0) { io_r = (rb - prev_io_r_) / 1048576.0 / wall; io_w = (wb - prev_io_w_) / 1048576.0 / wall; }
      prev_io_r_ = rb;
      prev_io_w_ = wb;
    }
    // CPU clocks.
    double mhz_sum = 0, mhz_max = 0;
    int mhz_n = 0;
    for (long c = 0; c < cores; ++c) {
      const std::string v = ReadSmall(fmt::format("/sys/devices/system/cpu/cpu{}/cpufreq/scaling_cur_freq", c));
      if (v.empty()) continue;
      const double m = std::atof(v.c_str()) / 1000.0;
      mhz_sum += m; mhz_max = std::max(mhz_max, m); ++mhz_n;
    }
    // GPU (amdgpu sysfs: Steam Deck, ROG Ally, Radeon desktops).
    if (!gpu_probed_) ProbeSysfs();
    double busy = -1, sclk = -1, mclk = -1, vram = -1, vram_total = -1, gtt = -1, gtemp = -1, gpow = -1;
    if (!gpu_dir_.empty()) {
      busy = Num(gpu_dir_ / "gpu_busy_percent");
      sclk = DpmCurrent(gpu_dir_ / "pp_dpm_sclk");
      mclk = DpmCurrent(gpu_dir_ / "pp_dpm_mclk");
      vram = Num(gpu_dir_ / "mem_info_vram_used") / 1048576.0;
      vram_total = Num(gpu_dir_ / "mem_info_vram_total") / 1048576.0;
      gtt = Num(gpu_dir_ / "mem_info_gtt_used") / 1048576.0;
    }
    if (!gpu_hwmon_.empty()) {
      gtemp = Num(gpu_hwmon_ / "temp1_input") / 1000.0;
      double p = Num(gpu_hwmon_ / "power1_average");
      if (p < 0) p = Num(gpu_hwmon_ / "power1_input");
      gpow = p >= 0 ? p / 1e6 : -1;
      if (sclk < 0) { const double f = Num(gpu_hwmon_ / "freq1_input"); sclk = f >= 0 ? f / 1e6 : -1; }
    }
    const double ctemp = cpu_hwmon_.empty() ? -1 : Num(cpu_hwmon_ / "temp1_input") / 1000.0;
    double fan = -1;
    for (const auto& f : fan_inputs_) { const double v = Num(f); if (v > fan) fan = v; }
    double bat_w = -1, bat_pct = -1;
    if (!battery_.empty()) {
      double p = Num(battery_ / "power_now");
      if (p < 0) {
        const double i = Num(battery_ / "current_now"), v = Num(battery_ / "voltage_now");
        if (i >= 0 && v >= 0) p = i * v / 1e6;
      }
      bat_w = p >= 0 ? p / 1e6 : -1;
      bat_pct = Num(battery_ / "capacity");
    }
    system_ << fmt::format(
        "{:.3f},{},{:.1f},{:.1f},{},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.1f},{:.2f},{:.2f},{:.0f},{:.0f},"
        "{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.1f},{:.1f},{:.2f},{:.2f},{:.0f},{:.0f},{}\n",
        t, seg, proc_cpu, sys_cpu, cores, rss, pswap, mtotal - mavail, mavail, swap_used, majflt_s, io_r,
        io_w, mhz_n ? mhz_sum / mhz_n : -1.0, mhz_n ? mhz_max : -1.0, busy, sclk, mclk, vram, vram_total,
        gtt, gtemp, ctemp, gpow, bat_w, bat_pct, fan, prev_threads_.size());
#else
    (void)seg;
    (void)t;
#endif
    const uint64_t dropped = ring_dropped_.exchange(0);
    if (dropped) REXLOG_WARN("[ht-diag] {} frame timestamps dropped (telemetry thread late)", dropped);
  }

  // One traced frame at fixed times after each track load (and once in the menus).
  void ScheduleTraces(const std::vector<std::pair<double, int>>& starts) {
    if (starts.empty()) return;
    const auto [start, seg] = starts.back();
    const double since = Seconds() - start;
    const double* times = seg == 0 ? kTraceBoot : kTraceAfter;
    const size_t n = seg == 0 ? std::size(kTraceBoot) : std::size(kTraceAfter);
    for (size_t k = 0; k < n; ++k) {
      if (since < times[k] || since > times[k] + 5.0) continue;
      const int key = seg * 16 + int(k);
      if (!traced_.insert(key).second) continue;
      rex::ht_diag::RequestTraceFrame();
      REXLOG_WARN("[ht-diag] trace requested segment={} k={} at {:.0f}s into the segment", seg, k, since);
      break;
    }
  }

#if defined(__linux__)
  // ---- CPU sampling profiler (perf_event, no root needed with perf_event_paranoid<=2) ----
  // Each busy thread (>=2% CPU) gets a task-clock sampling event: one sample per 1 ms of
  // that thread's CPU time, user space only, with the user call chain when the code has
  // frame pointers. Aggregated per track segment and thread name; addresses are stored as
  // module + offset (symbolized offline against the exact binaries of this build).
  static constexpr size_t kProfPages = 64;  // data pages per thread (power of two)
  struct Prof { long tid; int fd; void* base; size_t size; };
  struct RawSample { double t; long tid; uint64_t ip, caller; };
  struct AddrKey {
    int seg; int thread; uint64_t addr;
    bool operator==(const AddrKey& o) const { return seg == o.seg && thread == o.thread && addr == o.addr; }
  };
  struct AddrKeyHash {
    size_t operator()(const AddrKey& k) const {
      return std::hash<uint64_t>()(k.addr * 1315423911u ^ (uint64_t(k.seg) << 40) ^ (uint64_t(k.thread) << 20));
    }
  };

  void ProfOpen(long tid) {
    profiled_.insert(tid);
    if (prof_disabled_ || prof_.size() >= 24) return;
    perf_event_attr a{};
    a.size = sizeof(a);
    a.type = PERF_TYPE_SOFTWARE;
    a.config = PERF_COUNT_SW_TASK_CLOCK;
    a.sample_period = 1000000;  // 1 ms of the thread's CPU time
    a.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_TID | PERF_SAMPLE_TIME | PERF_SAMPLE_CALLCHAIN;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    a.exclude_callchain_kernel = 1;
    a.sample_max_stack = 8;
    a.use_clockid = 1;
    a.clockid = CLOCK_MONOTONIC;
    int fd = int(syscall(SYS_perf_event_open, &a, pid_t(tid), -1, -1, PERF_FLAG_FD_CLOEXEC));
    if (fd < 0 && errno == EINVAL) {  // older kernels: no sample_max_stack
      a.sample_max_stack = 0;
      fd = int(syscall(SYS_perf_event_open, &a, pid_t(tid), -1, -1, PERF_FLAG_FD_CLOEXEC));
    }
    if (fd < 0) {
      const int err = errno;
      if (!prof_status_written_) {
        prof_status_written_ = true;
        std::ofstream(dir_ / "info.txt", std::ios::app)
            << "cpu profiler: perf_event_open FAILED errno=" << err << " (" << std::strerror(err)
            << ") perf_event_paranoid=" << ReadSmall("/proc/sys/kernel/perf_event_paranoid");
      }
      if (err == EACCES || err == EPERM || err == ENOSYS || err == ENOENT) prof_disabled_ = true;
      return;
    }
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const size_t size = (kProfPages + 1) * page;
    void* base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
      close(fd);
      return;
    }
    prof_.push_back({tid, fd, base, size});
    if (!prof_status_written_) {
      prof_status_written_ = true;
      std::ofstream(dir_ / "info.txt", std::ios::app)
          << "cpu profiler: perf_event OK (task-clock, 1 sample per ms of thread CPU), perf_event_paranoid="
          << ReadSmall("/proc/sys/kernel/perf_event_paranoid");
    }
  }

  // "name#tid": many threads share the process name, and the game thread must stay apart.
  std::string ThreadLabel(long tid) const {
    auto it = prev_threads_.find(tid);
    return fmt::format("{}#{}", it == prev_threads_.end() ? std::string("?") : it->second.first, tid);
  }
  // Name of the nearest exported symbol, for system libraries (libc, Vulkan driver) that
  // cannot be symbolized offline. Our own binaries are symbolized offline with nm.
  std::string DlName(uint64_t a, const std::string& module) {
    if (!a || module == "hydrothunder" || module == "librexruntime.so" || module == "librexgpu-xenos.so") return {};
    auto c = dlname_cache_.find(a);
    if (c != dlname_cache_.end()) return c->second;
    std::string out;
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(a), &info) && info.dli_sname) out = info.dli_sname;
    if (dlname_cache_.size() < 200000) dlname_cache_[a] = out;
    return out;
  }

  int ThreadIndex(long tid) {
    const std::string name = ThreadLabel(tid);
    auto f = thread_index_.find(name);
    if (f != thread_index_.end()) return f->second;
    const int idx = int(thread_names_.size());
    thread_names_.push_back(name);
    thread_index_[name] = idx;
    return idx;
  }

  void ProfDrain(const std::vector<std::pair<double, int>>& starts) {
    if (prof_.empty()) return;
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const uint64_t dsize = kProfPages * page;
    std::vector<uint8_t> rec;
    double newest = 0;
    for (auto& p : prof_) {
      auto* meta = static_cast<perf_event_mmap_page*>(p.base);
      const uint8_t* data = static_cast<const uint8_t*>(p.base) + page;
      const uint64_t head = __atomic_load_n(&meta->data_head, __ATOMIC_ACQUIRE);
      uint64_t tail = meta->data_tail;
      auto copy = [&](uint64_t off, void* dst, size_t n) {
        uint8_t* d = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < n; ++i) d[i] = data[(off + i) % dsize];
      };
      while (tail + sizeof(perf_event_header) <= head) {
        perf_event_header h{};
        copy(tail, &h, sizeof(h));
        if (h.size < sizeof(h) || tail + h.size > head) break;
        rec.resize(h.size);
        copy(tail, rec.data(), h.size);
        tail += h.size;
        if (h.type == PERF_RECORD_LOST && h.size >= 24) {
          uint64_t lost = 0;
          std::memcpy(&lost, rec.data() + 16, 8);
          prof_lost_ += lost;
          continue;
        }
        if (h.type != PERF_RECORD_SAMPLE || h.size < 8 + 32) continue;
        const uint8_t* r = rec.data() + 8;
        uint64_t ip = 0, time = 0, nr = 0;
        uint32_t pidtid[2] = {};
        std::memcpy(&ip, r, 8);
        std::memcpy(pidtid, r + 8, 8);
        std::memcpy(&time, r + 16, 8);
        std::memcpy(&nr, r + 24, 8);
        const double t = double(int64_t(time) - mono0_ns_) * 1e-9;
        newest = std::max(newest, t);
        int seg = 0;
        for (const auto& [ts, s] : starts) if (t >= ts) seg = s;
        const int th = ThreadIndex(long(pidtid[1]));
        ++prof_samples_;
        ++self_[{seg, th, ip}];
        // Call chain (user part): inclusive counts for each distinct frame in the
        // executable mappings (garbage from code without frame pointers is dropped).
        uint64_t caller = 0;
        uint64_t seen[10] = {};
        int nseen = 0;
        const size_t avail = (h.size - 8 - 32) / 8;
        for (uint64_t i = 0; i < nr && i < avail && i < 10; ++i) {
          uint64_t a = 0;
          std::memcpy(&a, r + 32 + 8 * i, 8);
          if (a >= uint64_t(-4095) || a == 0) continue;  // PERF_CONTEXT_* markers
          if (!InExecutable(a)) continue;
          bool dup = false;
          for (int j = 0; j < nseen; ++j) dup |= seen[j] == a;
          if (dup) continue;
          seen[nseen++] = a;
          if (a != ip && !caller) caller = a;
          if (incl_.size() < 3000000) ++incl_[{seg, th, a}];
        }
        recent_.push_back({t, long(pidtid[1]), ip, caller});
      }
      __atomic_store_n(&meta->data_tail, tail, __ATOMIC_RELEASE);
    }
    while (!recent_.empty() && recent_.front().t < newest - 4.0) recent_.pop_front();
  }

  // Executable mappings, refreshed when an address is outside all of them (new library).
  struct Mapping { uint64_t start, end, base; std::string module; bool exec; };
  void LoadMaps() {
    maps_.clear();
    std::istringstream ss(ReadSmall("/proc/self/maps"));
    std::string line;
    std::map<std::string, uint64_t> base;
    std::vector<Mapping> all;
    while (std::getline(ss, line)) {
      const auto dash = line.find('-'), sp = line.find(' ');
      if (dash == std::string::npos || sp == std::string::npos) continue;
      const uint64_t s = std::strtoull(line.substr(0, dash).c_str(), nullptr, 16);
      const uint64_t e = std::strtoull(line.substr(dash + 1, sp - dash - 1).c_str(), nullptr, 16);
      const bool x = line.size() > sp + 3 && line[sp + 3] == 'x';
      const auto slash = line.find('/');
      std::string path = slash == std::string::npos ? std::string() : line.substr(slash);
      if (path.empty()) {
        const auto br = line.find('[');
        if (br != std::string::npos) path = line.substr(br);
      }
      if (!path.empty()) {
        auto it = base.find(path);
        if (it == base.end() || s < it->second) base[path] = s;
      }
      all.push_back({s, e, 0, path, x});
    }
    for (auto& m : all) {
      if (!m.exec) continue;
      m.base = m.module.empty() ? m.start : base[m.module];
      const auto sl = m.module.find_last_of('/');
      if (sl != std::string::npos) m.module = m.module.substr(sl + 1);
      if (m.module.empty()) m.module = "[anon]";
      maps_.push_back(m);
    }
    std::sort(maps_.begin(), maps_.end(), [](const Mapping& a, const Mapping& b) { return a.start < b.start; });
    maps_loaded_ = Clock::now();
  }
  const Mapping* FindMap(uint64_t a) {
    auto it = std::upper_bound(maps_.begin(), maps_.end(), a, [](uint64_t v, const Mapping& m) { return v < m.start; });
    if (it == maps_.begin()) return nullptr;
    --it;
    return a < it->end ? &*it : nullptr;
  }
  bool InExecutable(uint64_t a) {
    if (maps_.empty()) LoadMaps();
    if (FindMap(a)) return true;
    if (Clock::now() - maps_loaded_ > std::chrono::seconds(5)) {
      LoadMaps();
      return FindMap(a) != nullptr;
    }
    return false;
  }
  std::pair<std::string, uint64_t> Symbolic(uint64_t a) {
    if (!a) return {"", 0};
    if (maps_.empty()) LoadMaps();
    const Mapping* m = FindMap(a);
    if (!m) { LoadMaps(); m = FindMap(a); }
    return m ? std::make_pair(m->module, a - m->base) : std::make_pair(std::string("?"), a);
  }

  // What every profiled thread was doing during a slow frame (>= 50 ms).
  void DumpHitch(double t_end, double ms, int seg) {
    if (hitch_count_[seg] >= 60) return;
    ++hitch_count_[seg];
    const double from = t_end - ms / 1000.0 - 0.05, to = t_end + 0.02;
    // Aggregated: samples per (thread, instruction, caller) inside the slow frame.
    std::map<std::tuple<long, uint64_t, uint64_t>, int> agg;
    for (const auto& s : recent_)
      if (s.t >= from && s.t <= to) ++agg[{s.tid, s.ip, s.caller}];
    std::vector<std::pair<int, std::tuple<long, uint64_t, uint64_t>>> rows;
    for (const auto& [k, n] : agg) rows.push_back({n, k});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (rows.size() > 300) rows.resize(300);
    std::string out;
    for (const auto& [n, k] : rows) {
      const auto [tid, ip, caller] = k;
      const auto [mod, off] = Symbolic(ip);
      const auto [cmod, coff] = Symbolic(caller);
      const std::string sym = DlName(ip, mod);
      out += fmt::format("{:.4f},{},{:.1f},\"{}\",{},{:x},{},{:x},{},\"{}\"\n", t_end, seg, ms,
                         Csv(ThreadLabel(tid)), mod, off, cmod, coff, n, Csv(sym));
    }
    if (rows.empty()) out = fmt::format("{:.4f},{},{:.1f},,(no samples),0,,0,0\n", t_end, seg, ms);
    hitches_ << out;
  }

  // Cumulative profile (rewritten each time): self = samples where the address was the
  // current instruction; incl = samples where it was anywhere in the (valid) call chain.
  void WriteProfile() {
    if (self_.empty()) return;
    try {
      LoadMaps();
      const fs::path tmp = dir_ / "profile.csv.tmp";
      {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << "segment,thread,kind,module,offset,samples,sym\n";
        auto dump = [&](const auto& m, const char* kind, uint32_t min) {
          for (const auto& [k, n] : m) {
            if (n < min) continue;
            const auto [mod, off] = Symbolic(k.addr);
            f << k.seg << ",\"" << Csv(thread_names_[size_t(k.thread)]) << "\"," << kind << "," << mod << ","
              << fmt::format("{:x}", off) << "," << n << ",\"" << Csv(DlName(k.addr, mod)) << "\"\n";
          }
        };
        dump(self_, "self", 3);
        dump(incl_, "incl", 8);
        f << "-1,\"(total)\",meta,samples," << prof_samples_ << ",lost=" << prof_lost_ << ",\n";
      }
      fs::rename(tmp, dir_ / "profile.csv");
    } catch (...) {
    }
  }

  void ProbeSysfs() {
    gpu_probed_ = true;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
      const auto n = e.path().filename().string();
      if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos) continue;
      const fs::path dev = e.path() / "device";
      if (ReadSmall(dev / "vendor").rfind("0x1002", 0) != 0) continue;
      if (!fs::exists(dev / "gpu_busy_percent")) continue;
      gpu_dir_ = dev;
      for (const auto& h : fs::directory_iterator(dev / "hwmon", ec)) { gpu_hwmon_ = h.path(); break; }
      break;
    }
    for (const auto& h : fs::directory_iterator("/sys/class/hwmon", ec)) {
      std::string name = ReadSmall(h.path() / "name");
      while (!name.empty() && name.back() == '\n') name.pop_back();
      if (cpu_hwmon_.empty() && (name == "k10temp" || name == "zenpower" || name == "coretemp")) cpu_hwmon_ = h.path();
      if (fs::exists(h.path() / "fan1_input")) fan_inputs_.push_back(h.path() / "fan1_input");
      hwmon_names_ += name + " ";
    }
    for (const auto& b : fs::directory_iterator("/sys/class/power_supply", ec)) {
      if (ReadSmall(b.path() / "type").rfind("Battery", 0) == 0) { battery_ = b.path(); break; }
    }
    std::ofstream info(dir_ / "info.txt", std::ios::app);
    info << "sysfs gpu: " << (gpu_dir_.empty() ? "(no amdgpu sysfs; VRAM only from [ht-vram])" : gpu_dir_.string())
         << "\nsysfs gpu hwmon: " << gpu_hwmon_.string() << "\ncpu hwmon: " << cpu_hwmon_.string()
         << "\nhwmon names: " << hwmon_names_ << "\nbattery: " << battery_.string() << "\n";
  }
  static double DpmCurrent(const fs::path& p) {
    std::istringstream ss(ReadSmall(p));
    std::string line;
    while (std::getline(ss, line)) {
      if (line.find('*') == std::string::npos) continue;
      const auto colon = line.find(':');
      return colon == std::string::npos ? -1 : std::atof(line.c_str() + colon + 1);
    }
    return -1;
  }
  static std::map<std::string, double> KeyValues(const std::string& text) {
    std::map<std::string, double> m;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
      const auto c = line.find(':');
      if (c == std::string::npos) continue;
      m[line.substr(0, c)] = std::atof(line.c_str() + c + 1);
    }
    return m;
  }
#endif
  static double Num(const fs::path& p) {
    const std::string v = ReadSmall(p);
    return v.empty() ? -1.0 : std::atof(v.c_str());
  }
  static std::string ReadSmall(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), {});
    return s;
  }
  static std::string Csv(std::string s) {
    std::replace(s.begin(), s.end(), '"', '\'');
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
  }
  double Seconds() const { return std::chrono::duration<double>(Clock::now() - t0_).count(); }
  void Open(std::ofstream& f, const char* name, const char* header) {
    f.open(dir_ / name, std::ios::binary | std::ios::trunc);
    f << header;
  }

  void WriteInfo() {
    std::ofstream info(dir_ / "info.txt", std::ios::binary);
    const std::time_t now = std::time(nullptr);
    char when[64] = {};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    info << "Hydro Thunder Hurricane: Recompiled - diagnostic telemetry\n"
         << "build: " << HT_BUILD_STAMP << "\nstarted: " << when << "\nsession: " << dir_.string() << "\n";
#if defined(__linux__)
    utsname u{};
    if (uname(&u) == 0) info << "kernel: " << u.sysname << " " << u.release << " " << u.version << " " << u.machine << "\n";
    {
      std::istringstream os(ReadSmall("/etc/os-release"));
      std::string line;
      while (std::getline(os, line))
        if (line.rfind("PRETTY_NAME=", 0) == 0 || line.rfind("VERSION_ID=", 0) == 0 || line.rfind("ID=", 0) == 0)
          info << "os " << line << "\n";
    }
    {
      std::istringstream cpu(ReadSmall("/proc/cpuinfo"));
      std::string line;
      while (std::getline(cpu, line)) if (line.rfind("model name", 0) == 0) { info << "cpu " << line << "\n"; break; }
      info << "logical cpus: " << sysconf(_SC_NPROCESSORS_ONLN) << "\n";
    }
    {
      std::istringstream mem(ReadSmall("/proc/meminfo"));
      std::string line;
      while (std::getline(mem, line))
        if (line.rfind("MemTotal", 0) == 0 || line.rfind("SwapTotal", 0) == 0) info << line << "\n";
    }
    for (const char* k : {"SteamDeck", "SteamOS", "SteamGamepadUI", "XDG_SESSION_TYPE", "XDG_CURRENT_DESKTOP",
                          "WAYLAND_DISPLAY", "DISPLAY", "GAMESCOPE_WAYLAND_DISPLAY", "SDL_VIDEO_DRIVER",
                          "SDL_VIDEODRIVER", "ENABLE_GAMESCOPE_WSI", "MESA_VK_WSI_PRESENT_MODE", "RADV_DEBUG",
                          "RADV_PERFTEST", "AMD_VULKAN_ICD", "VK_ICD_FILENAMES", "VK_INSTANCE_LAYERS",
                          "ENABLE_VULKAN_RENDERDOC_CAPTURE", "MANGOHUD", "DXVK_HUD", "LD_PRELOAD",
                          "APPIMAGE", "STEAM_COMPAT_APP_ID", "SteamAppId"})
      if (const char* v = std::getenv(k)) info << "env " << k << "=" << v << "\n";
    // Anything Vulkan/RenderDoc already mapped into the process at start.
    {
      std::istringstream maps(ReadSmall("/proc/self/maps"));
      std::string line, seen;
      while (std::getline(maps, line)) {
        const auto s = line.find('/');
        if (s == std::string::npos) continue;
        const std::string lib = line.substr(s);
        const std::string l = Lower(lib);
        if ((l.find("renderdoc") != std::string::npos || l.find("vklayer") != std::string::npos ||
             l.find("mangohud") != std::string::npos || l.find("libvulkan") != std::string::npos) &&
            seen.find(lib) == std::string::npos) {
          seen += lib + "\n";
          info << "mapped " << lib << "\n";
        }
      }
    }
#endif
    // The player's settings at the time of the run.
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(ht::SettingsDirectory(), ec)) {
      if (e.path().extension() == ".toml") {
        info << "\n===== " << e.path().filename().string() << " =====\n" << ReadSmall(e.path());
      }
    }
    info << "\n";
  }

  std::atomic<bool> started_{false};
  std::atomic<bool> copy_log_now_{false};
  Clock::time_point t0_{};
  fs::path dir_;
  std::ofstream frames_, tracks_, opens_, system_, threads_;
  std::thread worker_;

  std::mutex mutex_;
  int segment_ = 0;
  std::string current_track_ = "boot";
  std::vector<std::pair<double, int>> segment_starts_{{0.0, 0}};
  std::vector<OpenRow> pending_opens_;
  std::vector<EventRow> pending_events_;

  fs::path log_source_;
  std::streamoff log_offset_ = 0;

#if defined(__linux__)
  double prev_wall_ = 0;
  std::map<long, std::pair<std::string, uint64_t>> prev_threads_;
  uint64_t prev_cpu_total_ = 0, prev_cpu_idle_ = 0, prev_majflt_ = 0;
  double prev_io_r_ = -1, prev_io_w_ = -1;
  bool gpu_probed_ = false;
  fs::path gpu_dir_, gpu_hwmon_, cpu_hwmon_, battery_;
  std::vector<fs::path> fan_inputs_;
  std::string hwmon_names_;
  // CPU profiler.
  int64_t mono0_ns_ = 0;
  std::vector<Prof> prof_;
  std::set<long> profiled_;
  bool prof_disabled_ = false, prof_status_written_ = false;
  uint64_t prof_samples_ = 0, prof_lost_ = 0;
  std::unordered_map<AddrKey, uint32_t, AddrKeyHash> self_, incl_;
  std::vector<std::string> thread_names_;
  std::map<std::string, int> thread_index_;
  std::deque<RawSample> recent_;
  std::vector<Mapping> maps_;
  Clock::time_point maps_loaded_{};
  std::map<int, int> hitch_count_;
  std::unordered_map<uint64_t, std::string> dlname_cache_;
  Clock::time_point next_profile_{};
  int profile_seg_ = 0;
#endif
  std::set<int> traced_;
  std::ofstream hitches_;
};

}  // namespace ht::diag

#endif  // HT_DIAG_BUILD
