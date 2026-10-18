"""Adds the presenter's real upscaling status for the in-game FPS overlay, and
stops retrying a FidelityFX context that failed to create (WSL SDK tree)."""
SDK = "/root/ht/sdk"


def edit(path, old, new, count=1):
    p = f"{SDK}/{path}"
    s = open(p).read()
    assert s.count(old) == count, (path, old[:60], s.count(old))
    s = s.replace(old, new)
    open(p, "w").write(s)


# --- presenter.h: public status API -----------------------------------------
H = "include/rex/ui/presenter.h"
s = open(f"{SDK}/{H}").read()
if "HtUpscaleStatus" not in s:
    tail = "}  // namespace ui\n}  // namespace rex"
    i = s.rfind(tail)
    assert i >= 0
    api = '''// htrecomp: what the presenter actually did on its latest paint, for the
// in-game FPS overlay. fsr3_ok is true only when an FSR 3.1 dispatch succeeded
// within the last second (otherwise the FSR 1 path was used instead).
struct HtUpscaleStatus {
  enum class Kind : uint32_t { kNone, kNative, kBilinear, kFsr1, kFsr3, kDownscale };
  Kind kind = Kind::kNone;
  bool fsr3_ok = false;
  uint32_t input_width = 0, input_height = 0;
  uint32_t output_width = 0, output_height = 0;
};
HtUpscaleStatus GetHtUpscaleStatus();
void SetHtFsr3DispatchResult(bool ok);

'''
    s = s[:i] + api + s[i:]
    open(f"{SDK}/{H}", "w").write(s)

# --- presenter.cpp: status storage, updated on every paint --------------------
C = "src/ui/presenter.cpp"
s = open(f"{SDK}/{C}").read()
if "g_ht_upscale_kind" not in s:
    anchor = "void Presenter::LogGuestOutputPaintFlowIfChanged(const GuestOutputPaintConfig& config,"
    store = '''namespace {
std::atomic<uint32_t> g_ht_upscale_kind{0};
std::atomic<uint32_t> g_ht_upscale_in{0}, g_ht_upscale_out{0};  // width << 16 | height
std::atomic<int64_t> g_ht_fsr3_ok_ms{0};
int64_t HtNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
}  // namespace

HtUpscaleStatus GetHtUpscaleStatus() {
  HtUpscaleStatus status;
  status.kind = HtUpscaleStatus::Kind(g_ht_upscale_kind.load(std::memory_order_relaxed));
  const uint32_t in = g_ht_upscale_in.load(std::memory_order_relaxed);
  const uint32_t out = g_ht_upscale_out.load(std::memory_order_relaxed);
  status.input_width = in >> 16;
  status.input_height = in & 0xFFFF;
  status.output_width = out >> 16;
  status.output_height = out & 0xFFFF;
  status.fsr3_ok = HtNowMs() - g_ht_fsr3_ok_ms.load(std::memory_order_relaxed) < 1000;
  return status;
}

void SetHtFsr3DispatchResult(bool ok) {
  g_ht_fsr3_ok_ms.store(ok ? HtNowMs() : 0, std::memory_order_relaxed);
}

'''
    assert s.count(anchor) == 1
    s = s.replace(anchor, store + anchor)
    old = "  // Cheap signature check every paint; the text is only built when it changes.\n"
    new = '''  {
    // Status for the FPS overlay, refreshed every paint.
    bool has_easu = false;
    for (size_t i = 0; i < flow.effect_count; ++i) {
#if defined(REX_HAS_FIDELITYFX_SDK) || defined(REX_HAS_SPATIAL_FSR)
      has_easu |= flow.effects[i] == GuestOutputPaintEffect::kFsrEasu;
#endif
    }
    const uint32_t in_w = properties.frontbuffer_width, in_h = properties.frontbuffer_height;
    uint32_t out_w = in_w, out_h = in_h;
    if (flow.effect_count) {
      out_w = flow.effect_output_sizes[flow.effect_count - 1].first;
      out_h = flow.effect_output_sizes[flow.effect_count - 1].second;
    }
    HtUpscaleStatus::Kind kind = HtUpscaleStatus::Kind::kNative;
    if (out_w < in_w || out_h < in_h) {
      kind = HtUpscaleStatus::Kind::kDownscale;
    } else if (out_w > in_w || out_h > in_h) {
      kind = HtUpscaleStatus::Kind::kBilinear;
#if defined(REX_HAS_FIDELITYFX_SDK) || defined(REX_HAS_SPATIAL_FSR)
      if (has_easu) {
        kind = (config.GetEffect() == GuestOutputPaintConfig::Effect::kFsr2 ||
                config.GetEffect() == GuestOutputPaintConfig::Effect::kFsr3)
                   ? HtUpscaleStatus::Kind::kFsr3
                   : HtUpscaleStatus::Kind::kFsr1;
      }
#endif
    }
    g_ht_upscale_kind.store(uint32_t(kind), std::memory_order_relaxed);
    g_ht_upscale_in.store((in_w & 0xFFFF) << 16 | (in_h & 0xFFFF), std::memory_order_relaxed);
    g_ht_upscale_out.store((out_w & 0xFFFF) << 16 | (out_h & 0xFFFF), std::memory_order_relaxed);
  }
''' + old
    assert s.count(old) == 1
    s = s.replace(old, new)
    if "#include <chrono>" not in s:
        s = s.replace("#include <atomic>", "#include <atomic>\n#include <chrono>", 1)
    open(f"{SDK}/{C}", "w").write(s)

# --- vulkan_presenter.cpp: report dispatch results, no per-frame retries -------
V = "src/ui/vulkan/vulkan_presenter.cpp"
s = open(f"{SDK}/{V}").read()
if "SetHtFsr3DispatchResult" not in s:
    old = '''            if (DispatchTemporalUpscaler(draw_command_buffer, source_image, effect_input_width,
                                         effect_input_height, output_image, effect_rect_size.first,
                                         effect_rect_size.second, guest_output_paint_config)) {'''
    new = '''            const bool fsr3_dispatched = DispatchTemporalUpscaler(
                draw_command_buffer, source_image, effect_input_width, effect_input_height,
                output_image, effect_rect_size.first, effect_rect_size.second,
                guest_output_paint_config);
            SetHtFsr3DispatchResult(fsr3_dispatched);
            if (fsr3_dispatched) {'''
    assert s.count(old) == 1
    s = s.replace(old, new)
    old = '''    ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
    if (ffxCreateContext(context, &create_desc.header, nullptr) != FFX_API_RETURN_OK) {
      REXLOG_WARN(
          "VulkanPresenter: Failed to create FidelityFX temporal upscaler "
          "context");
      temporal_upscaler_context_ = nullptr;
      return false;
    }'''
    new = '''    // A context that failed to create for these sizes fails again: do not retry
    // every frame (FSR 1 is used instead) until the sizes change.
    static std::atomic<uint64_t> failed_sizes{0};
    const uint64_t sizes = (uint64_t(render_width) << 48) | (uint64_t(render_height) << 32) |
                           (uint64_t(output_width) << 16) | uint64_t(output_height);
    if (failed_sizes.load(std::memory_order_relaxed) == sizes) {
      return false;
    }
    ffxContext* context = reinterpret_cast<ffxContext*>(&temporal_upscaler_context_);
    if (ffxCreateContext(context, &create_desc.header, nullptr) != FFX_API_RETURN_OK) {
      REXLOG_WARN(
          "[ht-fsr] FSR 3.1 unavailable: failed to create the FidelityFX upscaler "
          "context ({}x{} -> {}x{}); using FSR 1",
          render_width, render_height, output_width, output_height);
      temporal_upscaler_context_ = nullptr;
      failed_sizes.store(sizes, std::memory_order_relaxed);
      return false;
    }'''
    assert s.count(old) == 1
    s = s.replace(old, new)
    open(f"{SDK}/{V}", "w").write(s)
print("ok")
