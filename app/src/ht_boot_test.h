#pragma once

// Optional integration-test input. Enabled only by HT_BOOT_TEST=1; normal
// launches keep the standard keyboard and controller drivers unchanged.
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <rex/input/input_driver.h>
#include <rex/logging.h>

namespace rex::input {
class HtBootTestDriver final : public InputDriver {
 public:
  explicit HtBootTestDriver(uint32_t user = 0) : InputDriver(nullptr, 0), started_(Clock::now()), user_(user) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }
  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = device_id();
    info.name = "Hydro Thunder boot test";
    info.synthetic = true;
    info.synthetic_user_index = user_;
    out.push_back(info);
  }
  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* state) override {
    if (id != device_id()) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!state) return X_ERROR_INVALID_PARAMETER;
    std::memset(state, 0, sizeof(*state));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started_).count();
    state->packet_number = static_cast<uint32_t>(ms);
    unsigned buttons[2] = {}, triggers[2] = {};
    std::ifstream commands("ht_test_input.txt");
    commands >> buttons[0] >> triggers[0] >> buttons[1] >> triggers[1];
    int lx[2]={},ly[2]={};commands >> lx[0] >> ly[0] >> lx[1] >> ly[1];
    state->gamepad.buttons = static_cast<uint16_t>(buttons[user_]);
    state->gamepad.right_trigger = static_cast<uint8_t>(triggers[user_]);
    state->gamepad.thumb_lx=static_cast<int16_t>(std::clamp(lx[user_],-32768,32767));
    state->gamepad.thumb_ly=static_cast<int16_t>(std::clamp(ly[user_],-32768,32767));
    const int value = buttons[user_] | (triggers[user_] << 16);
    if (last_pulse_.exchange(value) != value) {
      REXLOG_WARN("[ht-boot-test] Player {} polled buttons={} RT={} at {} ms", user_ + 1, buttons[user_], triggers[user_], ms);
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* caps) override {
    if (id != device_id()) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (caps) {
      std::memset(caps, 0, sizeof(*caps));
      caps->type = 1;
      caps->sub_type = 1;
      caps->gamepad.buttons = 0xffff;
    }
    return X_ERROR_SUCCESS;
  }
  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    return id == device_id() ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE* out) override {
    if (id != device_id()) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out) return X_ERROR_INVALID_PARAMETER;
    unsigned buttons[2] = {}, triggers[2] = {};
    std::ifstream commands("ht_test_input.txt");
    commands >> buttons[0] >> triggers[0] >> buttons[1] >> triggers[1];
    const uint32_t current = buttons[user_] | (triggers[user_] > 30 ? 1u << 17 : 0);
    const uint32_t changed = current ^ last_keys_;
    constexpr uint16_t vk[18] = {0x5810,0x5811,0x5812,0x5813,0x5814,0x5815,
      0x5816,0x5817,0x5805,0x5804,0,0,0x5800,0x5801,0x5802,0x5803,0x5806,0x5807};
    for (unsigned bit = 0; bit < 18; ++bit) {
      if (!(changed & (1u << bit))) continue;
      last_keys_ ^= 1u << bit;
      if (!vk[bit]) continue;
      *out = {};
      out->virtual_key = vk[bit];
      out->flags = current & (1u << bit) ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
      REXLOG_WARN("[ht-boot-test] Player {} keystroke {} flags {}", user_+1, vk[bit], uint16_t(out->flags));
      return X_ERROR_SUCCESS;
    }
    return X_ERROR_EMPTY;
  }
 private:
  using Clock = std::chrono::steady_clock;
  static constexpr DeviceId kId = static_cast<DeviceId>(0x4854534d);
  DeviceId device_id() const { return static_cast<DeviceId>(static_cast<uint64_t>(kId) + user_); }
  Clock::time_point started_;
  uint32_t user_;
  uint32_t last_keys_ = 0;
  std::atomic<int> last_pulse_{-1};
};
}  // namespace rex::input
