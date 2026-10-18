"""Adds a content-mount hook to the WSL SDK tree so the app can overlay files of a
content package (DLC string tables) without touching the package itself."""
SDK = "/root/ht/sdk"

h = f"{SDK}/include/rex/system/xam/content_manager.h"
s = open(h).read()
if "SetContentMountHook" not in s:
    s = s.replace("#include <algorithm>\n", "#include <algorithm>\n#include <filesystem>\n#include <functional>\n", 1)
    anchor = "class ContentPackage {"
    assert s.count(anchor) == 1
    api = '''// htrecomp: called after a content package is mounted, with its VFS device path
// (e.g. "\\\\Device\\\\Content\\\\1\\\\") and host folder. Lets the app layer overlay
// symbolic links under the device; ContentPackage removes "<device>Strings" on unmount.
using ContentMountHook =
    std::function<void(const std::string& device_path, const std::filesystem::path& package_path)>;
void SetContentMountHook(ContentMountHook hook);

'''
    s = s.replace(anchor, api + anchor)
    open(h, "w").write(s)

c = f"{SDK}/src/system/xam/content_manager.cpp"
s = open(c).read()
if "g_content_mount_hook" not in s:
    anchor = "static int content_device_id_ = 0;\n"
    assert s.count(anchor) == 1
    s = s.replace(anchor, anchor + '''
static ContentMountHook g_content_mount_hook;

void SetContentMountHook(ContentMountHook hook) { g_content_mount_hook = std::move(hook); }
''')
    old = '''  fs->RegisterSymbolicLink(root_name_ + ":", device_path_);
}
'''
    assert s.count(old) == 1
    s = s.replace(old, '''  fs->RegisterSymbolicLink(root_name_ + ":", device_path_);
  if (g_content_mount_hook) {
    g_content_mount_hook(device_path_, package_path);
  }
}
''')
    old = '''  fs->UnregisterSymbolicLink(root_name_ + ":");
  fs->UnregisterDevice(device_path_);'''
    assert s.count(old) == 1
    s = s.replace(old, '''  fs->UnregisterSymbolicLink(root_name_ + ":");
  fs->UnregisterSymbolicLink(device_path_ + "Strings");  // htrecomp overlay, if any
  fs->UnregisterSymbolicLink(device_path_ + "\\\\Strings");
  fs->UnregisterDevice(device_path_);''')
    open(c, "w").write(s)
print("ok")
