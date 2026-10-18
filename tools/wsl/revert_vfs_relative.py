"""Removes the relative-open symlink block added by patch_vfs_relative.py (not
needed: the DLC overlay miss was a double-backslash path spelling)."""
p = "/root/ht/sdk/src/filesystem/virtual_file_system.cpp"
s = open(p).read()
MARK = "  // htrecomp: symlinks for relative opens"
if MARK in s:
    start = s.index(MARK)
    end = s.index("  auto base_path = rex::string::utf8_find_base_guest_path(path);", start)
    s = s[:start] + s[end:]
    open(p, "w").write(s)
print("restante:", s.count("htrecomp: symlinks for relative opens"))
