#pragma once
#include <mutex>
#include <vulkan/vulkan.h>

namespace rex::graphics::vulkan {
// A draw must see the handle, its compatible layout and its placeholder status
// from the same publication. Separate atomic loads can mix two generations.
template <class Layout>
class PipelineBindingState {
 public:
  struct Snapshot {
    VkPipeline pipeline = VK_NULL_HANDLE;
    const Layout* layout = nullptr;
    bool is_placeholder = false;
  };
  PipelineBindingState() = default;
  PipelineBindingState(const PipelineBindingState& other) : snapshot_(other.Read()) {}
  PipelineBindingState& operator=(const PipelineBindingState& other) {
    if (this != &other) Publish(other.Read());
    return *this;
  }
  Snapshot Read() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
  }
  void Publish(Snapshot snapshot) {
    std::lock_guard lock(mutex_);
    snapshot_ = snapshot;
  }
 private:
  mutable std::mutex mutex_;
  Snapshot snapshot_;
};
}  // namespace rex::graphics::vulkan
