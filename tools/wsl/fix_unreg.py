p = "/root/ht/sdk/src/system/xam/content_manager.cpp"
s = open(p).read()
old = '  fs->UnregisterSymbolicLink(device_path_ + "Strings");  // htrecomp overlay, if any\n'
if '"\\\\Strings"' not in s:
    assert s.count(old) == 1
    s = s.replace(old, old + '  fs->UnregisterSymbolicLink(device_path_ + "\\\\Strings");\n')
    open(p, "w").write(s)
print([l for l in s.splitlines() if "Strings" in l])
