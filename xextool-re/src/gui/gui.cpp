// gui.cpp - a small native Win32 front-end for the XexTool-RE engine.
//
// The "face lift": drag-free, click-driven XEX inspection and conversion built
// directly on the rebuilt engine (no shelling out). Open a .xex, read its info
// in a clean panel, and run operations with save dialogs and safety rails.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>

#include "../xex_file.h"
#include "../basefile.h"
#include "../extract.h"
#include "../modify.h"
#include "../convert.h"
#include "../idc.h"

using namespace xex;

// ---- control ids ---------------------------------------------------------
enum {
    IDC_OPEN = 1000, IDC_INFO, IDC_STATUS,
    IDC_EXTRACT, IDC_RESOURCES, IDC_REMOVE, IDC_DECOMP, IDC_CRYPT,
    IDC_IDC, IDC_COMPRESS, IDC_BINARY, IDC_BOUND, IDC_MACH_D, IDC_MACH_R,
    IDC_BOUNDPATH,
};

// ---- GUI state -----------------------------------------------------------
static HWND  g_info, g_status, g_crypt_btn, g_boundpath;
static HFONT g_font, g_mono;
static std::vector<uint8_t> g_raw;     // loaded file bytes
static std::string g_path;             // loaded file path
static bool  g_loaded = false;

// ---- helpers -------------------------------------------------------------
static bool read_file(const std::string& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize n = f.tellg(); f.seekg(0);
    out.resize(size_t(n));
    return bool(f.read(reinterpret_cast<char*>(out.data()), n));
}
static bool write_file(const std::string& p, const std::vector<uint8_t>& b) {
    std::ofstream f(p, std::ios::binary);
    return bool(f.write(reinterpret_cast<const char*>(b.data()), b.size()));
}
static void status(const std::string& s) { SetWindowTextA(g_status, s.c_str()); }

static std::string hex16(const std::vector<uint8_t>& b) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (uint8_t c : b) { s += d[c >> 4]; s += d[c & 0xF]; s += ' '; }
    return s;
}

// Build a readable info summary (CRLF line endings for the edit control).
static std::string info_summary(const XexFile& x) {
    std::string s;
    auto line = [&](const std::string& l){ s += l; s += "\r\n"; };
    char buf[256];

    line("== XEX INFO ==");
    line(std::string("  Format:       ") + (is_retail_xex(x) ? "Retail" : "Devkit"));
    line(std::string("  Compression:  ") + (x.is_compressed() ? "Compressed (LZX)" : "Not compressed"));
    line(std::string("  Encryption:   ") + (x.is_encrypted() ? "Encrypted (AES-128)" : "Not encrypted"));
    if (auto n = x.original_pe_name()) line("  PE name:      " + *n);
    if (auto a = x.image_base())  { std::snprintf(buf,sizeof buf,"  Load addr:    0x%08X",*a); line(buf); }
    if (auto e = x.entry_point()) { std::snprintf(buf,sizeof buf,"  Entry point:  0x%08X",*e); line(buf); }
    std::snprintf(buf,sizeof buf,"  Image size:   0x%X", x.security().image_size); line(buf);
    if (auto ex = x.execution_id()) {
        std::snprintf(buf,sizeof buf,"  Title id:     %08X", ex->title_id); line(buf);
        line("  Version:      " + ex->version.str());
    }
    if (auto bp = x.bounding_path()) line("  Bound path:   " + *bp);
    line("");
    std::snprintf(buf,sizeof buf,"  Region:       0x%08X", x.security().region); line(buf);
    std::snprintf(buf,sizeof buf,"  Allowed media:0x%08X", x.security().allowed_media); line(buf);
    line("  Media id:     " + hex16(x.security().media_id));

    if (!x.static_libraries().empty()) {
        line(""); line("== STATIC LIBRARIES ==");
        for (auto& l : x.static_libraries())
            line("  " + l.name + std::string(std::max<int>(1,14-(int)l.name.size()),' ') + l.version.str());
    }
    if (!x.import_libraries().empty()) {
        line(""); line("== IMPORT LIBRARIES ==");
        for (auto& l : x.import_libraries()) line("  " + l.name + "  " + l.version.str());
    }
    if (!x.resources().empty()) {
        line(""); line("== RESOURCES ==");
        for (auto& r : x.resources()) {
            std::snprintf(buf,sizeof buf,"  %-10s 0x%08X (0x%X bytes)", r.name.c_str(), r.address, r.size);
            line(buf);
        }
    }
    if (!x.sections().empty()) {
        line(""); line("== SECTIONS ==");
        for (auto& sec : x.sections()) {
            std::snprintf(buf,sizeof buf,"  0x%08X - 0x%08X  %s", sec.begin, sec.end, section_type_name(sec.type));
            line(buf);
        }
    }
    return s;
}

static std::string save_dialog(HWND h, const char* filter, const char* def) {
    char file[MAX_PATH] = {0};
    if (def) lstrcpynA(file, def, MAX_PATH);
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = h;
    ofn.lpstrFilter = filter; ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameA(&ofn) ? std::string(file) : std::string();
}

// Parse the currently loaded bytes (fresh copy each time, ops consume it).
static bool load_current(XexFile& out) {
    try { out = XexFile::parse(g_raw); return true; }
    catch (const std::exception& e) { status(std::string("parse error: ") + e.what()); return false; }
}

static void load_path(const std::string& file) {
    if (!read_file(file, g_raw)) { status("cannot read file"); return; }
    g_path = file;
    XexFile x;
    if (!load_current(x)) { SetWindowTextA(g_info, "(not a valid XEX2 file)"); g_loaded = false; return; }
    g_loaded = true;
    SetWindowTextA(g_info, info_summary(x).c_str());
    SetWindowTextA(g_crypt_btn, x.is_encrypted() ? "Decrypt" : "Encrypt");
    status(std::string("loaded ") + file);
}

static void do_open(HWND h) {
    char file[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = h;
    ofn.lpstrFilter = "Xbox360 executables (*.xex)\0*.xex\0All files\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) load_path(file);
}

static void run_write(HWND h, int op) {
    if (!g_loaded) { status("open a XEX first"); return; }
    XexFile x;
    if (!load_current(x)) return;
    try {
        if (op == IDC_EXTRACT) {
            std::string out = save_dialog(h, "Basefile (*.exe)\0*.exe\0All\0*.*\0", "basefile.exe");
            if (out.empty()) return;
            extract_basefile(x, out);
            status("wrote basefile -> " + out);
        } else if (op == IDC_RESOURCES) {
            char dir[MAX_PATH] = {0};
            BROWSEINFOA bi = {}; bi.hwndOwner = h; bi.lpszTitle = "Select output folder for resources";
            LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
            if (!pidl || !SHGetPathFromIDListA(pidl, dir)) return;
            int n = dump_resources(x, dir);
            status("dumped " + std::to_string(n) + " resource(s) -> " + dir);
        } else if (op == IDC_REMOVE) {
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0", "unlocked.xex");
            if (out.empty()) return;
            write_file(out, remove_limitations(x, "a"));
            status("removed all limits -> " + out);
        } else if (op == IDC_DECOMP) {
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0", "uncompressed.xex");
            if (out.empty()) return;
            write_file(out, decompress_to_basic(x, false));
            status("decompressed -> " + out);
        } else if (op == IDC_BINARY) {
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0", "binary.xex");
            if (out.empty()) return;
            write_file(out, decompress_to_basic(x, true));
            status("converted to flat binary -> " + out);
        } else if (op == IDC_COMPRESS) {
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0", "compressed.xex");
            if (out.empty()) return;
            status("compressing (this takes a while on large images)...");
            UpdateWindow(g_status);
            // Keep the source's encryption state, as the CLI does by default.
            write_file(out, compress_to_normal(x, x.is_encrypted()));
            status("LZX-compressed -> " + out);
        } else if (op == IDC_IDC) {
            std::string out = save_dialog(h, "IDA script (*.idc)\0*.idc\0All\0*.*\0", "basefile.idc");
            if (out.empty()) return;
            std::string s = make_idc(x);
            std::ofstream f(out, std::ios::binary);
            f.write(s.data(), s.size());
            status("wrote IDA script -> " + out);
        } else if (op == IDC_MACH_D || op == IDC_MACH_R) {
            bool to_devkit = (op == IDC_MACH_D);
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0",
                                          to_devkit ? "devkit.xex" : "retail.xex");
            if (out.empty()) return;
            write_file(out, convert_machine(x, to_devkit));
            status(to_devkit ? "converted to devkit (re-signed) -> " + out
                             : "converted to retail (signature cleared) -> " + out);
        } else if (op == IDC_BOUND) {
            char path[MAX_PATH] = {0};
            GetWindowTextA(g_boundpath, path, MAX_PATH);
            if (!path[0]) { status("enter a bounding path first"); return; }
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0", "bounded.xex");
            if (out.empty()) return;
            write_file(out, add_bounding_path(x, path));
            status(std::string("added bounding path '") + path + "' -> " + out);
        } else if (op == IDC_CRYPT) {
            bool enc = !x.is_encrypted();
            std::string out = save_dialog(h, "XEX (*.xex)\0*.xex\0All\0*.*\0",
                                          enc ? "encrypted.xex" : "decrypted.xex");
            if (out.empty()) return;
            write_file(out, set_encryption(x, enc));
            status(std::string(enc ? "encrypted -> " : "decrypted -> ") + out);
        }
    } catch (const std::exception& e) {
        status(std::string("error: ") + e.what());
    }
}

// ---- window --------------------------------------------------------------
static HWND mkbtn(HWND p, const char* t, int id, int x, int y, int w) {
    HWND b = CreateWindowA("BUTTON", t, WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                           x, y, w, 28, p, (HMENU)(intptr_t)id, 0, 0);
    SendMessage(b, WM_SETFONT, (WPARAM)g_font, TRUE);
    return b;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_font = CreateFontA(-15,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        g_mono = CreateFontA(-14,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Consolas");
        mkbtn(h, "Open XEX...", IDC_OPEN, 12, 12, 110);
        g_info = CreateWindowA("EDIT", "(open a .xex file to begin)",
            WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,
            12, 52, 660, 330, h, (HMENU)IDC_INFO, 0, 0);
        SendMessage(g_info, WM_SETFONT, (WPARAM)g_mono, TRUE);

        // Bounding path is the one operation that needs free text, so it gets a
        // field rather than a prompt (Win32 has no stock input dialog).
        HWND lbl = CreateWindowA("STATIC", "Bounding path:", WS_CHILD|WS_VISIBLE,
                                 12, 394, 104, 20, h, 0, 0, 0);
        SendMessage(lbl, WM_SETFONT, (WPARAM)g_font, TRUE);
        g_boundpath = CreateWindowA("EDIT", "",
            WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,
            120, 392, 552, 22, h, (HMENU)IDC_BOUNDPATH, 0, 0);
        SendMessage(g_boundpath, WM_SETFONT, (WPARAM)g_font, TRUE);

        const int w = 104, gap = 6;
        int x = 12, y = 424;                       // row 1: extract / inspect
        mkbtn(h, "Extract base",  IDC_EXTRACT,   x, y, w);   x += w+gap;
        mkbtn(h, "Resources",     IDC_RESOURCES, x, y, w);   x += w+gap;
        mkbtn(h, "IDA script",    IDC_IDC,       x, y, w);   x += w+gap;
        mkbtn(h, "Compress",      IDC_COMPRESS,  x, y, w);   x += w+gap;
        mkbtn(h, "Decompress",    IDC_DECOMP,    x, y, w);   x += w+gap;
        mkbtn(h, "Flat binary",   IDC_BINARY,    x, y, w);

        x = 12; y = 458;                           // row 2: modify / convert
        g_crypt_btn = mkbtn(h, "Encrypt", IDC_CRYPT, x, y, w); x += w+gap;
        mkbtn(h, "Remove limits", IDC_REMOVE,    x, y, w);   x += w+gap;
        mkbtn(h, "Add bnd path",  IDC_BOUND,     x, y, w);   x += w+gap;
        mkbtn(h, "-> Devkit",     IDC_MACH_D,    x, y, w);   x += w+gap;
        mkbtn(h, "-> Retail",     IDC_MACH_R,    x, y, w);

        g_status = CreateWindowA("STATIC", "ready",
            WS_CHILD|WS_VISIBLE|SS_SUNKEN, 12, 496, 660, 22, h, (HMENU)IDC_STATUS, 0, 0);
        SendMessage(g_status, WM_SETFONT, (WPARAM)g_font, TRUE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_OPEN: do_open(h); break;
        case IDC_EXTRACT: case IDC_RESOURCES: case IDC_REMOVE:
        case IDC_DECOMP: case IDC_CRYPT: case IDC_IDC: case IDC_COMPRESS:
        case IDC_BINARY: case IDC_BOUND: case IDC_MACH_D: case IDC_MACH_R:
            run_write(h, LOWORD(w)); break;
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int show) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "XexToolReWnd";
    RegisterClassA(&wc);
    HWND h = CreateWindowA("XexToolReWnd", "XexTool-RE  -  Xbox 360 XEX tool",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 570, 0, 0, hInst, 0);
    ShowWindow(h, show); UpdateWindow(h);

    // Open a file passed on the command line (supports "Open with" / drag-drop).
    std::string cmd = lpCmdLine ? lpCmdLine : "";
    if (!cmd.empty()) {
        if (cmd.front() == '"' && cmd.back() == '"' && cmd.size() >= 2)
            cmd = cmd.substr(1, cmd.size() - 2);
        load_path(cmd);
    }

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
