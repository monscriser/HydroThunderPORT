// Sharp launcher text at any window size.
//
// The SDK's ImGui drawer is a "legacy" renderer for ImGui 1.92: it uploads the font atlas
// once (GetTexDataAsRGBA32) and does not set ImGuiBackendFlags_RendererHasTextures. ImGui
// then locks the atlas and draws every other text size by stretching the closest size that
// was baked at start-up (18 px), which blurs big text and the whole UI once it is scaled
// with the window.
//
// The launcher scales its fonts with the window (see UiScale in ht_setup_wizard.cpp), so
// this bakes the exact pixel sizes it is about to use - at the monitor's DPI density - and
// re-uploads the atlas as a texture of its own. It runs only when a needed size is missing
// (first frame, a finished window resize, a move to a monitor with another DPI), never per
// frame. If anything is unavailable it does nothing and ImGui keeps stretching, as before.
#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <rex/ui/imgui_drawer.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace ht::launcher_fonts {

class SharpFonts {
 public:
  // Call at the start of the dialog's frame, before any window is begun.
  // sizes: final pixel sizes in ImGui (logical) units; density: monitor scale (1 = 96 DPI).
  void Ensure(rex::ui::ImGuiDrawer* drawer, const std::vector<float>& sizes, float density) {
    ImGuiIO& io = ImGui::GetIO();
    if (!drawer || (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures)) return;
    ImFontAtlas* atlas = io.Fonts;
    ImFont* font = io.FontDefault ? io.FontDefault : ImGui::GetDefaultFont();
    if (!atlas || !font || !atlas->Builder || !atlas->TexData || font->OwnerAtlas != atlas) return;
    density = std::clamp(std::round(density * 4.0f) / 4.0f, 1.0f, 3.0f);

    std::vector<float> missing;
    for (float size : sizes) {
      const int px = int(std::lround(size));
      if (px < 6 || px > 160) continue;
      if (baked_.insert({px, int(density * 4.0f)}).second) missing.push_back(float(px));
    }
    if (!missing.empty()) {
      ImFontAtlasBuilder* builder = atlas->Builder;
      // A texture grow may repack and drop sizes not used in the last frames: mark every
      // existing size as used now, so the start-up size and earlier bakes survive.
      for (int i = 0; i < builder->BakedPool.Size; ++i) builder->BakedPool[i].LastUsedFrame = builder->FrameCount;
      const bool was_locked = atlas->Locked;
      atlas->Locked = false;  // legacy renderer: ImGui re-locks it on the next NewFrame anyway
      for (float px : missing) {
        ImFontBaked* baked = font->GetFontBaked(px, density);
        if (!baked) continue;
        // Same glyphs ImGui preloads for legacy renderers (all configured ranges).
        if (font->FallbackChar) baked->FindGlyph(font->FallbackChar);
        if (font->EllipsisChar) baked->FindGlyph(font->EllipsisChar);
        for (ImFontConfig* src : font->Sources) {
          const ImWchar* ranges = src->GlyphRanges ? src->GlyphRanges : atlas->GetGlyphRangesDefault();
          for (; ranges[0]; ranges += 2)
            for (unsigned c = ranges[0]; c <= ranges[1] && c <= IM_UNICODE_CODEPOINT_MAX; ++c) baked->FindGlyph(ImWchar(c));
        }
      }
      atlas->Locked = was_locked;
      // Later lookups (UpdateCurrentFontSize) prefer sizes baked at this density.
      font->CurrentRasterizerDensity = density;
      font->LastBaked = nullptr;
      dirty_ = true;
    }
    if (atlas->TexData != uploaded_) dirty_ = true;
    // ImGui clears TexIsBuilt whenever glyphs are added ("for legacy backends"); with it false
    // NewFrame reports "font atlas is not built" every frame (seen ~400 log lines/s on Linux).
    // Re-upload so the texture also gets glyphs that were added lazily.
    if (!atlas->TexIsBuilt) dirty_ = true;
    if (dirty_) Upload(drawer, atlas);
  }

 private:
  void Upload(rex::ui::ImGuiDrawer* drawer, ImFontAtlas* atlas) {
    ImTextureData* tex = atlas->TexData;
    if (!tex || !tex->Pixels || tex->Width <= 0 || tex->Height <= 0) return;
    std::vector<unsigned char> rgba;
    const unsigned char* pixels = static_cast<const unsigned char*>(tex->Pixels);
    if (tex->Format == ImTextureFormat_Alpha8) {
      rgba.resize(size_t(tex->Width) * tex->Height * 4);
      for (size_t i = 0, n = size_t(tex->Width) * tex->Height; i < n; ++i) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = pixels[i];
      }
      pixels = rgba.data();
    } else if (tex->Format != ImTextureFormat_RGBA32) {
      return;
    }
    auto texture = drawer->CreateTextureRGBA(uint32_t(tex->Width), uint32_t(tex->Height), pixels);
    if (!texture) return;  // no immediate drawer yet: try again next frame
    // An older atlas texture that ImGui replaced (grow) can now be freed by ImGui.
    if (uploaded_ && uploaded_ != tex) {
      bool alive = false;
      for (ImTextureData* t : atlas->TexList) alive |= t == uploaded_;
      if (alive) uploaded_->SetTexID(ImTextureID_Invalid);
    }
    tex->SetTexID(reinterpret_cast<ImTextureID>(texture.get()));
    tex->Updates.resize(0);
    tex->SetStatus(ImTextureStatus_OK);
    atlas->TexRef = tex->GetTexRef();
    atlas->TexIsBuilt = true;  // the uploaded texture matches the atlas now
    // Draw lists of this frame may still point at the previous texture: keep it one more frame.
    previous_ = std::move(texture_);
    texture_ = std::move(texture);
    uploaded_ = tex;
    dirty_ = false;
  }

  std::set<std::pair<int, int>> baked_;
  std::unique_ptr<rex::ui::ImmediateTexture> texture_, previous_;
  ImTextureData* uploaded_ = nullptr;
  bool dirty_ = false;
};

}  // namespace ht::launcher_fonts
