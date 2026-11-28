#include "ht_lan_transport.h"

#include <algorithm>
#include <cstring>
#include <random>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ht::lan {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
void CloseNative(NativeSocket s) { closesocket(s); }
bool StartSockets() {
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
void StopSockets() { WSACleanup(); }
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
void CloseNative(NativeSocket s) { close(s); }
bool StartSockets() { return true; }
void StopSockets() {}
#endif

NativeSocket Native(intptr_t s) { return static_cast<NativeSocket>(s); }

constexpr uint8_t kMagic[4] = {'H', 'T', 'L', 'N'};
// Fixed part of an ANNOUNCE payload, before the variable-length lists.
constexpr size_t kAnnounceFixed = 8 + 8 + 16 + kXnAddrSize + 4 * 7 + 8 + 1 + 1 + 2 + 1;
constexpr size_t kPropertySize = 4 + 1 + 8;
constexpr uint32_t kMaxSlots = 32;

class Writer {
 public:
  void U8(uint8_t v) { out_.push_back(v); }
  void U16(uint16_t v) { U8(uint8_t(v >> 8)), U8(uint8_t(v)); }
  void U32(uint32_t v) { U16(uint16_t(v >> 16)), U16(uint16_t(v)); }
  void U64(uint64_t v) { U32(uint32_t(v >> 32)), U32(uint32_t(v)); }
  void Bytes(const uint8_t* p, size_t n) { out_.insert(out_.end(), p, p + n); }
  std::vector<uint8_t>& data() { return out_; }

 private:
  std::vector<uint8_t> out_;
};

class Reader {
 public:
  Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  bool ok() const { return ok_; }
  bool done() const { return ok_ && pos_ == n_; }
  uint8_t U8() { return Need(1) ? p_[pos_++] : 0; }
  uint16_t U16() {
    uint16_t hi = U8();
    return uint16_t(hi << 8 | U8());
  }
  uint32_t U32() {
    uint32_t hi = U16();
    return hi << 16 | U16();
  }
  uint64_t U64() {
    uint64_t hi = U32();
    return hi << 32 | U32();
  }
  bool Bytes(uint8_t* out, size_t n) {
    if (!Need(n)) return false;
    std::memcpy(out, p_ + pos_, n);
    pos_ += n;
    return true;
  }

 private:
  bool Need(size_t n) {
    if (!ok_ || n_ - pos_ < n) ok_ = false;
    return ok_;
  }
  const uint8_t* p_;
  size_t n_;
  size_t pos_ = 0;
  bool ok_ = true;
};

std::vector<uint8_t> Frame(uint32_t title_id, MessageType type, std::vector<uint8_t> payload) {
  Writer w;
  w.Bytes(kMagic, 4);
  w.U8(kProtocolVersion);
  w.U8(uint8_t(type));
  w.U16(0);
  w.U32(title_id);
  w.U16(uint16_t(payload.size()));
  w.U16(0);
  w.Bytes(payload.data(), payload.size());
  return std::move(w.data());
}

bool PrintableName(const std::string& s) {
  return s.size() <= kMaxHostName &&
         std::all_of(s.begin(), s.end(), [](char c) { return c >= 0x20 && c <= 0x7E; });
}

std::mt19937_64& Rng() {
  thread_local std::mt19937_64 rng([] {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device(), device(), device()};
    return std::mt19937_64(seed);
  }());
  return rng;
}

}  // namespace

uint64_t RandomU64() { return Rng()(); }

void RandomBytes(uint8_t* out, size_t size) {
  for (size_t i = 0; i < size; i += 8) {
    const uint64_t v = RandomU64();
    std::memcpy(out + i, &v, std::min<size_t>(8, size - i));
  }
}

// --- Addresses -------------------------------------------------------------------

bool IsLanAddress(uint32_t ip) {
  const uint8_t a = uint8_t(ip >> 24), b = uint8_t(ip >> 16);
  return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
         (a == 169 && b == 254);
}

std::string AddressToString(uint32_t ip) {
  return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
         std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
}

std::optional<uint32_t> ParseAddress(const std::string& text) {
  uint32_t ip = 0;
  size_t pos = 0;
  for (int part = 0; part < 4; ++part) {
    if (part && (pos >= text.size() || text[pos++] != '.')) return std::nullopt;
    size_t digits = 0;
    uint32_t value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9' && digits < 3) {
      value = value * 10 + uint32_t(text[pos++] - '0');
      ++digits;
    }
    if (!digits || value > 255) return std::nullopt;
    ip = ip << 8 | value;
  }
  if (pos != text.size()) return std::nullopt;
  return ip;
}

std::vector<Interface> LocalInterfaces() {
  std::vector<Interface> result;
#if defined(_WIN32)
  ULONG size = 16 * 1024;
  std::vector<uint8_t> buffer;
  ULONG status = ERROR_BUFFER_OVERFLOW;
  for (int tries = 0; tries < 3 && status == ERROR_BUFFER_OVERFLOW; ++tries) {
    buffer.resize(size);
    status = GetAdaptersAddresses(AF_INET,
                                  GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                      GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                  &size);
  }
  if (status != NO_ERROR) return result;
  for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter;
       adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
      continue;
    for (auto* u = adapter->FirstUnicastAddress; u; u = u->Next) {
      if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
      const uint32_t ip =
          ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
      const uint8_t prefix = std::min<uint8_t>(u->OnLinkPrefixLength, 32);
      const uint32_t mask = prefix ? 0xFFFFFFFFu << (32 - prefix) : 0;
      result.push_back({ip, ip | ~mask});
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return result;
  for (ifaddrs* it = list; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
    if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
    const uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(it->ifa_addr)->sin_addr.s_addr);
    uint32_t mask = 0xFFFFFF00u;
    if (it->ifa_netmask)
      mask = ntohl(reinterpret_cast<sockaddr_in*>(it->ifa_netmask)->sin_addr.s_addr);
    result.push_back({ip, ip | ~mask});
  }
  freeifaddrs(list);
#endif
  return result;
}

uint32_t PickLocalAddress(const std::string& preferred) {
  if (auto ip = ParseAddress(preferred)) return *ip;
  // The source address the OS would use for the default route. A UDP connect sends
  // nothing; it only selects a route.
  uint32_t routed = 0;
  if (StartSockets()) {
    NativeSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != kInvalidSocket) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(9);
      to.sin_addr.s_addr = htonl(0x08080808);
      sockaddr_in local{};
      socklen_t len = sizeof(local);
      if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof(to)) == 0 &&
          getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0)
        routed = ntohl(local.sin_addr.s_addr);
      CloseNative(s);
    }
    StopSockets();
  }
  const auto interfaces = LocalInterfaces();
  for (const auto& i : interfaces)
    if (i.address == routed && IsLanAddress(routed)) return routed;
  for (const auto& i : interfaces)
    if (IsLanAddress(i.address)) return i.address;
  return 0x7F000001;
}

// --- Ports -----------------------------------------------------------------------

uint16_t PortMap::ToHost(uint16_t guest) const {
  if (guest >= kGuestFirst && guest < kGuestFirst + kGamePorts)
    return uint16_t(base + 1 + (guest - kGuestFirst));
  return guest;
}

uint16_t PortMap::ToGuest(uint16_t host) const {
  if (host > base && host <= base + kGamePorts) return uint16_t(kGuestFirst + (host - base - 1));
  return host;
}

bool IsUsableBasePort(int base) { return base >= 1024 && base + PortMap::kGamePorts <= 65535; }

// --- Wire format -----------------------------------------------------------------

std::vector<uint8_t> EncodeQuery(uint32_t title_id, uint64_t instance) {
  Writer w;
  w.U64(instance);
  return Frame(title_id, MessageType::kQuery, std::move(w.data()));
}

std::vector<uint8_t> EncodeLeave(uint32_t title_id, uint64_t instance, const SessionId& id) {
  Writer w;
  w.U64(instance);
  w.Bytes(id.data(), id.size());
  return Frame(title_id, MessageType::kLeave, std::move(w.data()));
}

std::vector<uint8_t> EncodeAnnounce(uint32_t title_id, const SessionAd& ad) {
  if (ad.contexts.size() > kMaxContexts || ad.properties.size() > kMaxProperties ||
      ad.qos.size() > kMaxQosData || !PrintableName(ad.host_name))
    return {};
  Writer w;
  w.U64(ad.instance);
  w.Bytes(ad.session_id.data(), ad.session_id.size());
  w.Bytes(ad.key.data(), ad.key.size());
  w.Bytes(ad.host_xnaddr.data(), ad.host_xnaddr.size());
  for (uint32_t v : {ad.flags, ad.game_type, ad.game_mode, ad.max_public, ad.max_private,
                     ad.filled_public, ad.filled_private})
    w.U32(v);
  w.U64(ad.host_xuid);
  w.U8(uint8_t(ad.contexts.size()));
  w.U8(uint8_t(ad.properties.size()));
  w.U16(uint16_t(ad.qos.size()));
  w.U8(uint8_t(ad.host_name.size()));
  for (const auto& [id, value] : ad.contexts) w.U32(id), w.U32(value);
  for (const auto& p : ad.properties) w.U32(p.id), w.U8(p.type), w.U64(p.value);
  w.Bytes(ad.qos.data(), ad.qos.size());
  w.Bytes(reinterpret_cast<const uint8_t*>(ad.host_name.data()), ad.host_name.size());
  auto packet = Frame(title_id, MessageType::kAnnounce, std::move(w.data()));
  if (packet.size() > kMaxDatagram) return {};
  return packet;
}

std::optional<Message> Decode(const uint8_t* data, size_t size, uint32_t title_id) {
  if (!data || size < kHeaderSize || size > kMaxDatagram) return std::nullopt;
  Reader r(data, size);
  uint8_t magic[4];
  r.Bytes(magic, 4);
  if (std::memcmp(magic, kMagic, 4) != 0) return std::nullopt;
  if (r.U8() != kProtocolVersion) return std::nullopt;
  const uint8_t type = r.U8();
  const uint16_t flags = r.U16();
  const uint32_t title = r.U32();
  const uint16_t payload = r.U16();
  const uint16_t reserved = r.U16();
  if (flags || reserved || title != title_id || payload != size - kHeaderSize) return std::nullopt;

  Message m;
  m.instance = r.U64();
  switch (MessageType(type)) {
    case MessageType::kQuery:
      m.type = MessageType::kQuery;
      break;
    case MessageType::kLeave:
      m.type = MessageType::kLeave;
      r.Bytes(m.session_id.data(), m.session_id.size());
      break;
    case MessageType::kAnnounce: {
      if (payload < kAnnounceFixed) return std::nullopt;
      m.type = MessageType::kAnnounce;
      SessionAd& ad = m.ad;
      ad.instance = m.instance;
      r.Bytes(ad.session_id.data(), ad.session_id.size());
      r.Bytes(ad.key.data(), ad.key.size());
      r.Bytes(ad.host_xnaddr.data(), ad.host_xnaddr.size());
      ad.flags = r.U32();
      ad.game_type = r.U32();
      ad.game_mode = r.U32();
      ad.max_public = r.U32();
      ad.max_private = r.U32();
      ad.filled_public = r.U32();
      ad.filled_private = r.U32();
      ad.host_xuid = r.U64();
      const size_t contexts = r.U8(), properties = r.U8(), qos = r.U16(), name = r.U8();
      if (!r.ok() || contexts > kMaxContexts || properties > kMaxProperties ||
          qos > kMaxQosData || name > kMaxHostName)
        return std::nullopt;
      if (ad.max_public > kMaxSlots || ad.max_private > kMaxSlots ||
          ad.filled_public > ad.max_public || ad.filled_private > ad.max_private)
        return std::nullopt;
      if (payload != kAnnounceFixed + contexts * 8 + properties * kPropertySize + qos + name)
        return std::nullopt;
      for (size_t i = 0; i < contexts; ++i) {
        const uint32_t id = r.U32();
        ad.contexts.emplace_back(id, r.U32());
      }
      for (size_t i = 0; i < properties; ++i) {
        Property p;
        p.id = r.U32();
        p.type = r.U8();
        p.value = r.U64();
        ad.properties.push_back(p);
      }
      ad.qos.resize(qos);
      r.Bytes(ad.qos.data(), qos);
      ad.host_name.resize(name);
      r.Bytes(reinterpret_cast<uint8_t*>(ad.host_name.data()), name);
      if (!PrintableName(ad.host_name)) return std::nullopt;
      break;
    }
    default:
      return std::nullopt;
  }
  if (!r.done() || !m.instance) return std::nullopt;
  return m;
}

// --- Discovery -------------------------------------------------------------------

Discovery::~Discovery() { Stop(); }

bool Discovery::Start(const Options& options, std::string* error) {
  Stop();
  options_ = options;
  if (!options_.instance) options_.instance = RandomU64() | 1;
  if (!StartSockets()) {
    if (error) *error = "socket startup failed";
    return false;
  }
  NativeSocket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kInvalidSocket) {
    if (error) *error = "socket() failed";
    StopSockets();
    return false;
  }
  int on = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof(on));
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(options_.port);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    if (error) *error = "UDP port " + std::to_string(options_.port) + " is in use";
    CloseNative(s);
    StopSockets();
    return false;
  }
  socket_ = intptr_t(s);
  running_ = true;
  thread_ = std::thread([this] { Run(); });
  return true;
}

void Discovery::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (advertised_) {
      // Tell searchers right away rather than letting the entry expire.
      const auto leave =
          EncodeLeave(options_.title_id, options_.instance, advertised_->session_id);
      advertised_.reset();
      Send(leave, std::nullopt);
    }
  }
  if (thread_.joinable()) thread_.join();
  CloseNative(Native(socket_));
  socket_ = -1;
  StopSockets();
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear();
}

void Discovery::Send(const std::vector<uint8_t>& packet,
                     std::optional<std::pair<uint32_t, uint16_t>> to) {
  if (packet.empty() || socket_ == -1) return;
  auto send_one = [&](uint32_t ip, uint16_t port) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = htonl(ip);
    if (sendto(Native(socket_), reinterpret_cast<const char*>(packet.data()), int(packet.size()),
               0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) >= 0)
      ++stats_.sent;
  };
  if (to) {
    send_one(to->first, to->second);
    return;
  }
  if (options_.broadcast) {
    bool any = false;
    for (const auto& i : LocalInterfaces()) {
      if (!IsLanAddress(i.address) || i.broadcast == i.address) continue;
      send_one(i.broadcast, options_.port);
      any = true;
    }
    if (!any) send_one(0xFFFFFFFFu, options_.port);
  }
  for (const auto& [ip, port] : options_.peers) send_one(ip, port);
}

void Discovery::Advertise(const SessionAd& ad) {
  std::lock_guard<std::mutex> lock(mutex_);
  advertised_ = ad;
  advertised_->instance = options_.instance;
  Send(EncodeAnnounce(options_.title_id, *advertised_), std::nullopt);
  next_announce_ = std::chrono::steady_clock::now() + options_.announce_interval;
}

void Discovery::Unadvertise(const SessionId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!advertised_ || advertised_->session_id != id) return;
  advertised_.reset();
  Send(EncodeLeave(options_.title_id, options_.instance, id), std::nullopt);
}

bool Discovery::advertising() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return advertised_.has_value();
}

std::vector<RemoteSession> Discovery::Search(std::chrono::milliseconds wait, size_t enough) {
  std::unique_lock<std::mutex> lock(mutex_);
  Send(EncodeQuery(options_.title_id, options_.instance), std::nullopt);
  changed_.wait_for(lock, wait, [&] { return !running_ || sessions_.size() >= enough; });
  auto result = sessions_;
  std::sort(result.begin(), result.end(),
            [](const auto& a, const auto& b) { return a.last_seen > b.last_seen; });
  return result;
}

std::vector<RemoteSession> Discovery::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_;
}

std::optional<RemoteSession> Discovery::Find(const SessionId& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& s : sessions_)
    if (s.ad.session_id == id) return s;
  return std::nullopt;
}

std::optional<RemoteSession> Discovery::FindByAddress(uint32_t source) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& s : sessions_)
    if (s.source == source) return s;
  return std::nullopt;
}

DiscoveryStats Discovery::stats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return stats_;
}

void Discovery::SetChangeCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_change_ = std::move(callback);
}

void Discovery::Run() {
  std::vector<uint8_t> buffer(kMaxDatagram + 1);
  while (running_) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(Native(socket_), &read_set);
    timeval timeout{0, 100 * 1000};
    const int ready =
        select(int(Native(socket_)) + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready > 0) {
      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      const int got = recvfrom(Native(socket_), reinterpret_cast<char*>(buffer.data()),
                               int(buffer.size()), 0, reinterpret_cast<sockaddr*>(&from),
                               &from_len);
      // got > kMaxDatagram means the datagram was larger than the protocol allows
      // (Linux truncates it). Windows reports such a datagram as WSAEMSGSIZE instead.
      if (got >= 0) {
        Handle(buffer.data(), size_t(got), ntohl(from.sin_addr.s_addr), ntohs(from.sin_port));
      }
#if defined(_WIN32)
      else if (WSAGetLastError() == WSAEMSGSIZE) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++stats_.received;
        ++stats_.rejected_malformed;
      }
#endif
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (advertised_ && now >= next_announce_) {
      Send(EncodeAnnounce(options_.title_id, *advertised_), std::nullopt);
      next_announce_ = now + options_.announce_interval;
    }
    Expire();
  }
}

void Discovery::Handle(const uint8_t* data, size_t size, uint32_t source, uint16_t source_port) {
  std::function<void()> notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.received;
    if (!options_.allow_any_source && !IsLanAddress(source)) {
      ++stats_.rejected_source;
      return;
    }
    auto message = Decode(data, size, options_.title_id);
    if (!message) {
      ++stats_.rejected_malformed;
      return;
    }
    if (message->instance == options_.instance) return;  // our own broadcast
    ++stats_.accepted;
    const auto now = std::chrono::steady_clock::now();
    switch (message->type) {
      case MessageType::kQuery:
        if (advertised_)
          Send(EncodeAnnounce(options_.title_id, *advertised_),
               std::make_pair(source, source_port));
        return;
      case MessageType::kAnnounce: {
        auto it = std::find_if(sessions_.begin(), sessions_.end(), [&](const auto& s) {
          return s.ad.session_id == message->ad.session_id;
        });
        if (it == sessions_.end()) {
          sessions_.push_back({std::move(message->ad), source, now});
        } else {
          it->ad = std::move(message->ad);
          it->source = source;
          it->last_seen = now;
        }
        break;
      }
      case MessageType::kLeave:
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                       [&](const auto& s) {
                                         return s.ad.session_id == message->session_id &&
                                                s.ad.instance == message->instance;
                                       }),
                        sessions_.end());
        break;
    }
    notify = on_change_;
  }
  changed_.notify_all();
  if (notify) notify();
}

void Discovery::Expire() {
  const auto now = std::chrono::steady_clock::now();
  const auto before = sessions_.size();
  sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                 [&](const auto& s) { return now - s.last_seen > options_.expiry; }),
                  sessions_.end());
  if (sessions_.size() != before) changed_.notify_all();
}

}  // namespace ht::lan
