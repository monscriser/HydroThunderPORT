// Tests for the LAN play transport (src/ht_lan_transport.*). No game files needed.
//
//   ht_lan_transport_test              unit + loopback tests on this machine
//   ht_lan_transport_test host [base]  play the host on a real network (two machines)
//   ht_lan_transport_test join [base]  find that host, exchange data both ways, see it leave
//
// host/join use broadcast discovery like the game does. Add an address after the port
// (e.g. "join 36000 192.168.1.20") to also query that machine directly.
#include "../src/ht_lan_transport.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Sock = SOCKET;
static void CloseSock(Sock s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using Sock = int;
static void CloseSock(Sock s) { close(s); }
#endif

using namespace ht::lan;
using namespace std::chrono_literals;

namespace {

int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(cond)) {                                                      \
      ++g_failures;                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                   \
  } while (0)

constexpr uint32_t kTitle = 0x5841096A;
constexpr uint32_t kLoopback = 0x7F000001;

SessionAd SampleAd() {
  SessionAd ad;
  ad.instance = 0x1122334455667788ull;
  for (uint8_t i = 0; i < 8; ++i) ad.session_id[i] = uint8_t(0xA0 + i);
  for (uint8_t i = 0; i < 16; ++i) ad.key[i] = uint8_t(i * 7);
  for (uint8_t i = 0; i < kXnAddrSize; ++i) ad.host_xnaddr[i] = uint8_t(i);
  ad.flags = 0x2B;
  ad.game_type = 1;
  ad.game_mode = 2;
  ad.max_public = 8;
  ad.filled_public = 1;
  ad.host_xuid = 0x0009000012345678ull;
  ad.contexts = {{0x8001, 3}, {0x800B, 1}};
  ad.properties = {{0x10000001, 42, 1}, {0x30000002, 0x4045000000000000ull, 3}};
  ad.qos = {1, 2, 3, 4, 5};
  ad.host_name = "Player 1";
  return ad;
}

bool SameAd(const SessionAd& a, const SessionAd& b) {
  if (a.contexts.size() != b.contexts.size() || a.properties.size() != b.properties.size())
    return false;
  for (size_t i = 0; i < a.properties.size(); ++i)
    if (a.properties[i].id != b.properties[i].id || a.properties[i].value != b.properties[i].value ||
        a.properties[i].type != b.properties[i].type)
      return false;
  return a.instance == b.instance && a.session_id == b.session_id && a.key == b.key &&
         a.host_xnaddr == b.host_xnaddr && a.flags == b.flags && a.game_type == b.game_type &&
         a.game_mode == b.game_mode && a.max_public == b.max_public &&
         a.max_private == b.max_private && a.filled_public == b.filled_public &&
         a.filled_private == b.filled_private && a.host_xuid == b.host_xuid &&
         a.contexts == b.contexts && a.qos == b.qos && a.host_name == b.host_name;
}

// --- unit ------------------------------------------------------------------------

void TestAddresses() {
  CHECK(IsLanAddress(*ParseAddress("10.1.2.3")));
  CHECK(IsLanAddress(*ParseAddress("172.16.0.1")));
  CHECK(IsLanAddress(*ParseAddress("172.31.255.255")));
  CHECK(!IsLanAddress(*ParseAddress("172.32.0.1")));
  CHECK(!IsLanAddress(*ParseAddress("172.15.0.1")));
  CHECK(IsLanAddress(*ParseAddress("192.168.201.128")));
  CHECK(!IsLanAddress(*ParseAddress("192.169.0.1")));
  CHECK(IsLanAddress(*ParseAddress("169.254.10.10")));
  CHECK(IsLanAddress(*ParseAddress("127.0.0.1")));
  CHECK(!IsLanAddress(*ParseAddress("8.8.8.8")));
  CHECK(!IsLanAddress(*ParseAddress("100.64.0.1")));
  CHECK(!IsLanAddress(*ParseAddress("255.255.255.255")));
  CHECK(!ParseAddress(""));
  CHECK(!ParseAddress("1.2.3"));
  CHECK(!ParseAddress("1.2.3.4.5"));
  CHECK(!ParseAddress("256.1.1.1"));
  CHECK(!ParseAddress("1.2.3.4 "));
  CHECK(!ParseAddress("a.b.c.d"));
  CHECK(!ParseAddress("1..2.3"));
  CHECK(ParseAddress("0.0.0.0") == 0u);
  CHECK(AddressToString(*ParseAddress("192.168.1.20")) == "192.168.1.20");
}

void TestPorts() {
  PortMap map;
  map.base = 36000;
  CHECK(map.discovery() == 36000);
  CHECK(map.ToHost(1000) == 36001);
  CHECK(map.ToHost(1015) == 36016);
  CHECK(map.ToHost(1016) == 1016);
  CHECK(map.ToHost(0) == 0);
  CHECK(map.ToHost(3074) == 3074);
  CHECK(map.ToGuest(36001) == 1000);
  CHECK(map.ToGuest(36016) == 1015);
  CHECK(map.ToGuest(36000) == 36000);
  CHECK(map.ToGuest(36017) == 36017);
  for (uint16_t p = 1000; p < 1016; ++p) CHECK(map.ToGuest(map.ToHost(p)) == p);
  CHECK(IsUsableBasePort(36000));
  CHECK(!IsUsableBasePort(1000));
  CHECK(!IsUsableBasePort(65530));
  CHECK(!IsUsableBasePort(-1));
}

void TestRoundTrip() {
  const SessionAd ad = SampleAd();
  const auto packet = EncodeAnnounce(kTitle, ad);
  CHECK(!packet.empty() && packet.size() <= kMaxDatagram);
  auto m = Decode(packet.data(), packet.size(), kTitle);
  CHECK(m && m->type == MessageType::kAnnounce && SameAd(m->ad, ad));

  const auto query = EncodeQuery(kTitle, 77);
  m = Decode(query.data(), query.size(), kTitle);
  CHECK(m && m->type == MessageType::kQuery && m->instance == 77);

  const auto leave = EncodeLeave(kTitle, 78, ad.session_id);
  m = Decode(leave.data(), leave.size(), kTitle);
  CHECK(m && m->type == MessageType::kLeave && m->instance == 78 &&
        m->session_id == ad.session_id);

  const auto hello = EncodeHello(kTitle, 79, ad.host_xnaddr);
  m = Decode(hello.data(), hello.size(), kTitle);
  CHECK(m && m->type == MessageType::kHello && m->instance == 79 &&
        m->xnaddr == ad.host_xnaddr);
  for (size_t n = 0; n < hello.size(); ++n) CHECK(!Decode(hello.data(), n, kTitle));

  // Largest allowed ad still fits one datagram.
  SessionAd big = ad;
  big.contexts.assign(kMaxContexts, {1, 2});
  big.properties.assign(kMaxProperties, {0x10000003, 4, 1});
  big.qos.assign(kMaxQosData, 0x5A);
  big.host_name = std::string(kMaxHostName, 'x');
  const auto big_packet = EncodeAnnounce(kTitle, big);
  CHECK(!big_packet.empty());
  m = Decode(big_packet.data(), big_packet.size(), kTitle);
  CHECK(m && SameAd(m->ad, big));

  // Anything over the limits is refused at encode time.
  SessionAd over = ad;
  over.qos.assign(kMaxQosData + 1, 0);
  CHECK(EncodeAnnounce(kTitle, over).empty());
  over = ad;
  over.contexts.assign(kMaxContexts + 1, {1, 2});
  CHECK(EncodeAnnounce(kTitle, over).empty());
  over = ad;
  over.host_name = "bad\nname";
  CHECK(EncodeAnnounce(kTitle, over).empty());
}

void TestMalformed() {
  const auto good = EncodeAnnounce(kTitle, SampleAd());
  // Wrong title / magic / version / reserved fields.
  CHECK(!Decode(good.data(), good.size(), kTitle + 1));
  CHECK(!Decode(nullptr, 0, kTitle));
  auto bad = good;
  bad[0] = 'X';
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[4] = kProtocolVersion + 1;
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[5] = 9;  // unknown type
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[6] = 1;  // flags must be zero
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[15] = 1;  // reserved must be zero
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  // Every truncation and one trailing byte.
  for (size_t n = 0; n < good.size(); ++n) CHECK(!Decode(good.data(), n, kTitle));
  bad = good;
  bad.push_back(0);
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  // Payload length that disagrees with the datagram.
  bad = good;
  bad[13] ^= 1;
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  // Counts beyond the limits, slot counts that make no sense.
  const size_t counts = kHeaderSize + 8 + 8 + 16 + kXnAddrSize + 4 * 7 + 8;
  bad = good;
  bad[counts] = uint8_t(kMaxContexts + 1);
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[counts + 1] = 0xFF;
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[counts + 2] = 0xFF;  // qos length high byte
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  const size_t filled_public = kHeaderSize + 8 + 8 + 16 + kXnAddrSize + 4 * 5;
  bad[filled_public + 3] = 9;  // filled 9 > max 8
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  bad = good;
  bad[bad.size() - 1] = 0x01;  // control character in the host name
  CHECK(!Decode(bad.data(), bad.size(), kTitle));
  // A query whose instance is zero is not a real sender.
  const auto zero = EncodeQuery(kTitle, 0);
  CHECK(!Decode(zero.data(), zero.size(), kTitle));
  // Well-formed but hostile properties: text/binary values (a length and pointer the
  // sender would choose) and a type that disagrees with the property id.
  for (const Property& p : {Property{0x40000001, 0x0000100040000000ull, 4},
                            Property{0x60000001, 0x0000100040000000ull, 6},
                            Property{0x10000001, 0, 2}, Property{0x00000001, 0, 0}}) {
    SessionAd hostile = SampleAd();
    hostile.properties = {p};
    const auto packet = EncodeAnnounce(kTitle, hostile);
    CHECK(!packet.empty() && !Decode(packet.data(), packet.size(), kTitle));
  }
  // Oversized datagram.
  std::vector<uint8_t> huge(kMaxDatagram + 1, 0);
  std::memcpy(huge.data(), good.data(), kHeaderSize);
  CHECK(!Decode(huge.data(), huge.size(), kTitle));

  // Random garbage and random bit flips must never crash, and garbage must not decode.
  std::mt19937 rng(1234);
  int decoded_garbage = 0;
  for (int i = 0; i < 20000; ++i) {
    std::vector<uint8_t> noise(rng() % (kMaxDatagram + 8));
    for (auto& b : noise) b = uint8_t(rng());
    if (Decode(noise.data(), noise.size(), kTitle)) ++decoded_garbage;
  }
  CHECK(decoded_garbage == 0);
  for (int i = 0; i < 20000; ++i) {
    auto flipped = good;
    flipped[rng() % flipped.size()] ^= uint8_t(1u << (rng() % 8));
    (void)Decode(flipped.data(), flipped.size(), kTitle);  // must simply not crash
  }
}

// --- loopback --------------------------------------------------------------------

Discovery::Options LoopbackOptions(uint16_t port, uint16_t peer_port) {
  Discovery::Options o;
  o.port = port;
  o.title_id = kTitle;
  o.broadcast = false;
  o.peers = {{kLoopback, peer_port}};
  o.announce_interval = 200ms;
  o.expiry = 800ms;
  return o;
}

Sock UdpSocket(uint16_t port) {
  Sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
#if defined(_WIN32)
  DWORD ms = 2000;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms));
#else
  timeval tv{2, 0};
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
  return s;
}

void SendUdp(Sock s, uint32_t ip, uint16_t port, const void* data, size_t size) {
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(port);
  to.sin_addr.s_addr = htonl(ip);
  sendto(s, static_cast<const char*>(data), int(size), 0, reinterpret_cast<sockaddr*>(&to),
         sizeof(to));
}

// Receives one datagram; returns its size (or -1) and the sender.
int RecvUdp(Sock s, std::vector<uint8_t>& buf, uint32_t* ip, uint16_t* port) {
  sockaddr_in from{};
  socklen_t len = sizeof(from);
  buf.resize(2048);
  const int got = recvfrom(s, reinterpret_cast<char*>(buf.data()), int(buf.size()), 0,
                           reinterpret_cast<sockaddr*>(&from), &len);
  if (got >= 0) {
    if (ip) *ip = ntohl(from.sin_addr.s_addr);
    if (port) *port = ntohs(from.sin_port);
  }
  return got;
}

void TestLoopback() {
  const uint16_t host_port = 47100, join_port = 47150;
  Discovery host, joiner;
  std::string error;
  CHECK(host.Start(LoopbackOptions(host_port, join_port), &error));
  CHECK(joiner.Start(LoopbackOptions(join_port, host_port), &error));
  if (!host.running() || !joiner.running()) {
    std::printf("loopback: cannot start (%s)\n", error.c_str());
    return;
  }
  // Nothing hosted yet.
  CHECK(joiner.Search(300ms).empty());

  // Discovery and join.
  SessionAd ad = SampleAd();
  host.Advertise(ad);
  auto found = joiner.Search(1000ms, 1);
  CHECK(found.size() == 1);
  if (found.size() == 1) {
    CHECK(found[0].ad.session_id == ad.session_id && found[0].source == kLoopback);
    CHECK(found[0].ad.instance == host.instance());
    CHECK(found[0].ad.qos == ad.qos);
  }
  CHECK(joiner.Find(ad.session_id).has_value());
  CHECK(joiner.FindByAddress(kLoopback).has_value());
  // The host does not list its own session.
  CHECK(host.Snapshot().empty());

  // Updates replace the entry (slots filled after a join).
  ad.filled_public = 2;
  host.Advertise(ad);
  std::this_thread::sleep_for(300ms);
  auto updated = joiner.Find(ad.session_id);
  CHECK(updated && updated->ad.filled_public == 2);

  // Game data both ways through the port map, as the game sockets do.
  PortMap host_map{48000}, join_map{48100};
  Sock host_game = UdpSocket(host_map.ToHost(1000));
  Sock join_game = UdpSocket(join_map.ToHost(1000));
  const char hello[] = "joiner->host";
  SendUdp(join_game, kLoopback, host_map.ToHost(1000), hello, sizeof(hello));
  std::vector<uint8_t> buf;
  uint32_t from_ip = 0;
  uint16_t from_port = 0;
  CHECK(RecvUdp(host_game, buf, &from_ip, &from_port) == int(sizeof(hello)) &&
        std::memcmp(buf.data(), hello, sizeof(hello)) == 0);
  CHECK(IsLanAddress(from_ip) && join_map.ToGuest(from_port) == 1000);
  const char reply[] = "host->joiner";
  SendUdp(host_game, from_ip, from_port, reply, sizeof(reply));
  CHECK(RecvUdp(join_game, buf, nullptr, nullptr) == int(sizeof(reply)) &&
        std::memcmp(buf.data(), reply, sizeof(reply)) == 0);
  CloseSock(host_game);
  CloseSock(join_game);

  // Malformed datagrams at the discovery port: counted, ignored, service keeps going.
  const auto before = joiner.stats();
  Sock attacker = UdpSocket(0);
  std::mt19937 rng(99);
  for (int i = 0; i < 300; ++i) {
    std::vector<uint8_t> noise(rng() % 1400);
    for (auto& b : noise) b = uint8_t(rng());
    if (noise.size() >= 4 && i % 2) std::memcpy(noise.data(), "HTLN", 4);
    SendUdp(attacker, kLoopback, join_port, noise.data(), noise.size());
    // Paced so the socket buffer does not drop the burst before it is counted.
    if (i % 20 == 19) std::this_thread::sleep_for(5ms);
  }
  std::vector<uint8_t> oversized(4000, 0x41);
  SendUdp(attacker, kLoopback, join_port, oversized.data(), oversized.size());
  CloseSock(attacker);
  std::this_thread::sleep_for(400ms);
  const auto after = joiner.stats();
  std::printf("malformed burst: %llu of 301 counted\n",
              (unsigned long long)(after.rejected_malformed - before.rejected_malformed));
  CHECK(after.rejected_malformed >= before.rejected_malformed + 290);
  CHECK(joiner.running());
  CHECK(joiner.Search(600ms, 1).size() == 1);  // still works

  // Joining: the joiner's HELLO gives the host its XNet address, and the host answers
  // with its own, so each can map the other's IP to an XNADDR.
  XnAddr host_x{}, join_x{};
  host_x.fill(0x11);
  join_x.fill(0x22);
  host.SetLocalXnAddr(host_x);
  joiner.SetLocalXnAddr(join_x);
  CHECK(!host.FindPeer(kLoopback));
  joiner.Hello(kLoopback, host_port);
  std::this_thread::sleep_for(300ms);
  // The address fields (ina, inaOnline, SGADDR ina) are the sender's, whatever the
  // packet said; the rest (MAC, ids) is kept.
  auto pinned_to_loopback = [](XnAddr x) {
    for (size_t at : {size_t(0), size_t(4), size_t(16)})
      x[at] = 127, x[at + 1] = 0, x[at + 2] = 0, x[at + 3] = 1;
    return x;
  };
  CHECK(host.FindPeer(kLoopback) == pinned_to_loopback(join_x));
  CHECK(joiner.FindPeer(kLoopback) == pinned_to_loopback(host_x));

  // Host leaves the lobby: LEAVE removes it at once.
  host.Unadvertise(ad.session_id);
  std::this_thread::sleep_for(300ms);
  CHECK(!joiner.Find(ad.session_id));

  // A flood of announces with new session ids cannot grow the table without bound,
  // and the sessions already listed stay listed.
  host.Advertise(ad);
  CHECK(joiner.Search(1000ms, 1).size() == 1);
  Sock flood = UdpSocket(0);
  for (int i = 0; i < 100; ++i) {
    SessionAd fake = SampleAd();
    fake.instance = 0xF000 + i;
    fake.session_id[7] = uint8_t(i);
    fake.session_id[6] = 0xEE;
    const auto p = EncodeAnnounce(kTitle, fake);
    SendUdp(flood, kLoopback, join_port, p.data(), p.size());
    if (i % 20 == 19) std::this_thread::sleep_for(5ms);
  }
  CloseSock(flood);
  std::this_thread::sleep_for(300ms);
  CHECK(joiner.Snapshot().size() <= kMaxSessionsPerSender);  // the real host shares 127.0.0.1
  CHECK(joiner.Find(ad.session_id).has_value());
  std::this_thread::sleep_for(1200ms);  // the fakes expire; the real host keeps announcing
  CHECK(joiner.Snapshot().size() == 1);

  // A hostile announce claiming a public host address: players only ever see the
  // address the packet came from.
  Sock evil = UdpSocket(0);
  SessionAd liar = SampleAd();
  liar.instance = 0xBAD;
  liar.session_id[0] = 0x42;
  for (size_t at : {size_t(0), size_t(4), size_t(16)}) {
    liar.host_xnaddr[at] = 8, liar.host_xnaddr[at + 1] = 8;
    liar.host_xnaddr[at + 2] = 8, liar.host_xnaddr[at + 3] = 8;
  }
  const auto lie = EncodeAnnounce(kTitle, liar);
  SendUdp(evil, kLoopback, join_port, lie.data(), lie.size());
  std::this_thread::sleep_for(200ms);
  auto pinned = joiner.Find(liar.session_id);
  CHECK(pinned && pinned->ad.host_xnaddr[0] == 127 && pinned->ad.host_xnaddr[4] == 127 &&
        pinned->ad.host_xnaddr[16] == 127 && pinned->ad.host_xnaddr[3] == 1);
  // ...and it cannot take over a listed session by reusing its id.
  SessionAd hijack = liar;
  hijack.session_id = ad.session_id;
  hijack.host_name = "hijack";
  const auto steal = EncodeAnnounce(kTitle, hijack);
  SendUdp(evil, kLoopback, join_port, steal.data(), steal.size());
  std::this_thread::sleep_for(200ms);
  auto kept = joiner.Find(ad.session_id);
  CHECK(kept && kept->ad.host_name == ad.host_name && kept->ad.instance == host.instance());

  // A burst of queries from one sender gets few answers (one per 250 ms), so a small
  // spoofed query cannot be turned into a stream of large announces.
  const auto query = EncodeQuery(kTitle, 0x5151);
  for (int i = 0; i < 20; ++i) SendUdp(evil, kLoopback, host_port, query.data(), query.size());
  int answers = 0;
  std::vector<uint8_t> answer;
  while (RecvUdp(evil, answer, nullptr, nullptr) > 0) ++answers;  // until the 2 s timeout
  CHECK(answers >= 1 && answers <= 2);
  CloseSock(evil);

  // Destinations outside the LAN are never sent to, even when listed as peers.
  Discovery::Options outward = LoopbackOptions(47300, 47301);
  outward.peers = {{*ParseAddress("8.8.8.8"), 47301}};
  Discovery outside;
  CHECK(outside.Start(outward, &error));
  outside.Search(50ms);
  CHECK(outside.stats().sent == 0);
  outside.Stop();

  // Host stops cleanly (quits the game): Stop() sends LEAVE.
  host.Advertise(ad);
  CHECK(joiner.Search(1000ms, 1).size() == 1);
  host.Stop();
  std::this_thread::sleep_for(300ms);
  CHECK(!joiner.Find(ad.session_id));

  // Host vanishes without a word (crash, cable pulled): one ANNOUNCE, then silence.
  Sock ghost = UdpSocket(0);
  SessionAd ghost_ad = SampleAd();
  ghost_ad.instance = 0xDEADBEEF;
  ghost_ad.session_id[0] ^= 0xFF;
  const auto packet = EncodeAnnounce(kTitle, ghost_ad);
  SendUdp(ghost, kLoopback, join_port, packet.data(), packet.size());
  CloseSock(ghost);
  std::this_thread::sleep_for(200ms);
  CHECK(joiner.Find(ghost_ad.session_id).has_value());
  std::this_thread::sleep_for(1200ms);  // > expiry (800 ms)
  CHECK(!joiner.Find(ghost_ad.session_id));
  joiner.Stop();
}

// --- two machines ------------------------------------------------------------------

Discovery::Options NetworkOptions(uint16_t base, int argc, char** argv) {
  Discovery::Options o;
  o.port = base;
  o.title_id = kTitle;
  if (argc > 3)
    if (auto ip = ParseAddress(argv[3])) o.peers = {{*ip, base}};
  return o;
}

int RunHost(uint16_t base, int argc, char** argv) {
  const uint32_t self = PickLocalAddress("");
  std::printf("host: %s, discovery UDP %u, game UDP %u\n", AddressToString(self).c_str(), base,
              PortMap{base}.ToHost(1000));
  Discovery d;
  std::string error;
  if (!d.Start(NetworkOptions(base, argc, argv), &error)) {
    std::printf("FAIL host: %s\n", error.c_str());
    return 1;
  }
  SessionAd ad = SampleAd();
  RandomBytes(ad.session_id.data(), ad.session_id.size());
  ad.host_name = "LAN test host";
  d.Advertise(ad);
  PortMap map{base};
  Sock game = UdpSocket(map.ToHost(1000));
  std::vector<uint8_t> buf;
  bool ok = false;
  for (int i = 0; i < 60 && !ok; ++i) {  // wait up to ~2 minutes for the joiner
    uint32_t ip = 0;
    uint16_t port = 0;
    const int got = RecvUdp(game, buf, &ip, &port);
    if (got <= 0) continue;
    if (!IsLanAddress(ip)) {
      std::printf("host: dropped datagram from non-LAN %s\n", AddressToString(ip).c_str());
      continue;
    }
    std::printf("host: %d bytes from %s:%u \"%.*s\"\n", got, AddressToString(ip).c_str(), port,
                got, reinterpret_cast<char*>(buf.data()));
    const std::string reply = "host->joiner data";
    SendUdp(game, ip, port, reply.data(), reply.size());
    ok = true;
  }
  std::this_thread::sleep_for(1500ms);
  std::printf("host: leaving the session\n");
  d.Unadvertise(ad.session_id);
  std::this_thread::sleep_for(500ms);
  CloseSock(game);
  const auto s = d.stats();
  std::printf("host stats: received %llu accepted %llu bad-source %llu malformed %llu sent %llu\n",
              (unsigned long long)s.received, (unsigned long long)s.accepted,
              (unsigned long long)s.rejected_source, (unsigned long long)s.rejected_malformed,
              (unsigned long long)s.sent);
  d.Stop();
  std::printf(ok ? "PASS host\n" : "FAIL host: no joiner\n");
  return ok ? 0 : 1;
}

int RunJoin(uint16_t base, int argc, char** argv) {
  Discovery d;
  std::string error;
  if (!d.Start(NetworkOptions(base, argc, argv), &error)) {
    std::printf("FAIL join: %s\n", error.c_str());
    return 1;
  }
  std::optional<RemoteSession> host;
  for (int i = 0; i < 30 && !host; ++i) {
    auto found = d.Search(2000ms, 1);
    if (!found.empty()) host = found.front();
  }
  if (!host) {
    std::printf("FAIL join: no session found\n");
    return 1;
  }
  std::printf("join: found \"%s\" at %s (%zu QoS bytes)\n", host->ad.host_name.c_str(),
              AddressToString(host->source).c_str(), host->ad.qos.size());
  PortMap map{base};
  Sock game = UdpSocket(map.ToHost(1000));
  const std::string hello = "joiner->host data";
  SendUdp(game, host->source, map.ToHost(1000), hello.data(), hello.size());
  std::vector<uint8_t> buf;
  uint32_t ip = 0;
  const int got = RecvUdp(game, buf, &ip, nullptr);
  const bool data_ok = got > 0 && ip == host->source;
  std::printf("join: %s\n", data_ok ? "got the host's reply" : "no reply from the host");
  bool left = false;
  for (int i = 0; i < 50 && !left; ++i) {
    std::this_thread::sleep_for(100ms);
    left = !d.Find(host->ad.session_id);
  }
  std::printf("join: %s\n", left ? "host left, session gone" : "session still listed");
  CloseSock(game);
  d.Stop();
  const bool ok = data_ok && left;
  std::printf(ok ? "PASS join\n" : "FAIL join\n");
  return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  if (argc > 1) {
    const std::string mode = argv[1];
    const int base = argc > 2 ? std::atoi(argv[2]) : 36000;
    if (!IsUsableBasePort(base)) {
      std::printf("bad port %d\n", base);
      return 2;
    }
    if (mode == "host") return RunHost(uint16_t(base), argc, argv);
    if (mode == "join") return RunJoin(uint16_t(base), argc, argv);
    std::printf("usage: %s [host|join [port [peer-ip]]]\n", argv[0]);
    return 2;
  }
  TestAddresses();
  TestPorts();
  TestRoundTrip();
  TestMalformed();
  TestLoopback();
  std::printf("local address: %s\n", AddressToString(PickLocalAddress("")).c_str());
  std::printf("%d checks, %d failed\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
