"""1.1 BETA: four local player profiles (split screen) with names the launcher can set.

- UserProfile gets set_name() and a fixed storage_name(): the per-profile settings folder
  (content/<title>/profile/<name>) keeps using the original "Player N", so renaming a player
  never moves anything. Saves are filed by XUID and never depended on the name.
- KernelState gets profiles 3 and 4 (XUIDs ...BEC0 / ...BEC1), next to the existing 1 and 2.
  New members go at the end of the class so existing member offsets do not move.
- ContentManager / XamUserReadProfileSettings look at all four profiles.
Idempotent: refuses to run twice."""
import sys
from pathlib import Path
sdk = Path(sys.argv[1] if len(sys.argv) > 1 else '/root/ht/sdk')

def sub(rel, old, new, count=1):
    p = sdk / rel
    t = p.read_text()
    if t.count(old) != count:
        raise SystemExit(f'{rel}: expected {count}x {old[:60]!r}, found {t.count(old)}')
    p.write_text(t.replace(old, new))

h = 'include/rex/system/xam/user_profile.h'
if 'storage_name_' in (sdk / h).read_text():
    raise SystemExit('already applied')
sub(h, '  std::string name() const { return name_; }\n',
    '  std::string name() const { return name_; }\n'
    '  // [HT] Display name (gamertag) can change; storage_name() is the original one and names\n'
    '  // the per-profile settings folder, so renaming never moves data.\n'
    '  void set_name(std::string name) { name_ = std::move(name); }\n'
    '  const std::string& storage_name() const { return storage_name_; }\n')
sub(h, '  KernelState* kernel_state_ = nullptr;\n\n  void LoadSetting',
    '  KernelState* kernel_state_ = nullptr;\n  std::string storage_name_;\n\n  void LoadSetting')
sub('src/system/xam/user_profile.cpp', '  name_ = std::move(name);\n',
    '  name_ = std::move(name);\n  storage_name_ = name_;\n')

k = 'include/rex/system/kernel_state.h'
sub(k, '    return index == 0 ? user_profile_.get() : index == 1 ? user_profile2_.get() : nullptr;\n',
    '    // [HT] Four local players (split screen).\n'
    '    return index == 0 ? user_profile_.get() : index == 1 ? user_profile2_.get()\n'
    '         : index == 2 ? ht_user_profile3_.get() : index == 3 ? ht_user_profile4_.get() : nullptr;\n')
sub(k, '  friend class XObject;\n};\n\n// Global kernel state accessor',
    '  friend class XObject;\n\n  // [HT] Players 3 and 4 (kept last so earlier member offsets are unchanged).\n'
    '  std::unique_ptr<xam::UserProfile> ht_user_profile3_;\n'
    '  std::unique_ptr<xam::UserProfile> ht_user_profile4_;\n};\n\n// Global kernel state accessor')

sub('src/system/kernel_state.cpp',
    '  user_profile2_->set_kernel_state(this);\n',
    '  user_profile2_->set_kernel_state(this);\n'
    '  ht_user_profile3_ = std::make_unique<xam::UserProfile>(0xB13EBABEBABEBAC0, "Player 3");\n'
    '  ht_user_profile3_->set_kernel_state(this);\n'
    '  ht_user_profile4_ = std::make_unique<xam::UserProfile>(0xB13EBABEBABEBAC1, "Player 4");\n'
    '  ht_user_profile4_->set_kernel_state(this);\n')

sub('src/system/xam/content_manager.cpp',
    '  if (xuid == kernel_state_->user_profile(1)->xuid()) profile = kernel_state_->user_profile(1);\n'
    '  auto user_name = rex::to_path(profile->name());\n',
    '  for (uint32_t i = 1; i < 4; ++i)\n'
    '    if (auto* p = kernel_state_->user_profile(i); p && xuid == p->xuid()) profile = p;\n'
    '  auto user_name = rex::to_path(profile->storage_name());\n')

sub('src/kernel/xam/xam_user.cpp', '    if (!xuids || xuid_count > 2) return X_ERROR_INVALID_PARAMETER;\n',
    '    if (!xuids || xuid_count > 4) return X_ERROR_INVALID_PARAMETER;\n')
sub('src/kernel/xam/xam_user.cpp', '      for (uint32_t slot = 0; slot < 2; ++slot) {\n',
    '      for (uint32_t slot = 0; slot < 4; ++slot) {\n')
print('patch_player_profiles: applied')
