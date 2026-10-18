from pathlib import Path
p=Path('/root/ht/sdk/include/rex/ui/imgui_drawer.h')
s=p.read_text()
if 'CreateTextureRGBA' not in s:
    key='  ImGuiIO& GetIO();'
    assert key in s
    s=s.replace(key,key+'''

  // A dialog owns its optional artwork texture. Creation is lazy, after the
  // immediate drawer exists, and never repeated per frame.
  std::unique_ptr<ImmediateTexture> CreateTextureRGBA(uint32_t width, uint32_t height,
                                                     const uint8_t* data) {
    return immediate_drawer_ ? immediate_drawer_->CreateTexture(
        width, height, ImmediateTextureFilter::kLinear, false, data) : nullptr;
  }
''',1)
    p.write_text(s)
