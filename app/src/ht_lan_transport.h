#pragma once
// LAN play transport: session discovery on the local network.
//
// The game only knows Xbox LIVE. ht_lan_xam.cpp answers its LIVE calls (sessions,
// XNet addresses, QoS) from this layer instead, so players on one network find each
// other without any server. This file has no SDK dependency, so the protocol and its
// validation are unit-tested on their own (app/tests/ht_lan_transport_test.cpp).
//
// Protocol: small UDP datagrams on one port (ht_lan_port, default 36000).
//   QUERY     a player searching for games; hosts answer with ANNOUNCE.
//   ANNOUNCE  a hosted session; also broadcast every second while it is open.
//   LEAVE     the host closed the session.
//   HELLO     a player joining a host (and the host answering) gives its XNet address,
//             which the game asks for when the first game packet arrives.
// Every datagram starts with a fixed header (magic "HTLN", version, type, title id,
// payload length) and is checked field by field before use. Only private and
// link-local senders are accepted unless the player allows any address.
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ht::lan {

// --- Addresses ---------------------------------------------------------------
// All IPv4 addresses in this API are host byte order.

// 10/8, 172.16/12, 192.168/16, 169.254/16 (link-local) and 127/8 (same machine).
bool IsLanAddress(uint32_t ip);
std::string AddressToString(uint32_t ip);
// Parses dotted IPv4; returns nullopt for anything else.
std::optional<uint32_t> ParseAddress(const std::string& text);

struct Interface {
  uint32_t address = 0;
  uint32_t broadcast = 0;
};
// IPv4 interfaces that are up, excluding loopback.
std::vector<Interface> LocalInterfaces();
// The address other players should use to reach this machine: `preferred` when it is
// set and valid, else the interface of the default route when it is a LAN address,
// else the first LAN interface, else 127.0.0.1.
uint32_t PickLocalAddress(const std::string& preferred);

// --- Ports -------------------------------------------------------------------
// The game binds fixed console ports (1000 and up). Those are privileged on Linux and
// may collide with other software, so each guest port is moved into the player's
// range: guest 1000+k <-> host base+1+k for k < kGamePorts. Port 0 (any) and ports
// outside the console range pass through unchanged.
struct PortMap {
  static constexpr uint16_t kGuestFirst = 1000;
  static constexpr uint16_t kGamePorts = 16;
  uint16_t base = 36000;
  uint16_t discovery() const { return base; }
  uint16_t ToHost(uint16_t guest) const;
  uint16_t ToGuest(uint16_t host) const;
};
// Valid when base..base+kGamePorts fits below 65536 and base is not privileged.
bool IsUsableBasePort(int base);

// --- Wire format -------------------------------------------------------------

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kHeaderSize = 16;
constexpr size_t kMaxDatagram = 1200;
constexpr size_t kMaxContexts = 16;
constexpr size_t kMaxProperties = 32;
constexpr size_t kMaxQosData = 512;
constexpr size_t kMaxHostName = 15;
constexpr size_t kXnAddrSize = 36;
constexpr size_t kMaxPeers = 64;
constexpr size_t kMaxSessions = 64;  // a LAN flood of announces cannot grow the table further
constexpr size_t kMaxSessionsPerSender = 4;

enum class MessageType : uint8_t { kQuery = 1, kAnnounce = 2, kLeave = 3, kHello = 4 };

using XnAddr = std::array<uint8_t, kXnAddrSize>;

using SessionId = std::array<uint8_t, 8>;

// XUSER_DATA types that are plain values: INT32 1, INT64 2, DOUBLE 3, FLOAT 5, DATETIME 7.
inline bool IsNumericPropertyType(uint8_t type) {
  return type == 1 || type == 2 || type == 3 || type == 5 || type == 7;
}

struct Property {
  uint32_t id = 0;
  uint64_t value = 0;  // raw 8 bytes of the XUSER_DATA union (numeric types only)
  uint8_t type = 0;    // XUSER_DATA_TYPE
};

struct SessionAd {
  uint64_t instance = 0;  // random per running game, identifies the sender
  SessionId session_id{};
  std::array<uint8_t, 16> key{};                  // XNKEY key exchange key
  std::array<uint8_t, kXnAddrSize> host_xnaddr{};  // guest XNADDR bytes, big-endian
  uint32_t flags = 0;
  uint32_t game_type = 0;
  uint32_t game_mode = 0;
  uint32_t max_public = 0, max_private = 0;
  uint32_t filled_public = 0, filled_private = 0;
  uint64_t host_xuid = 0;
  std::vector<std::pair<uint32_t, uint32_t>> contexts;
  std::vector<Property> properties;
  std::vector<uint8_t> qos;  // XNetQosListen data, handed to searchers by XNetQosLookup
  std::string host_name;
};

struct Message {
  MessageType type = MessageType::kQuery;
  uint64_t instance = 0;
  SessionId session_id{};  // kLeave
  SessionAd ad;            // kAnnounce
  XnAddr xnaddr{};         // kHello
};

std::vector<uint8_t> EncodeQuery(uint32_t title_id, uint64_t instance);
// Returns empty when the ad does not fit (too many contexts, properties or QoS bytes).
std::vector<uint8_t> EncodeAnnounce(uint32_t title_id, const SessionAd& ad);
std::vector<uint8_t> EncodeLeave(uint32_t title_id, uint64_t instance, const SessionId& id);
std::vector<uint8_t> EncodeHello(uint32_t title_id, uint64_t instance, const XnAddr& xnaddr);
// Strict: wrong magic, version, title, type, any length or count out of range, or
// trailing bytes all reject the datagram.
std::optional<Message> Decode(const uint8_t* data, size_t size, uint32_t title_id);

// --- Discovery service ---------------------------------------------------------

struct RemoteSession {
  SessionAd ad;
  uint32_t source = 0;  // sender address (host order)
  std::chrono::steady_clock::time_point last_seen;
};

struct DiscoveryStats {
  uint64_t received = 0;
  uint64_t accepted = 0;
  uint64_t rejected_source = 0;  // not a LAN address
  uint64_t rejected_malformed = 0;
  uint64_t sent = 0;
};

class Discovery {
 public:
  struct Options {
    uint16_t port = 36000;      // listen port, and destination port of broadcasts
    uint32_t bind_address = 0;  // listen on this address only (0 = every interface)
    uint32_t title_id = 0;
    uint64_t instance = 0;      // 0 = pick a random one
    bool allow_any_source = false;
    bool broadcast = true;      // send to every LAN interface's broadcast address
    // Extra unicast destinations (address, port): players can list IPs when their
    // network drops broadcasts; tests use it to talk over loopback.
    std::vector<std::pair<uint32_t, uint16_t>> peers;
    std::chrono::milliseconds announce_interval{1000};
    std::chrono::milliseconds expiry{4000};
  };

  Discovery() = default;
  ~Discovery();
  Discovery(const Discovery&) = delete;
  Discovery& operator=(const Discovery&) = delete;

  bool Start(const Options& options, std::string* error = nullptr);
  void Stop();
  bool running() const { return running_; }
  uint64_t instance() const { return options_.instance; }

  // Hosting: the ad is announced now, then every announce_interval and to queries.
  void Advertise(const SessionAd& ad);
  void Unadvertise(const SessionId& id);
  bool advertising() const;

  // Searching: sends a query and waits up to `wait` (returns early once `enough`
  // sessions are known). Results are the live table, newest first.
  std::vector<RemoteSession> Search(std::chrono::milliseconds wait, size_t enough = SIZE_MAX);
  std::vector<RemoteSession> Snapshot();
  std::optional<RemoteSession> Find(const SessionId& id);
  std::optional<RemoteSession> FindByAddress(uint32_t source);

  // Peers: our XNet address goes out in HELLO; theirs are remembered by sender address.
  void SetLocalXnAddr(const XnAddr& xnaddr);
  void Hello(uint32_t address, uint16_t port = 0);  // port 0: the discovery port
  std::optional<XnAddr> FindPeer(uint32_t address);

  // An address the player typed (join by IP): queried like a peer and accepted even
  // outside the LAN ranges, so a VPN friend (Tailscale 100.x, Radmin 26.x) can play.
  void AddTrustedPeer(uint32_t address);
  bool IsTrusted(uint32_t address) const;

  DiscoveryStats stats() const;
  // Called (on the receive thread) whenever the table changes.
  void SetChangeCallback(std::function<void()> callback);

 private:
  void Run();
  void Send(const std::vector<uint8_t>& packet, std::optional<std::pair<uint32_t, uint16_t>> to);
  void Handle(const uint8_t* data, size_t size, uint32_t source, uint16_t source_port);
  void Expire();
  bool ReplyAllowed(uint32_t source, std::chrono::steady_clock::time_point now);

  Options options_;
  std::atomic<bool> running_{false};
  intptr_t socket_ = -1;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::optional<SessionAd> advertised_;
  std::vector<RemoteSession> sessions_;
  std::vector<std::pair<uint32_t, XnAddr>> peers_;  // bounded, newest last
  std::optional<XnAddr> local_xnaddr_;
  std::vector<std::pair<uint32_t, std::chrono::steady_clock::time_point>> replies_;  // bounded
  std::vector<uint32_t> trusted_;  // bounded by kMaxPeers
  DiscoveryStats stats_;
  std::function<void()> on_change_;
  std::chrono::steady_clock::time_point next_announce_{};
};

// > 0 when the socket has data within timeout_ms (poll on POSIX, select on Windows).
int WaitReadable(intptr_t socket, int timeout_ms);

uint64_t RandomU64();
void RandomBytes(uint8_t* out, size_t size);

}  // namespace ht::lan
