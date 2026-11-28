// LAN play: the game's Xbox LIVE multiplayer, answered on the local network.
//
// Hydro Thunder Hurricane has no system link mode; its Quick Match, Search for Match
// and Create Match screens talk to Xbox LIVE. This file replaces the LIVE pieces those
// screens use with local-network equivalents, so players on one network race each
// other without a server:
//
//   sign-in, privileges  the local profiles report "signed in to LIVE" with the
//                        multiplayer privileges (the game refuses to go online otherwise)
//   XNet addresses       each game gets its real LAN address and a random identity
//   XSession             sessions are created, found and joined through ht_lan_transport
//   QoS                  answered from the discovery table (the host's QoS data included)
//   sockets              console ports are moved into the player's range (ht_lan_port)
//                        and packets from outside the LAN are dropped
//
// Mechanism: the recompiled code calls every kernel import through its
// rex_original_<name> symbol. Defining that symbol here takes precedence over the
// runtime library's copy; Forward() still reaches the library's original, so with
// ht_lan_play = false (and for every message that is not ours) the game sees exactly
// what it saw before.
#include "ht_lan_transport.h"
#include "ht_lan_ui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xevent.h>
#include <rex/system/xmemory.h>
#include <rex/system/xnotifylistener.h>
#include <rex/system/xsocket.h>
#include <rex/system/xthread.h>

REXCVAR_DEFINE_BOOL(ht_lan_play, true, "Network",
                    "Multiplayer's online modes (Quick Match, Search for Match, Create Match) play "
                    "over the local network instead of Xbox LIVE");
REXCVAR_DEFINE_INT32(ht_lan_port, 36000, "Network",
                     "UDP port for finding LAN games; game traffic uses the next 16 ports. All "
                     "players must use the same value");
REXCVAR_DEFINE_STRING(ht_lan_address, "", "Network",
                      "This computer's LAN IPv4 address to give other players (empty = automatic)");
REXCVAR_DEFINE_STRING(ht_lan_peers, "", "Network",
                      "Extra IPv4 addresses to look for games at, comma separated (for networks "
                      "that block broadcasts)");
REXCVAR_DEFINE_BOOL(ht_lan_allow_any_address, false, "Network",
                    "Accept game packets from any address, not only private and link-local ones");
REXCVAR_DEFINE_BOOL(ht_lan_trace, false, "Network", "Log every LAN play call (for bug reports)");

namespace {

using ht::lan::PortMap;
using GuestFunction = void (*)(PPCContext&, uint8_t*);

// --- plumbing ------------------------------------------------------------------------

GuestFunction Original(const char* name) {
#if defined(_WIN32)
  HMODULE runtime = GetModuleHandleA("librexruntime.dll");
  auto fn = runtime ? reinterpret_cast<GuestFunction>(GetProcAddress(runtime, name)) : nullptr;
#else
  void* runtime = dlopen("librexruntime.so", RTLD_NOW | RTLD_NOLOAD);
  auto fn = runtime ? reinterpret_cast<GuestFunction>(dlsym(runtime, name)) : nullptr;
#endif
  if (!fn) {
    // Without the original the game would silently lose the call, even with LAN play
    // off; this is a build mismatch, so stop rather than run wrong.
    REXLOG_ERROR("[ht-lan] runtime export {} not found", name);
    rex::FlushLogging();
    std::abort();
  }
  return fn;
}

// Runs the runtime's own implementation with the registers untouched.
// An expression, so a hook can `return HT_FORWARD(...)`.
#define HT_FORWARD(name)                               \
  [&] {                                                \
    static GuestFunction original = Original(#name);   \
    if (original) original(ctx, base);                 \
  }()

bool Enabled() { return REXCVAR_GET(ht_lan_play); }
bool Tracing() { return REXCVAR_GET(ht_lan_trace); }
#define HT_TRACE(...)                                          \
  do {                                                         \
    if (Tracing()) REXLOG_WARN("[ht-lan] " __VA_ARGS__);       \
  } while (0)

uint32_t Arg(const PPCContext& ctx, uint8_t* base, int i) {
  switch (i) {
    case 0: return ctx.r3.u32;
    case 1: return ctx.r4.u32;
    case 2: return ctx.r5.u32;
    case 3: return ctx.r6.u32;
    case 4: return ctx.r7.u32;
    case 5: return ctx.r8.u32;
    case 6: return ctx.r9.u32;
    case 7: return ctx.r10.u32;
    default: {
      const uint8_t* p = base + ctx.r1.u32 + 0x54 + (i - 8) * 8;
      return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
  }
}
void Return(PPCContext& ctx, uint32_t value) { ctx.r3.u64 = value; }

uint8_t* Guest(uint32_t address) {
  return address ? REX_KERNEL_MEMORY()->TranslateVirtual<uint8_t*>(address) : nullptr;
}
uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(Be16(p)) << 16 | Be16(p + 2); }
uint64_t Be64(const uint8_t* p) { return uint64_t(Be32(p)) << 32 | Be32(p + 4); }
void Put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
void Put32(uint8_t* p, uint32_t v) { Put16(p, uint16_t(v >> 16)), Put16(p + 2, uint16_t(v)); }
void Put64(uint8_t* p, uint64_t v) { Put32(p, uint32_t(v >> 32)), Put32(p + 4, uint32_t(v)); }

constexpr uint32_t kSuccess = 0;
constexpr uint32_t kSocketError = 0xFFFFFFFF;
constexpr uint32_t kWsaEinval = 10022;
constexpr uint32_t kWsaEconnaborted = 10053;
constexpr uint32_t kXEInvalidArg = 0x80070057;
constexpr uint32_t kErrorIoPending = 0x3E5;
constexpr uint32_t kXOnlineSessionFull = 0x80155209;      // X_ONLINE_E_SESSION_FULL

// Guest structure sizes (Xbox 360 SDK layouts).
constexpr uint32_t kXnAddrSize = 36;       // XNADDR
constexpr uint32_t kSessionInfoSize = 60;  // XSESSION_INFO: XNKID, XNADDR, XNKEY
constexpr uint32_t kSearchResultSize = 0x5C;
constexpr uint32_t kLocalDetailsSize = 0x80;
constexpr uint32_t kMemberSize = 0x10;
constexpr uint32_t kPropertySize = 0x18;   // XUSER_PROPERTY
constexpr uint32_t kContextSize = 8;       // XUSER_CONTEXT
constexpr uint32_t kQosInfoSize = 0x18;    // XNQOSINFO

// XSESSION_CREATE_* flags.
constexpr uint32_t kSessionHost = 0x01;
constexpr uint32_t kSessionMatchmaking = 0x08;
constexpr uint32_t kSessionPeerNetwork = 0x20;

// --- state -------------------------------------------------------------------------

struct Member {
  uint64_t xuid = 0;
  uint32_t user_index = 0xFE;  // 0xFE: remote
  bool private_slot = false;
};

struct Session {
  uint32_t object = 0;  // guest pointer handed to the game
  uint32_t handle = 0;
  bool created = false, deleted = false, host = false, advertised = false;
  uint32_t flags = 0, user_index = 0;
  uint32_t max_public = 0, max_private = 0;
  uint32_t state = 0;  // XSESSION_STATE
  uint64_t nonce = 0;
  std::array<uint8_t, kSessionInfoSize> info{};
  std::vector<Member> members;
  uint32_t game_type = 0, game_mode = 0;
};

struct UserData {
  std::map<uint32_t, uint32_t> contexts;
  std::map<uint32_t, ht::lan::Property> properties;
};

class Lan {
 public:
  static Lan& Get() {
    // Never destroyed: the receive worker and discovery thread may still run while
    // the process exits.
    static Lan& lan = *new Lan;
    return lan;
  }
  std::mutex mutex;
  ht::lan::Discovery discovery;
  PortMap ports;
  uint32_t address = 0;  // host order
  uint64_t instance = 0;
  std::array<uint8_t, 6> mac{};
  uint32_t spi = 0;
  uint64_t online_xuid_base = 0;
  std::vector<uint8_t> qos_data;
  std::map<uint32_t, Session> sessions;           // by guest object
  std::map<uint32_t, uint32_t> handles;           // handle -> object
  std::map<uint32_t, std::array<uint8_t, kXnAddrSize>> addresses;  // ip -> XNADDR
  UserData users[4];
  // The game's rich-presence value per player (5 on every Multiplayer screen) and the
  // Join Game list state, read by the in-game LAN screens (ht_lan_panel.cpp).
  std::atomic<uint32_t> presence[4] = {};
  std::atomic<bool> join_list{false};
  uint32_t next_handle = 0xF1A00000;
  bool discovery_failed = false;
  bool address_fixed = false;

  // Identity and address for this run; cheap, called from every entry point, from
  // any thread and with or without `mutex` held. Everything it sets is read-only after.
  void Init() { std::call_once(init_once_, [this] { InitOnce(); }); }
  const PortMap& Ports() { return Init(), ports; }

 private:
  std::once_flag init_once_;
  void InitOnce() {
    instance = ht::lan::RandomU64() | 1;
    const int base = REXCVAR_GET(ht_lan_port);
    ports.base = uint16_t(ht::lan::IsUsableBasePort(base) ? base : 36000);
    address_fixed = ht::lan::ParseAddress(REXCVAR_GET(ht_lan_address)).has_value();
    address = ht::lan::PickLocalAddress(REXCVAR_GET(ht_lan_address));
    ht::lan::RandomBytes(mac.data(), mac.size());
    mac[0] = uint8_t((mac[0] & 0xFC) | 0x02);  // locally administered, unicast
    spi = uint32_t(ht::lan::RandomU64());
    // Online XUIDs (0009xxxxxxxxxxxx) unique to this run, one per local player.
    online_xuid_base = 0x0009000000000000ull | ((ht::lan::RandomU64() & 0x3FFFFFFFFFull) << 2);
    REXLOG_WARN("[ht-lan] LAN play on {} (UDP {} discovery, {}-{} game)",
                ht::lan::AddressToString(address), ports.discovery(), ports.base + 1,
                ports.base + PortMap::kGamePorts);
  }

 public:
  // Discovery opens its port only when a multiplayer screen first needs it, so
  // single player never listens on the network.
  bool StartDiscovery() {
    Init();
    if (discovery.running()) return true;
    if (discovery_failed) return false;
    ht::lan::Discovery::Options o;
    o.port = ports.discovery();
    o.title_id = REX_KERNEL_STATE()->title_id();
    o.instance = instance;
    o.allow_any_source = REXCVAR_GET(ht_lan_allow_any_address);
    // A chosen address is also the only one listened on, so two games on one
    // computer (127.0.0.1 and 127.0.0.2, say) can use the same ports.
    if (address_fixed) o.bind_address = address;
    if ((address >> 24) == 127) o.broadcast = false;
    std::string peers = REXCVAR_GET(ht_lan_peers);
    for (size_t pos = 0; pos <= peers.size();) {
      size_t comma = peers.find(',', pos);
      if (comma == std::string::npos) comma = peers.size();
      std::string item = peers.substr(pos, comma - pos);
      item.erase(0, item.find_first_not_of(' '));
      item.erase(item.find_last_not_of(' ') + 1);
      auto ip = ht::lan::ParseAddress(item);
      if (ip && !o.allow_any_source && !ht::lan::IsLanAddress(*ip)) {
        REXLOG_WARN("[ht-lan] ignoring ht_lan_peers entry {} (not a LAN address)", item);
      } else if (ip) {
        o.peers.emplace_back(*ip, o.port);
      }
      else if (!item.empty()) REXLOG_WARN("[ht-lan] ignoring ht_lan_peers entry \"{}\"", item);
      pos = comma + 1;
    }
    std::string error;
    if (!discovery.Start(o, &error)) {
      discovery_failed = true;
      REXLOG_ERROR("[ht-lan] cannot open LAN discovery: {}", error);
      return false;
    }
    discovery.SetLocalXnAddr(LocalXnAddr());
    return true;
  }

  std::array<uint8_t, kXnAddrSize> LocalXnAddr() {
    Init();
    std::array<uint8_t, kXnAddrSize> x{};
    Put32(&x[0], address);   // ina
    Put32(&x[4], address);   // inaOnline
    Put16(&x[8], ports.ToHost(PortMap::kGuestFirst));  // wPortOnline
    std::memcpy(&x[10], mac.data(), 6);                 // abEnet
    Put32(&x[16], address);  // abOnline: SGADDR.ina
    Put32(&x[20], spi);      // SGADDR.dwSpiSg
    Put64(&x[24], instance);  // SGADDR.qwXboxID
    return x;
  }

  uint64_t OnlineXuid(uint32_t user) { return Init(), online_xuid_base | (user & 3); }
  bool IsOurOnlineXuid(uint64_t xuid) { return (xuid & ~3ull) == online_xuid_base; }

  void Remember(const std::array<uint8_t, kXnAddrSize>& xnaddr) {
    const uint32_t ip = Be32(&xnaddr[0]);
    if (addresses.size() >= ht::lan::kMaxPeers && !addresses.count(ip)) {
      // Full: forget one address that is not the host of a live session or a player
      // who said HELLO, so a flood cannot cut the players of a running race.
      auto in_use = [&](uint32_t candidate) {
        for (const auto& [object, s] : sessions)
          if (s.created && !s.deleted && Be32(&s.info[8]) == candidate) return true;
        return discovery.FindPeer(candidate).has_value();
      };
      for (auto it = addresses.begin(); it != addresses.end(); ++it)
        if (!in_use(it->first)) {
          addresses.erase(it);
          break;
        }
      if (addresses.size() >= ht::lan::kMaxPeers) return;  // all in use: keep them
    }
    addresses[ip] = xnaddr;
  }

  Session* Find(uint32_t object) {
    auto it = sessions.find(object);
    return it == sessions.end() ? nullptr : &it->second;
  }

  void Advertise(Session& s) {
    if (!s.host || !(s.flags & kSessionMatchmaking) || s.deleted) return;
    if (!StartDiscovery()) return;
    ht::lan::SessionAd ad;
    std::memcpy(ad.session_id.data(), &s.info[0], 8);
    std::memcpy(ad.host_xnaddr.data(), &s.info[8], kXnAddrSize);
    std::memcpy(ad.key.data(), &s.info[44], 16);
    ad.flags = s.flags;
    ad.game_type = s.game_type;
    ad.game_mode = s.game_mode;
    ad.max_public = std::min<uint32_t>(s.max_public, 32);
    ad.max_private = std::min<uint32_t>(s.max_private, 32);
    for (const auto& m : s.members) (m.private_slot ? ad.filled_private : ad.filled_public)++;
    ad.filled_public = std::min(ad.filled_public, ad.max_public);
    ad.filled_private = std::min(ad.filled_private, ad.max_private);
    ad.host_xuid = OnlineXuid(s.user_index);
    const UserData& u = users[s.user_index & 3];
    for (const auto& [id, value] : u.contexts)
      if (ad.contexts.size() < ht::lan::kMaxContexts) ad.contexts.emplace_back(id, value);
    for (const auto& [id, p] : u.properties)
      if (ad.properties.size() < ht::lan::kMaxProperties) ad.properties.push_back(p);
    ad.qos = qos_data;
    if (auto* profile = REX_KERNEL_STATE()->user_profile(s.user_index)) {
      for (char c : profile->name())
        if (c >= 0x20 && c <= 0x7E && ad.host_name.size() < ht::lan::kMaxHostName)
          ad.host_name += c;
    }
    discovery.Advertise(ad);
    s.advertised = true;
  }

  void Unadvertise(Session& s) {
    if (!s.advertised) return;
    ht::lan::SessionId id;
    std::memcpy(id.data(), &s.info[0], 8);
    discovery.Unadvertise(id);
    s.advertised = false;
  }
};

bool UserExists(uint32_t user) {
  return user < 4 && REX_KERNEL_STATE()->user_profile(user) != nullptr;
}

// --- XNet ----------------------------------------------------------------------------

}  // namespace

REX_HOOK_RAW(rex_original_NetDll_XNetRandom) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetRandom);
  // The runtime fills this with a constant, which would give every player the same
  // session IDs, keys and nonces.
  if (uint8_t* out = Guest(Arg(ctx, base, 1))) ht::lan::RandomBytes(out, Arg(ctx, base, 2));
  Return(ctx, kSuccess);
}

REX_HOOK_RAW(rex_original_NetDll_XNetGetTitleXnAddr) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetGetTitleXnAddr);
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  const auto x = lan.LocalXnAddr();
  if (uint8_t* out = Guest(Arg(ctx, base, 1))) std::memcpy(out, x.data(), x.size());
  HT_TRACE("XNetGetTitleXnAddr -> {}", ht::lan::AddressToString(lan.address));
  // STATIC | GATEWAY | DNS | ONLINE
  Return(ctx, 0x04 | 0x20 | 0x40 | 0x80);
}

REX_HOOK_RAW(rex_original_NetDll_XNetXnAddrToInAddr) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetXnAddrToInAddr);
  const uint8_t* xnaddr = Guest(Arg(ctx, base, 1));
  uint8_t* out = Guest(Arg(ctx, base, 3));
  if (!xnaddr || !out) return Return(ctx, kWsaEinval);
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  std::array<uint8_t, kXnAddrSize> x;
  std::memcpy(x.data(), xnaddr, x.size());
  lan.Remember(x);
  std::memcpy(out, xnaddr, 4);  // the peer's LAN address is its IN_ADDR
  HT_TRACE("XNetXnAddrToInAddr -> {}", ht::lan::AddressToString(Be32(xnaddr)));
  Return(ctx, kSuccess);
}

REX_HOOK_RAW(rex_original_NetDll_XNetInAddrToXnAddr) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetInAddrToXnAddr);
  const uint32_t ina = Arg(ctx, base, 1);
  uint8_t* xnaddr = Guest(Arg(ctx, base, 2));
  uint8_t* xnkid = Guest(Arg(ctx, base, 3));
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  lan.Init();
  std::optional<std::array<uint8_t, kXnAddrSize>> found;
  if (ina == lan.address) found = lan.LocalXnAddr();
  else if (auto it = lan.addresses.find(ina); it != lan.addresses.end()) found = it->second;
  else if (auto remote = lan.discovery.FindByAddress(ina)) found = remote->ad.host_xnaddr;
  else if (auto peer = lan.discovery.FindPeer(ina)) found = *peer;
  HT_TRACE("XNetInAddrToXnAddr({}) -> {}", ht::lan::AddressToString(ina), found ? "found" : "unknown");
  if (!found) return Return(ctx, kWsaEinval);
  if (xnaddr) std::memcpy(xnaddr, found->data(), kXnAddrSize);
  if (xnkid) {
    std::memset(xnkid, 0, 8);
    for (const auto& [object, s] : lan.sessions)
      if (s.created && !s.deleted) std::memcpy(xnkid, &s.info[0], 8);
  }
  Return(ctx, kSuccess);
}

// Connections are plain LAN UDP: there is no secure association to set up.
REX_HOOK_RAW(rex_original_NetDll_XNetConnect) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetConnect);
  Return(ctx, kSuccess);
}
REX_HOOK_RAW(rex_original_NetDll_XNetGetConnectStatus) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetGetConnectStatus);
  Return(ctx, 2);  // XNET_CONNECT_STATUS_CONNECTED
}
REX_HOOK_RAW(rex_original_NetDll_XNetRegisterKey) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetRegisterKey);
  Return(ctx, kSuccess);
}
REX_HOOK_RAW(rex_original_NetDll_XNetReplaceKey) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetReplaceKey);
  Return(ctx, kSuccess);
}
REX_HOOK_RAW(rex_original_NetDll_XNetUnregisterInAddr) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetUnregisterInAddr);
  Return(ctx, kSuccess);
}

// XNetQosListen(caller, xnkid, data, size, bits_per_sec, flags)
REX_HOOK_RAW(rex_original_NetDll_XNetQosListen) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetQosListen);
  const uint32_t flags = Arg(ctx, base, 5);
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  if (flags & 0x04) {  // XNET_QOS_LISTEN_SET_DATA
    const uint8_t* data = Guest(Arg(ctx, base, 2));
    const uint32_t size = std::min<uint32_t>(Arg(ctx, base, 3), ht::lan::kMaxQosData);
    lan.qos_data.assign(data, data ? data + size : data);
  }
  if (flags & 0x10) lan.qos_data.clear();  // XNET_QOS_LISTEN_RELEASE
  HT_TRACE("XNetQosListen({:X}, {:X}, {:X}, {:X}, {:X}) data={} bytes", Arg(ctx, base, 1),
           Arg(ctx, base, 2), Arg(ctx, base, 3), Arg(ctx, base, 4), flags, lan.qos_data.size());
  for (auto& [object, s] : lan.sessions)
    if (s.advertised) lan.Advertise(s);
  Return(ctx, kSuccess);
}

// XNetQosLookup(caller, count, xnaddrs[], xnkids[], xnkeys[], gateways, ..., flags, event, out)
REX_HOOK_RAW(rex_original_NetDll_XNetQosLookup) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_XNetQosLookup);
  const uint32_t count = std::min<uint32_t>(Arg(ctx, base, 1), 64);
  const uint8_t* addrs = Guest(Arg(ctx, base, 2));
  const uint32_t event = Arg(ctx, base, 11);
  uint8_t* out = Guest(Arg(ctx, base, 12));
  if (!out) return Return(ctx, kWsaEinval);
  auto& lan = Lan::Get();
  std::vector<std::vector<uint8_t>> data(count);
  std::vector<bool> contacted(count, false);
  {
    std::lock_guard<std::mutex> lock(lan.mutex);
    for (uint32_t i = 0; i < count && addrs; ++i) {
      const uint8_t* x = Guest(Be32(addrs + i * 4));
      if (!x) continue;
      if (auto remote = lan.discovery.FindByAddress(Be32(x))) {
        contacted[i] = true;
        data[i] = remote->ad.qos;
      }
    }
  }
  // One block for the result and every peer's data, so XNetQosRelease frees it all.
  uint32_t total = 8 + count * kQosInfoSize;
  for (const auto& d : data) total += uint32_t(d.size());
  const uint32_t block = REX_KERNEL_MEMORY()->SystemHeapAlloc(std::max<uint32_t>(total, 8));
  uint8_t* q = Guest(block);
  if (!q) return Return(ctx, 0x2747);  // WSAENOBUFS
  std::memset(q, 0, total);
  Put32(q, count);  // cxnqos
  Put32(q + 4, 0);  // cxnqosPending
  uint32_t data_at = block + 8 + count * kQosInfoSize;
  for (uint32_t i = 0; i < count; ++i) {
    uint8_t* info = q + 8 + i * kQosInfoSize;
    uint8_t flags = 0x01;                // COMPLETE
    if (contacted[i]) flags |= 0x02;     // TARGET_CONTACTED
    if (!data[i].empty()) flags |= 0x08;  // DATA_RECEIVED
    info[0] = flags;
    if (contacted[i]) {
      Put16(info + 2, 4);  // probes sent
      Put16(info + 4, 4);  // probes received
      Put16(info + 6, uint16_t(data[i].size()));
      Put32(info + 8, data[i].empty() ? 0 : data_at);
      Put16(info + 12, 1);  // RTT min (ms)
      Put16(info + 14, 2);  // RTT median (ms)
      Put32(info + 16, 100000000);  // up bits/s
      Put32(info + 20, 100000000);  // down bits/s
      if (!data[i].empty()) std::memcpy(Guest(data_at), data[i].data(), data[i].size());
      data_at += uint32_t(data[i].size());
    }
  }
  Put32(out, block);
  HT_TRACE("XNetQosLookup {} target(s)", count);
  if (event)
    if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event)) ev->Set(0, false);
  Return(ctx, kSuccess);
}

// --- sockets -------------------------------------------------------------------------
//
// The game talks over VDP datagram sockets with overlapped I/O: it posts a
// WSARecvFrom with an OVERLAPPED, expects WSA_IO_PENDING, waits on the event and
// collects the datagram with WSAGetOverlappedResult. The runtime stubs both calls
// (the receive always fails, which the game reports as "Could not join the game"),
// so in LAN play the datagram calls run here on the native socket:
//
//   - console ports (1000+) are moved into the player's range (ht_lan_port + 1...);
//   - datagrams from outside the LAN are dropped unless ht_lan_allow_any_address;
//   - pending receives are completed by one worker thread, and cancelled with
//     WSA_OPERATION_ABORTED when the socket closes.
//
// Guest structures: XSOCKADDR_IN {be16 family, be16 port, be32 addr},
// XWSABUF {be32 len, be32 buf}, XWSAOVERLAPPED {be32 internal, be32 internal_high,
// be32 offset[2], be32 event}.

namespace {

constexpr uint32_t kStatusPending = 0x103;
constexpr uint32_t kWsaIoPending = 997;
constexpr uint32_t kWsaIoIncomplete = 996;
constexpr uint32_t kWsaOperationAborted = 995;
constexpr uint32_t kWsaEwouldblock = 10035;
constexpr uint32_t kWsaEnotsock = 10038;
constexpr uint32_t kWsaEmsgsize = 10040;
constexpr uint32_t kWsaEnetdown = 10050;
constexpr size_t kMaxDatagramSize = 65536;

#if defined(_WIN32)
using NativeSocket = SOCKET;
uint32_t NativeError() { return uint32_t(WSAGetLastError()); }
#else
using NativeSocket = int;
uint32_t NativeError() {
  switch (errno) {
    case EAGAIN: return kWsaEwouldblock;
    case EMSGSIZE: return kWsaEmsgsize;
    case EBADF: case ENOTSOCK: return kWsaEnotsock;
    default: return kWsaEnetdown;
  }
}
#endif

bool AllowedSource(uint32_t ip) {
  return REXCVAR_GET(ht_lan_allow_any_address) || ht::lan::IsLanAddress(ip) ||
         Lan::Get().discovery.IsTrusted(ip);  // typed in by the player (join by IP)
}

void DroppedSource(uint32_t ip) {
  static std::atomic<uint32_t> dropped{0};
  if (dropped++ % 100 == 0)
    REXLOG_WARN("[ht-lan] dropped a packet from {} (not a LAN address; {} so far)",
                ht::lan::AddressToString(ip), dropped.load());
}

std::optional<NativeSocket> NativeOf(uint32_t handle) {
  auto socket = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XSocket>(handle);
  if (!socket) return std::nullopt;
  return NativeSocket(socket->native_handle());
}

void SignalEvent(uint32_t handle) {
  if (!handle) return;
  if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(handle)) ev->Set(0, false);
}
void ResetEvent(uint32_t handle) {
  if (!handle) return;
  if (auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(handle)) ev->Reset();
}

void CompleteOverlapped(uint32_t overlapped, uint32_t status, uint32_t bytes) {
  uint8_t* o = Guest(overlapped);
  if (!o) return;
  Put32(o + 4, bytes);
  // The game polls the status word; the byte count must be visible before it.
  std::atomic_thread_fence(std::memory_order_release);
  Put32(o, status);
  SignalEvent(Be32(o + 16));
}

using GuestBufferList = std::vector<std::pair<uint32_t, uint32_t>>;  // (address, length)

// Sends one datagram gathered from guest buffers. Returns bytes sent, or nullopt with
// `error` set.
std::optional<uint32_t> SendDatagram(NativeSocket s, const GuestBufferList& buffers,
                                     const uint8_t* to, uint32_t* error) {
  std::vector<uint8_t> data;
  for (const auto& [ptr, len] : buffers) {
    const uint8_t* p = Guest(ptr);
    if (!p || data.size() + len > kMaxDatagramSize) {
      *error = kWsaEmsgsize;
      return std::nullopt;
    }
    data.insert(data.end(), p, p + len);
  }
  sockaddr_in dest{};
  sockaddr* dest_ptr = nullptr;
  if (to) {
    // Game traffic goes to LAN addresses only, like what it accepts.
    if (!AllowedSource(Be32(to + 4))) {
      *error = 10065;  // WSAEHOSTUNREACH
      return std::nullopt;
    }
    dest.sin_family = AF_INET;
    dest.sin_port = htons(Lan::Get().Ports().ToHost(Be16(to + 2)));
    dest.sin_addr.s_addr = htonl(Be32(to + 4));
    dest_ptr = reinterpret_cast<sockaddr*>(&dest);
  }
  const int sent = sendto(s, reinterpret_cast<const char*>(data.data()), int(data.size()), 0,
                          dest_ptr, dest_ptr ? sizeof(dest) : 0);
  if (sent < 0) {
    *error = NativeError();
    return std::nullopt;
  }
  return uint32_t(sent);
}

// Receives one LAN datagram without blocking and scatters it into guest buffers.
// Returns bytes received, or nullopt with `error` (WSAEWOULDBLOCK when none waits).
std::optional<uint32_t> ReceiveDatagram(NativeSocket s, const GuestBufferList& buffers,
                                        uint32_t from_ptr, uint32_t fromlen_ptr,
                                        uint32_t* error) {
  static thread_local std::vector<uint8_t> data(kMaxDatagramSize);
  // A flood from outside the LAN is dropped a bounded amount per call, so it cannot
  // hold this thread (or the receive worker's lock) indefinitely.
  for (int dropped = 0; dropped < 64;) {
    if (ht::lan::WaitReadable(intptr_t(s), 0) <= 0) {
      *error = kWsaEwouldblock;
      return std::nullopt;
    }
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    const int got = recvfrom(s, reinterpret_cast<char*>(data.data()), int(data.size()), 0,
                             reinterpret_cast<sockaddr*>(&from), &from_len);
    if (got < 0) {
      *error = NativeError();
#if defined(_WIN32)
      // An earlier send reached a closed port (ICMP); harmless for datagram sockets.
      if (*error == WSAECONNRESET) continue;
#endif
      return std::nullopt;
    }
    const uint32_t ip = ntohl(from.sin_addr.s_addr);
    if (!AllowedSource(ip)) {
      DroppedSource(ip);
      ++dropped;
      continue;
    }
    uint32_t copied = 0;
    for (const auto& [ptr, len] : buffers) {
      uint8_t* p = Guest(ptr);
      const uint32_t n = std::min<uint32_t>(len, uint32_t(got) - copied);
      if (p && n) std::memcpy(p, data.data() + copied, n);
      copied += n;
    }
    if (uint8_t* f = Guest(from_ptr)) {
      std::memset(f, 0, 16);
      Put16(f, 2);  // AF_INET
      Put16(f + 2, Lan::Get().Ports().ToGuest(ntohs(from.sin_port)));
      Put32(f + 4, ip);
    }
    if (uint8_t* fl = Guest(fromlen_ptr)) Put32(fl, 16);
    if (copied < uint32_t(got)) {
      *error = kWsaEmsgsize;
      return std::nullopt;
    }
    return copied;
  }
  *error = kWsaEwouldblock;  // only dropped datagrams so far; the caller tries again later
  return std::nullopt;
}

GuestBufferList GuestBuffers(uint32_t wsabufs, uint32_t count) {
  GuestBufferList out;
  const uint8_t* b = Guest(wsabufs);
  for (uint32_t i = 0; b && i < std::min<uint32_t>(count, 16); ++i)
    out.emplace_back(Be32(b + i * 8 + 4), Be32(b + i * 8));
  return out;
}

// Datagram counters, logged every 10 s with ht_lan_trace (per-packet lines would cost
// frame time).
struct IoCounters {
  std::atomic<uint64_t> sends{0}, received{0}, posted{0}, completed{0};
};
IoCounters g_io;

// Overlapped receives waiting for a datagram.
class PendingReceives {
 public:
  struct Op {
    uint32_t socket = 0;
    GuestBufferList buffers;
    uint32_t from = 0, fromlen = 0, overlapped = 0;
  };
  static PendingReceives& Get() {
    static PendingReceives& p = *new PendingReceives;  // never destroyed, see Lan::Get
    return p;
  }
  void Add(Op op) {
    std::lock_guard<std::mutex> lock(mutex_);
    ops_.push_back(std::move(op));
    // One worker for the whole game, started on first use. (A detached std::thread is
    // no longer joinable(), so that cannot be the "already started" test.)
    if (!started_) {
      started_ = true;
      std::thread([this] { Run(); }).detach();
    }
    wake_.notify_all();
  }
  void Cancel(uint32_t socket) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = ops_.begin(); it != ops_.end();) {
      if (it->socket == socket) {
        CompleteOverlapped(it->overlapped, kWsaOperationAborted, 0);
        it = ops_.erase(it);
      } else {
        ++it;
      }
    }
    done_.notify_all();
  }
  // Blocks until the overlapped operation is no longer pending.
  void Wait(uint32_t overlapped) {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [&] {
      return std::none_of(ops_.begin(), ops_.end(),
                          [&](const Op& op) { return op.overlapped == overlapped; });
    });
  }

 private:
  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      wake_.wait(lock, [&] { return !ops_.empty(); });
      bool progress = false;
      for (auto it = ops_.begin(); it != ops_.end();) {
        auto native = NativeOf(it->socket);
        uint32_t error = 0;
        std::optional<uint32_t> got;
        if (native) got = ReceiveDatagram(*native, it->buffers, it->from, it->fromlen, &error);
        if (!native || got || error != kWsaEwouldblock) {
          const uint32_t status = got ? 0 : (native ? error : kWsaOperationAborted);
          // Only the OVERLAPPED is written now. The byte-count and flags pointers given to
          // WSARecvFrom usually point into the caller's stack frame, long gone by now;
          // the game reads the result through WSAGetOverlappedResult instead.
          CompleteOverlapped(it->overlapped, status, got.value_or(0));
          it = ops_.erase(it);
          progress = true;
          ++g_io.completed;
        } else {
          ++it;
        }
      }
      if (progress) done_.notify_all();
      const auto now = std::chrono::steady_clock::now();
      if (Tracing() && now >= next_stats_) {
        next_stats_ = now + std::chrono::seconds(10);
        REXLOG_WARN("[ht-lan] io: sent {} received {} posted {} completed {} pending {}",
                    g_io.sends.load(), g_io.received.load(), g_io.posted.load(),
                    g_io.completed.load(), ops_.size());
      }
      // Game datagrams come at frame rate at most; a 1 ms poll keeps latency negligible.
      lock.unlock();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      lock.lock();
    }
  }
  std::mutex mutex_;
  std::condition_variable wake_, done_;
  std::vector<Op> ops_;
  std::chrono::steady_clock::time_point next_stats_{};
  bool started_ = false;
};

}  // namespace

REX_HOOK_RAW(rex_original_NetDll_bind) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_bind);
  uint8_t* name = Guest(Arg(ctx, base, 2));
  auto& lan = Lan::Get();
  {
    std::lock_guard<std::mutex> lock(lan.mutex);
    lan.Init();
  }
  const uint16_t guest = name ? Be16(name + 2) : 0;
  const uint16_t host = lan.Ports().ToHost(guest);
  const uint32_t saved_addr = name ? Be32(name + 4) : 0;
  // With a chosen ht_lan_address, "any address" binds to that address only.
  if (name && lan.address_fixed && saved_addr == 0) Put32(name + 4, lan.address);
  if (name) Put16(name + 2, host);
  HT_FORWARD(rex_original_NetDll_bind);
  if (name) Put16(name + 2, guest), Put32(name + 4, saved_addr);
  HT_TRACE("bind port {} -> {} result {:X}", guest, host, ctx.r3.u32);
}

// sendto(caller, socket, buf, len, flags, to, to_len)
REX_HOOK_RAW(rex_original_NetDll_sendto) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_sendto);
  auto native = NativeOf(Arg(ctx, base, 1));
  if (!native) {
    rex::system::XThread::SetLastError(kWsaEnotsock);
    return Return(ctx, kSocketError);
  }
  const uint8_t* to = Guest(Arg(ctx, base, 5));
  uint32_t error = 0;
  auto sent = SendDatagram(*native, {{Arg(ctx, base, 2), Arg(ctx, base, 3)}}, to, &error);
  ++g_io.sends;
  if (!sent) {
    rex::system::XThread::SetLastError(error);
    return Return(ctx, kSocketError);
  }
  Return(ctx, *sent);
}

// WSASendTo(caller, socket, buffers, count, sent, flags, to, to_len, overlapped, routine):
// datagram sends complete at once; the overlapped result is filled in and signalled.
REX_HOOK_RAW(rex_original_NetDll_WSASendTo) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_WSASendTo);
  auto native = NativeOf(Arg(ctx, base, 1));
  if (!native) {
    rex::system::XThread::SetLastError(kWsaEnotsock);
    return Return(ctx, kSocketError);
  }
  const uint8_t* to = Guest(Arg(ctx, base, 6));
  const uint32_t overlapped = Arg(ctx, base, 8);
  uint32_t error = 0;
  auto sent =
      SendDatagram(*native, GuestBuffers(Arg(ctx, base, 2), Arg(ctx, base, 3)), to, &error);
  ++g_io.sends;
  if (!sent) {
    rex::system::XThread::SetLastError(error);
    return Return(ctx, kSocketError);
  }
  if (uint8_t* n = Guest(Arg(ctx, base, 4))) Put32(n, *sent);
  if (overlapped) CompleteOverlapped(overlapped, 0, *sent);
  Return(ctx, 0);
}

// recvfrom(caller, socket, buf, len, flags, from, fromlen)
REX_HOOK_RAW(rex_original_NetDll_recvfrom) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_recvfrom);
  auto native = NativeOf(Arg(ctx, base, 1));
  if (!native) {
    rex::system::XThread::SetLastError(kWsaEnotsock);
    return Return(ctx, kSocketError);
  }
  uint32_t error = 0;
  auto got = ReceiveDatagram(*native, {{Arg(ctx, base, 2), Arg(ctx, base, 3)}},
                             Arg(ctx, base, 5), Arg(ctx, base, 6), &error);
  if (!got) {
    rex::system::XThread::SetLastError(error);
    return Return(ctx, kSocketError);
  }
  ++g_io.received;
  Return(ctx, *got);
}

// WSARecvFrom(caller, socket, buffers, count, received, flags, from, fromlen, overlapped,
// routine)
REX_HOOK_RAW(rex_original_NetDll_WSARecvFrom) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_WSARecvFrom);
  const uint32_t socket = Arg(ctx, base, 1);
  auto native = NativeOf(socket);
  if (!native) {
    rex::system::XThread::SetLastError(kWsaEnotsock);
    return Return(ctx, kSocketError);
  }
  PendingReceives::Op op;
  op.socket = socket;
  op.buffers = GuestBuffers(Arg(ctx, base, 2), Arg(ctx, base, 3));
  op.from = Arg(ctx, base, 6);
  op.fromlen = Arg(ctx, base, 7);
  op.overlapped = Arg(ctx, base, 8);
  uint32_t error = 0;
  auto got = ReceiveDatagram(*native, op.buffers, op.from, op.fromlen, &error);
  if (got) {
    ++g_io.received;
    if (uint8_t* b = Guest(Arg(ctx, base, 4))) Put32(b, *got);
    if (uint8_t* f = Guest(Arg(ctx, base, 5))) Put32(f, 0);
    if (op.overlapped) CompleteOverlapped(op.overlapped, 0, *got);
    return Return(ctx, 0);
  }
  if (error == kWsaEwouldblock && op.overlapped) {
    uint8_t* o = Guest(op.overlapped);
    Put32(o, kStatusPending);
    Put32(o + 4, 0);
    ResetEvent(Be32(o + 16));
    ++g_io.posted;
    PendingReceives::Get().Add(std::move(op));
    error = kWsaIoPending;
  }
  rex::system::XThread::SetLastError(error);
  Return(ctx, kSocketError);
}

// WSAGetOverlappedResult(caller, socket, overlapped, bytes, wait, flags) -> BOOL
REX_HOOK_RAW(rex_original_NetDll_WSAGetOverlappedResult) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_WSAGetOverlappedResult);
  const uint32_t overlapped = Arg(ctx, base, 2);
  uint8_t* o = Guest(overlapped);
  if (!o) {
    rex::system::XThread::SetLastError(kWsaEinval);
    return Return(ctx, 0);
  }
  if (Be32(o) == kStatusPending) {
    if (!Arg(ctx, base, 4)) {
      rex::system::XThread::SetLastError(kWsaIoIncomplete);
      return Return(ctx, 0);
    }
    PendingReceives::Get().Wait(overlapped);
  }
  if (uint8_t* b = Guest(Arg(ctx, base, 3))) Put32(b, Be32(o + 4));
  if (uint8_t* f = Guest(Arg(ctx, base, 5))) Put32(f, 0);
  if (Be32(o) != 0) {
    rex::system::XThread::SetLastError(Be32(o));
    return Return(ctx, 0);
  }
  Return(ctx, 1);
}

// closesocket(caller, socket): pending receives on it end with WSA_OPERATION_ABORTED.
REX_HOOK_RAW(rex_original_NetDll_closesocket) {
  const uint32_t socket = Arg(ctx, base, 1);
  if (Enabled()) PendingReceives::Get().Cancel(socket);
  HT_FORWARD(rex_original_NetDll_closesocket);
  if (Enabled()) HT_TRACE("closesocket({:X}) -> {:X}", socket, ctx.r3.u32);
}

// Pass-through calls, logged with ht_lan_trace so a bug report shows the whole
// conversation between the game and its sockets.
#define HT_TRACED_FORWARD(name, label)                                                 \
  REX_HOOK_RAW(rex_original_NetDll_##name) {                                           \
    const uint32_t a1 = ctx.r4.u32, a2 = ctx.r5.u32, a3 = ctx.r6.u32, a4 = ctx.r7.u32; \
    HT_FORWARD(rex_original_NetDll_##name);                                            \
    if (Enabled())                                                                     \
      HT_TRACE(label "({:X}, {:X}, {:X}, {:X}) -> {:X}", a1, a2, a3, a4, ctx.r3.u32);  \
  }
HT_TRACED_FORWARD(socket, "socket")
HT_TRACED_FORWARD(setsockopt, "setsockopt")
HT_TRACED_FORWARD(ioctlsocket, "ioctlsocket")
HT_TRACED_FORWARD(listen, "listen")
HT_TRACED_FORWARD(XNetStartup, "XNetStartup")
HT_TRACED_FORWARD(XNetQosServiceLookup, "XNetQosServiceLookup")
HT_TRACED_FORWARD(XNetQosRelease, "XNetQosRelease")

// accept(caller, socket, addr, addrlen): connections from outside the LAN are closed.
REX_HOOK_RAW(rex_original_NetDll_accept) {
  if (!Enabled()) return HT_FORWARD(rex_original_NetDll_accept);
  uint8_t* addr = Guest(Arg(ctx, base, 2));
  const PPCContext args = ctx;
  HT_FORWARD(rex_original_NetDll_accept);
  if (ctx.r3.u32 == kSocketError || !addr) return;
  const uint32_t ip = Be32(addr + 4);
  if (AllowedSource(ip)) return;
  DroppedSource(ip);
  PPCContext close = args;
  close.r4.u64 = ctx.r3.u32;
  {
    static GuestFunction closesocket = Original("rex_original_NetDll_closesocket");
    if (closesocket) closesocket(close, base);
  }
  rex::system::XThread::SetLastError(kWsaEconnaborted);
  Return(ctx, kSocketError);
}

// --- sign-in and privileges ----------------------------------------------------------

// A console on LIVE tells each new LIVE notification listener that the connection is
// up; without it the game answers Create Match with "You must be connected to Xbox
// LIVE in order to play online".
REX_HOOK_RAW(rex_original_XamNotifyCreateListener) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamNotifyCreateListener);
  const uint64_t mask = ctx.r3.u64;
  HT_FORWARD(rex_original_XamNotifyCreateListener);
  constexpr uint64_t kNotifyLive = 0x02;
  constexpr uint32_t kLiveConnectionChanged = 0x02000001;
  constexpr uint32_t kLogonConnectionEstablished = 0x001510F0;
  if (!(mask & kNotifyLive)) return;
  if (auto listener =
          REX_KERNEL_OBJECTS()->LookupObject<rex::system::XNotifyListener>(ctx.r3.u32)) {
    listener->EnqueueNotification(kLiveConnectionChanged, kLogonConnectionEstablished);
    HT_TRACE("LIVE listener {:08X}: connection established", ctx.r3.u32);
  }
}

REX_HOOK_RAW(rex_original_XamUserGetSigninState) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamUserGetSigninState);
  const uint32_t user = ctx.r3.u32;
  HT_FORWARD(rex_original_XamUserGetSigninState);
  if (ctx.r3.u32 && UserExists(user)) Return(ctx, 2);  // eXUserSigninState_SignedInToLive
}

// XamUserGetSigninInfo(user, flags, info): info = { be64 xuid, u32, be32 state, ... }
REX_HOOK_RAW(rex_original_XamUserGetSigninInfo) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamUserGetSigninInfo);
  const uint32_t user = ctx.r3.u32, flags = ctx.r4.u32;
  uint8_t* info = Guest(ctx.r5.u32);
  HT_FORWARD(rex_original_XamUserGetSigninInfo);
  if (ctx.r3.u32 != 0 || !info || !UserExists(user) || !Be32(info + 12)) return;
  Put32(info + 12, 2);
  // A LIVE-signed-in profile reports its online XUID unless the offline one is asked
  // for (flags 2, which the game uses alongside 0). Every copy of the port shares the
  // same offline XUIDs, so players on different machines need these to tell apart.
  if (!(flags & 2)) Put64(info, Lan::Get().OnlineXuid(user));
  HT_TRACE("XamUserGetSigninInfo({}, {:X}) xuid={:016X}", user, flags, Be64(info));
}

// XamUserGetXUID(user, type_mask, out): a LIVE-signed-in profile answers any request
// that allows an online XUID (mask bit 2) with it, like the console does; the game
// identifies lobby players by it.
REX_HOOK_RAW(rex_original_XamUserGetXUID) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamUserGetXUID);
  const uint32_t user = ctx.r3.u32, mask = ctx.r4.u32;
  uint8_t* out = Guest(ctx.r5.u32);
  HT_FORWARD(rex_original_XamUserGetXUID);
  if (ctx.r3.u32 == 0 && out && UserExists(user) && (mask & 2))
    Put64(out, Lan::Get().OnlineXuid(user));
  HT_TRACE("XamUserGetXUID({}, {}) = {:016X}", user, mask, out ? Be64(out) : 0);
}

// XamUserCheckPrivilege(user, privilege, out)
REX_HOOK_RAW(rex_original_XamUserCheckPrivilege) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamUserCheckPrivilege);
  const uint32_t privilege = ctx.r4.u32;
  uint8_t* out = Guest(ctx.r5.u32);
  HT_FORWARD(rex_original_XamUserCheckPrivilege);
  if (ctx.r3.u32 != 0 || !out) return;
  // Grant everything except the "friends only" restrictions, which are true when the
  // account is limited.
  constexpr uint32_t kFriendsOnly[] = {234, 243, 246, 248, 251};
  const bool restriction = std::find(std::begin(kFriendsOnly), std::end(kFriendsOnly),
                                     privilege) != std::end(kFriendsOnly);
  Put32(out, restriction ? 0 : 1);
  HT_TRACE("XamUserCheckPrivilege({}, {}) = {}", ctx.r3.u32, privilege, Be32(out));
}

// --- XSession ------------------------------------------------------------------------

REX_HOOK_RAW(rex_original_XamSessionCreateHandle) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamSessionCreateHandle);
  uint8_t* out = Guest(ctx.r3.u32);
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  Session s;
  s.handle = lan.next_handle++;
  s.object = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
  std::memset(Guest(s.object), 0, 16);
  Put32(Guest(s.object), s.handle);
  lan.handles[s.handle] = s.object;
  lan.sessions[s.object] = s;
  if (out) Put32(out, s.handle);
  HT_TRACE("XamSessionCreateHandle -> {:08X} (object {:08X})", s.handle, s.object);
  Return(ctx, kSuccess);
}

REX_HOOK_RAW(rex_original_XamSessionRefObjByHandle) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamSessionRefObjByHandle);
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  auto it = lan.handles.find(ctx.r3.u32);
  if (it == lan.handles.end()) return Return(ctx, 6);  // ERROR_INVALID_HANDLE
  if (uint8_t* out = Guest(ctx.r4.u32)) Put32(out, it->second);
  Return(ctx, kSuccess);
}

namespace {

void WriteSearchResults(Lan& lan, const std::vector<ht::lan::RemoteSession>& found,
                        uint32_t max_results, uint32_t buffer, uint32_t size) {
  uint8_t* out = Guest(buffer);
  if (!out || size < 8) return;
  std::memset(out, 0, size);
  const uint32_t count = std::min<uint32_t>(
      {uint32_t(found.size()), max_results, (size - 8) / kSearchResultSize});
  Put32(out, count);
  Put32(out + 4, count ? buffer + 8 : 0);
  uint32_t extra = buffer + 8 + count * kSearchResultSize;  // contexts/properties after
  const uint32_t end = buffer + size;
  for (uint32_t i = 0; i < count; ++i) {
    const auto& ad = found[i].ad;
    uint8_t* r = out + 8 + i * kSearchResultSize;
    std::memcpy(r, ad.session_id.data(), 8);
    std::memcpy(r + 8, ad.host_xnaddr.data(), kXnAddrSize);
    std::memcpy(r + 44, ad.key.data(), 16);
    Put32(r + 60, ad.max_public - ad.filled_public);
    Put32(r + 64, ad.max_private - ad.filled_private);
    Put32(r + 68, ad.filled_public);
    Put32(r + 72, ad.filled_private);
    std::array<uint8_t, kXnAddrSize> x;
    std::memcpy(x.data(), ad.host_xnaddr.data(), x.size());
    lan.Remember(x);
    // The title's own properties, plus X_PROPERTY_GAMER_HOSTNAME, which LIVE adds to
    // every result and the game shows as the host's gamertag in the match list.
    const uint32_t props = uint32_t(ad.properties.size()) + 1;
    const uint32_t ctxs = uint32_t(ad.contexts.size());
    const uint32_t name_bytes = (uint32_t(ad.host_name.size()) + 1) * 2;  // UTF-16, with NUL
    const uint32_t needed =
        props * kPropertySize + ctxs * kContextSize + ((name_bytes + 3) & ~3u);
    if (extra + needed <= end) {
      Put32(r + 76, props);
      Put32(r + 80, ctxs);
      Put32(r + 84, extra);
      const uint32_t name_at = extra + props * kPropertySize + ctxs * kContextSize;
      for (const auto& p : ad.properties) {
        uint8_t* e = Guest(extra);
        Put32(e, p.id);
        e[8] = p.type;
        Put64(e + 16, p.value);
        extra += kPropertySize;
      }
      uint8_t* host = Guest(extra);
      Put32(host, 0x40008109);  // X_PROPERTY_GAMER_HOSTNAME
      host[8] = 4;              // XUSER_DATA_TYPE_UNICODE: {be32 bytes, be32 pointer}
      Put32(host + 16, name_bytes);
      Put32(host + 20, name_at);
      extra += kPropertySize;
      Put32(r + 88, ctxs ? extra : 0);
      for (const auto& [id, value] : ad.contexts) {
        Put32(Guest(extra), id);
        Put32(Guest(extra) + 4, value);
        extra += kContextSize;
      }
      uint8_t* name = Guest(name_at);
      for (size_t c = 0; c < ad.host_name.size(); ++c) Put16(name + c * 2, uint8_t(ad.host_name[c]));
      Put16(name + ad.host_name.size() * 2, 0);
      extra = name_at + ((name_bytes + 3) & ~3u);
    }
  }
}

// Searching waits up to 0.7 s for answers; the LAN lock is released meanwhile so the
// game's other network calls are not held up.
std::vector<ht::lan::RemoteSession> OpenSessions(Lan& lan, uint32_t wanted,
                                                 std::unique_lock<std::mutex>& lock) {
  if (!lan.StartDiscovery()) return {};
  lock.unlock();
  auto found = lan.discovery.Search(std::chrono::milliseconds(700), wanted ? wanted : 1);
  lock.lock();
  found.erase(std::remove_if(found.begin(), found.end(),
                             [](const auto& s) {
                               return s.ad.filled_public >= s.ad.max_public &&
                                      s.ad.filled_private >= s.ad.max_private;
                             }),
              found.end());
  return found;
}

// XSESSION_LOCAL_DETAILS followed by the member array, as XSessionGetDetails returns it.
uint32_t WriteDetails(Lan& lan, Session& s, uint32_t buffer, uint32_t size) {
  uint8_t* d = Guest(buffer);
  if (!d || size < kLocalDetailsSize) return kXEInvalidArg;
  std::memset(d, 0, kLocalDetailsSize);
  uint32_t filled_public = 0, filled_private = 0;
  for (const auto& m : s.members) (m.private_slot ? filled_private : filled_public)++;
  if (!s.host)
    if (auto remote = lan.discovery.Find([&] {
          ht::lan::SessionId id;
          std::memcpy(id.data(), &s.info[0], 8);
          return id;
        }()))
      filled_public = std::max(filled_public, remote->ad.filled_public);
  Put32(d + 0x00, s.user_index);
  Put32(d + 0x04, s.game_type);
  Put32(d + 0x08, s.game_mode);
  Put32(d + 0x0C, s.flags);
  Put32(d + 0x10, s.max_public);
  Put32(d + 0x14, s.max_private);
  Put32(d + 0x18, s.max_public - std::min(filled_public, s.max_public));
  Put32(d + 0x1C, s.max_private - std::min(filled_private, s.max_private));
  const uint32_t members = uint32_t(s.members.size());
  const uint32_t room = (size - kLocalDetailsSize) / kMemberSize;
  const uint32_t returned = std::min(members, room);
  Put32(d + 0x20, members);
  Put32(d + 0x24, returned);
  Put32(d + 0x28, s.state);
  Put64(d + 0x30, s.nonce);
  std::memcpy(d + 0x38, s.info.data(), kSessionInfoSize);
  Put32(d + 0x7C, returned ? buffer + kLocalDetailsSize : 0);
  for (uint32_t i = 0; i < returned; ++i) {
    uint8_t* m = d + kLocalDetailsSize + i * kMemberSize;
    Put64(m, s.members[i].xuid);
    Put32(m + 8, s.members[i].user_index);
    Put32(m + 12, s.members[i].private_slot ? 1 : 0);
  }
  return kSuccess;
}

std::optional<uint32_t> HandleSession(uint32_t message, uint32_t buffer_ptr, uint32_t length) {
  uint8_t* b = Guest(buffer_ptr);
  if (!b) return std::nullopt;
  auto& lan = Lan::Get();
  std::unique_lock<std::mutex> lock(lan.mutex);

  switch (message) {
    case 0x000B0006: {  // XUserSetContext: user @0, id @16, value @20
      const uint32_t user = Be32(b);
      if (user < 4) {
        lan.users[user].contexts[Be32(b + 16)] = Be32(b + 20);
        if (Be32(b + 16) == 0x00008001) lan.presence[user] = Be32(b + 20);  // rich presence: screen
        if (Be32(b + 16) == 0x0000800A) for (auto& [o, s] : lan.sessions) if (s.user_index == user) s.game_type = Be32(b + 20);
        if (Be32(b + 16) == 0x0000800B) for (auto& [o, s] : lan.sessions) if (s.user_index == user) s.game_mode = Be32(b + 20);
      }
      HT_TRACE("XUserSetContext({}, {:08X}, {})", user, Be32(b + 16), Be32(b + 20));
      return std::nullopt;  // the runtime handles it too
    }
    case 0x000B0007: {  // XUserSetProperty: user @0, id @16, size @20, value ptr @24
      const uint32_t user = Be32(b), id = Be32(b + 16), size = Be32(b + 20);
      const uint8_t* value = Guest(Be32(b + 24));
      if (user < 4 && value && (size == 4 || size == 8) &&
          ht::lan::IsNumericPropertyType(uint8_t(id >> 28))) {
        ht::lan::Property p;
        p.id = id;
        p.type = uint8_t(id >> 28);  // XUSER_DATA_TYPE lives in the top nibble of the id
        p.value = size == 4 ? uint64_t(Be32(value)) << 32 : Be64(value);
        lan.users[user].properties[id] = p;
      }
      HT_TRACE("XUserSetProperty({}, {:08X}, {} bytes)", user, id, size);
      return std::nullopt;
    }
    case 0x000B0016:    // XSessionSearch
    case 0x000B001C: {  // XSessionSearchEx
      const uint32_t max = Be32(b + 8), size = Be32(b + 24), results = Be32(b + 28);
      HT_TRACE("XSessionSearch proc={} user={} max={} props={} ctx={}", Be32(b), Be32(b + 4), max,
               Be16(b + 12), Be16(b + 14));
      lan.join_list = true;
      auto found = OpenSessions(lan, max, lock);
      WriteSearchResults(lan, found, max, results, size);
      REXLOG_WARN("[ht-lan] search found {} LAN game(s)", std::min<size_t>(found.size(), max));
      return kSuccess;
    }
    case 0x000B001B: {  // XSessionSearchByID: user @0, xnkid @4, size @12, results @16
      ht::lan::SessionId id;
      std::memcpy(id.data(), b + 4, 8);
      auto found = lan.discovery.Find(id);
      if (!found && lan.StartDiscovery()) {
        lock.unlock();
        lan.discovery.Search(std::chrono::milliseconds(700), SIZE_MAX);
        lock.lock();
        found = lan.discovery.Find(id);
      }
      std::vector<ht::lan::RemoteSession> list;
      if (found) list.push_back(*found);
      WriteSearchResults(lan, list, 1, Be32(b + 16), Be32(b + 12));
      HT_TRACE("XSessionSearchByID -> {}", found ? "found" : "not found");
      return kSuccess;
    }
    case 0x000B0021: {  // XUserReadStats: title, xuid count, xuids*, spec count, specs*, size, results*
      // No stats service on a LAN: every player reads as a newcomer (rank 0, all
      // columns 0). The runtime returns success without writing the results, which the
      // game then reads as garbage.
      const uint32_t xuid_count = std::min<uint32_t>(Be32(b + 4), 101);
      const uint8_t* xuids = Guest(Be32(b + 8));
      const uint32_t spec_count = std::min<uint32_t>(Be32(b + 12), 64);
      const uint8_t* specs = Guest(Be32(b + 16));
      const uint32_t size = Be32(b + 20), results = Be32(b + 24);
      uint8_t* out = Guest(results);
      if (!xuids || !specs || !out || size < 8 || !xuid_count || !spec_count) return kXEInvalidArg;
      constexpr uint32_t kSpecSize = 8 + 64 * 2, kViewSize = 0x10, kRowSize = 0x30, kColumnSize = 0x18;
      std::memset(out, 0, size);
      uint32_t at = results + 8 + spec_count * kViewSize;
      const uint32_t end = results + size;
      if (at > end) return 0x7A;  // ERROR_INSUFFICIENT_BUFFER
      Put32(out, spec_count);
      Put32(out + 4, results + 8);
      for (uint32_t v = 0; v < spec_count; ++v) {
        const uint8_t* spec = specs + v * kSpecSize;
        const uint32_t columns = std::min<uint32_t>(Be32(spec + 4), 64);
        uint8_t* view = out + 8 + v * kViewSize;
        Put32(view, Be32(spec));
        const uint32_t row_bytes = kRowSize + columns * kColumnSize;
        const uint32_t rows = std::min<uint32_t>(xuid_count, (end - at) / row_bytes);
        Put32(view + 4, rows);  // total_view_rows
        Put32(view + 8, rows);
        Put32(view + 12, rows ? at : 0);
        const uint32_t rows_at = at;
        at += rows * kRowSize;
        for (uint32_t r = 0; r < rows; ++r) {
          uint8_t* row = Guest(rows_at + r * kRowSize);
          Put64(row, Be64(xuids + r * 8));
          Put32(row + 40, columns);
          Put32(row + 44, columns ? at : 0);
          for (uint32_t c = 0; c < columns; ++c) {
            uint8_t* column = Guest(at + c * kColumnSize);
            Put16(column, Be16(spec + 8 + c * 2));
            column[8] = 2;  // XUSER_DATA_TYPE_INT64, value 0
          }
          at += columns * kColumnSize;
        }
      }
      HT_TRACE("XUserReadStats {} player(s), {} view(s)", xuid_count, spec_count);
      return kSuccess;
    }
    case 0x000B0060: {  // XSessionSearchByIds: user, count, ids, size, results
      const uint32_t count = std::min<uint32_t>(Be32(b + 4), 32);
      const uint8_t* ids = Guest(Be32(b + 8));
      lan.StartDiscovery();
      std::vector<ht::lan::RemoteSession> list;
      for (uint32_t i = 0; i < count && ids; ++i) {
        ht::lan::SessionId id;
        std::memcpy(id.data(), ids + i * 8, 8);
        if (auto s = lan.discovery.Find(id)) list.push_back(*s);
      }
      WriteSearchResults(lan, list, count, Be32(b + 16), Be32(b + 12));
      return kSuccess;
    }
    default:
      break;
  }

  // Everything below acts on a session object created by XamSessionCreateHandle.
  Session* s = lan.Find(Be32(b));
  if (!s) return std::nullopt;

  switch (message) {
    case 0x000B0010: {  // XSessionCreate: obj, flags, public, private, user, info*, nonce*
      s->flags = Be32(b + 4);
      s->max_public = std::min<uint32_t>(Be32(b + 8), 32);
      s->max_private = std::min<uint32_t>(Be32(b + 12), 32);
      s->user_index = Be32(b + 16) & 3;
      uint8_t* info = Guest(Be32(b + 20));
      uint8_t* nonce = Guest(Be32(b + 24));
      const UserData& u = lan.users[s->user_index];
      if (auto it = u.contexts.find(0x800A); it != u.contexts.end()) s->game_type = it->second;
      if (auto it = u.contexts.find(0x800B); it != u.contexts.end()) s->game_mode = it->second;
      // A session is hosted here when the game asks for it, or when it has no
      // multiplayer side at all (single-player stats/presence sessions).
      s->host = (s->flags & kSessionHost) ||
                !(s->flags & (kSessionMatchmaking | kSessionPeerNetwork)) || !info;
      if (s->host) {
        ht::lan::RandomBytes(&s->info[0], 8);
        s->info[0] = 0x80;  // XNKID_ONLINE_PEER
        const auto x = lan.LocalXnAddr();
        std::memcpy(&s->info[8], x.data(), kXnAddrSize);
        ht::lan::RandomBytes(&s->info[44], 16);
        s->nonce = ht::lan::RandomU64();
        if (info) std::memcpy(info, s->info.data(), kSessionInfoSize);
      } else {
        std::memcpy(s->info.data(), info, kSessionInfoSize);
        std::array<uint8_t, kXnAddrSize> x;
        std::memcpy(x.data(), &s->info[8], kXnAddrSize);
        lan.Remember(x);
        s->nonce = ht::lan::RandomU64();
        // Tell the host our XNet address before the game's first packet reaches it.
        if (lan.StartDiscovery()) lan.discovery.Hello(Be32(x.data()));
      }
      if (nonce) Put64(nonce, s->nonce);
      s->created = true;
      lan.join_list = false;
      lan.Advertise(*s);
      REXLOG_WARN("[ht-lan] session {:016X} {} (flags {:X}, {}+{} slots)", Be64(&s->info[0]),
                  s->host ? "hosted" : "joined", s->flags, s->max_public, s->max_private);
      return kSuccess;
    }
    case 0x000B0011: {  // XSessionDelete
      lan.Unadvertise(*s);
      s->deleted = true;
      s->state = 4;
      HT_TRACE("XSessionDelete {:016X}", Be64(&s->info[0]));
      return kSuccess;
    }
    case 0x000B0012:    // XSessionJoin(Local|Remote): obj, count, xuids*, indices*, private*
    case 0x000B0013: {  // XSessionLeave(Local|Remote)
      const uint32_t count = std::min<uint32_t>(Be32(b + 4), 32);
      const uint8_t* xuids = Guest(Be32(b + 8));
      const uint8_t* indices = Guest(Be32(b + 12));
      const uint8_t* priv = Guest(Be32(b + 16));
      if (!xuids == !indices) return kXEInvalidArg;
      for (uint32_t i = 0; i < count; ++i) {
        Member m;
        if (indices) {
          m.user_index = Be32(indices + i * 4) & 3;
          m.xuid = lan.OnlineXuid(m.user_index);
        } else {
          m.xuid = Be64(xuids + i * 8);
        }
        m.private_slot = priv && priv[i * 4 + 3];
        auto same = [&](const Member& o) { return o.xuid == m.xuid; };
        if (message == 0x000B0012) {
          if (std::find_if(s->members.begin(), s->members.end(), same) != s->members.end()) continue;
          if (s->members.size() >= s->max_public + s->max_private) return kXOnlineSessionFull;
          s->members.push_back(m);
        } else {
          s->members.erase(std::remove_if(s->members.begin(), s->members.end(), same),
                           s->members.end());
        }
      }
      HT_TRACE("XSession{} {} -> {} member(s)", message == 0x000B0012 ? "Join" : "Leave", count,
               s->members.size());
      if (s->advertised) lan.Advertise(*s);
      return kSuccess;
    }
    case 0x000B0014:  // XSessionStart
      s->state = 2;
      HT_TRACE("XSessionStart");
      return kSuccess;
    case 0x000B0015:  // XSessionEnd
      s->state = 3;
      HT_TRACE("XSessionEnd");
      return kSuccess;
    case 0x000B0018: {  // XSessionModify: obj, flags, public, private
      s->flags = Be32(b + 4) | (s->flags & kSessionHost);
      s->max_public = std::min<uint32_t>(Be32(b + 8), 32);
      s->max_private = std::min<uint32_t>(Be32(b + 12), 32);
      HT_TRACE("XSessionModify flags={:X} {}+{}", s->flags, s->max_public, s->max_private);
      if (s->advertised || s->host) lan.Advertise(*s);
      return kSuccess;
    }
    case 0x000B001D:  // XSessionGetDetails: obj, size, details*
      return WriteDetails(lan, *s, Be32(b + 8), Be32(b + 4));
    case 0x000B001E: {  // XSessionMigrateHost: obj, info*, user
      uint8_t* info = Guest(Be32(b + 4));
      if (!(s->flags & kSessionHost) && Be32(b + 8) < 4) {
        // This machine becomes the host: new identity, same session id.
        s->host = true;
        s->user_index = Be32(b + 8);
        const auto x = lan.LocalXnAddr();
        std::memcpy(&s->info[8], x.data(), kXnAddrSize);
        ht::lan::RandomBytes(&s->info[44], 16);
        lan.Advertise(*s);
      }
      if (info) std::memcpy(info, s->info.data(), kSessionInfoSize);
      REXLOG_WARN("[ht-lan] host migration: {}", s->host ? "now hosting" : "following new host");
      return kSuccess;
    }
    case 0x000B001A: {  // XSessionArbitrationRegister: obj, flags, nonce, duration, size, results*
      const uint32_t size = Be32(b + 20);
      uint8_t* r = Guest(Be32(b + 24));
      if (!r || size < 8) return kXEInvalidArg;
      // One registrant per member: XSESSION_REGISTRANT {be64 machine, be32 trust,
      // be32 users, be32 users*} followed by the XUID it lists.
      const uint32_t n = std::min<uint32_t>(uint32_t(s->members.size()), (size - 8) / (0x18 + 8));
      std::memset(r, 0, size);
      Put32(r, n);
      Put32(r + 4, n ? Be32(b + 24) + 8 : 0);
      for (uint32_t i = 0; i < n; ++i) {
        uint8_t* reg = r + 8 + i * 0x18;
        const uint32_t xuid_at = Be32(b + 24) + 8 + n * 0x18 + i * 8;
        Put64(reg, s->members[i].xuid);
        Put32(reg + 8, 1);
        Put32(reg + 12, 1);
        Put32(reg + 16, xuid_at);
        Put64(Guest(xuid_at), s->members[i].xuid);
      }
      return kSuccess;
    }
    case 0x000B001F:  // XSessionModifySkill
    case 0x000B0025:  // XSessionWriteStats
    case 0x000B0026:  // XSessionFlushStats
      return kSuccess;
    default:
      return std::nullopt;
  }
}

// Mirrors the runtime's XMsgStartIORequestEx completion rules.
uint32_t CompleteIo(uint32_t result, uint32_t overlapped) {
  if (overlapped) {
    REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped, result);
    result = kErrorIoPending;
  }
  if (result == kSuccess || result == kErrorIoPending) rex::system::XThread::SetLastError(0);
  return result;
}

}  // namespace

// XMsgStartIORequest(app, message, overlapped, buffer, length)
REX_HOOK_RAW(rex_original_XMsgStartIORequest) {
  if (Enabled() && ctx.r3.u32 == 0xFB) {
    if (auto result = HandleSession(ctx.r4.u32, ctx.r6.u32, ctx.r7.u32))
      return Return(ctx, CompleteIo(*result, ctx.r5.u32));
    HT_TRACE("XGI message {:08X} -> runtime", ctx.r4.u32);
  }
  HT_FORWARD(rex_original_XMsgStartIORequest);
}

// XMsgInProcessCall(app, message, buffer, length)
REX_HOOK_RAW(rex_original_XMsgInProcessCall) {
  if (Enabled() && ctx.r3.u32 == 0xFB) {
    if (auto result = HandleSession(ctx.r4.u32, ctx.r5.u32, ctx.r6.u32))
      return Return(ctx, *result);
    HT_TRACE("XGI in-process message {:08X} -> runtime", ctx.r4.u32);
  }
  HT_FORWARD(rex_original_XMsgInProcessCall);
}

// --- in-game LAN screens (see ht_lan_ui.h) ---------------------------------------------

namespace ht::lan {

bool PlayEnabled() { return Enabled(); }

bool InMultiplayerMenus() {
  if (!Enabled()) return false;
  auto& lan = Lan::Get();
  // 5 on every Multiplayer screen. The second player turns 5 too once a split-screen
  // race starts; an online lobby or race has a peer-network session.
  if (lan.presence[0] != 5 || lan.presence[1] == 5) return false;
  std::lock_guard<std::mutex> lock(lan.mutex);
  for (const auto& [object, s] : lan.sessions)
    if (s.created && !s.deleted && (s.flags & kSessionPeerNetwork)) return false;
  return true;
}

bool OnJoinList() { return Lan::Get().join_list; }
void LeaveJoinList() { Lan::Get().join_list = false; }

void JoinAddress(uint32_t ip) {
  auto& lan = Lan::Get();
  std::lock_guard<std::mutex> lock(lan.mutex);
  if (!lan.StartDiscovery()) return;
  lan.discovery.AddTrustedPeer(ip);
  REXLOG_WARN("[ht-lan] looking for games at {}", AddressToString(ip));
}


}  // namespace ht::lan

// XamInputGetState(user, flags, state): X_INPUT_STATE = {be32 packet, X_INPUT_GAMEPAD}.
// The LAN screens read the pad here and the game sees an idle one while they are open.
REX_HOOK_RAW(rex_original_XamInputGetState) {
  if (!Enabled()) return HT_FORWARD(rex_original_XamInputGetState);
  const uint32_t user = ctx.r3.u32;
  uint8_t* state = Guest(ctx.r5.u32);
  HT_FORWARD(rex_original_XamInputGetState);
  if (ctx.r3.u32 == 0 && state && user < 4) ht::lan::FilterPad(user, state + 4);
}
