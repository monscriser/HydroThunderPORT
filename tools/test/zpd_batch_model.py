"""Extract real candidate methods and exercise them against a CPU sample oracle.
This validates grouping, rounding and scopes; it is NOT GPU equivalence proof.
"""
import sys
from pathlib import Path
s=Path(sys.argv[1]).read_text()
def method(name):
    start=s.index('void VulkanCommandProcessor::'+name+'(')
    brace=s.index('{',start)
    depth=1;end=brace+1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}');end+=1
    return s[start:end]
methods='\n'.join(method(n) for n in ['ZpdBatchBeginSegment','ZpdBatchEndSegment','ZpdBatchCopy','ZpdBatchClose'])
prefix=r'''
#include <cassert>
#include <cstdint>
#include <vector>
#include <deque>
#include <array>
#include <unordered_map>
#include <random>
#include <iostream>
#define REXCVAR_GET(x) true
#define assert_true(x) assert(x)
#define assert_false(x) assert(!(x))
constexpr uint32_t VK_QUERY_CONTROL_PRECISE_BIT=1, VK_QUERY_RESULT_64_BIT=1, VK_QUERY_RESULT_WAIT_BIT=2;
struct Device { struct Props { bool occlusionQueryPrecise=true; } p; const Props& properties() {return p;} };
struct Buffer {
  bool* inside; bool active=false; uint32_t current=0;
  std::array<uint64_t,8192> samples{}, results{};
  void CmdVkBeginQuery(int, uint32_t index, int) {assert(*inside&&!active);active=true;current=index;samples[index]=0;}
  void CmdVkEndQuery(int,uint32_t index) {assert(*inside&&active&&current==index);active=false;}
  void CmdVkCopyQueryPoolResults(int,uint32_t first,uint32_t count,int,uint64_t offset,int,int) {
    assert(!*inside&&offset==first*8);
    for(uint32_t i=first;i<first+count;i++) results[i]=samples[i];
  }
  void Draw(uint64_t count) {assert(*inside);if(active) samples[current]+=count;}
};
struct VulkanCommandProcessor {
  struct ZpdInterval {uint32_t host_index;uint64_t submission;uint32_t key;uint64_t estimate;std::vector<uint32_t> parts;};
  bool in_render_pass_=false,zpd_batch_interval_open_=false,zpd_batch_segment_active_=false,zpd_batch_verify_suspended_=false;
  uint32_t zpd_batch_remaining_=0,zpd_batch_segment_=0,occlusion_query_cursor_=0;
  int occlusion_query_pool_=0, occlusion_query_readback_buffer_=0;
  std::vector<uint32_t> zpd_batch_parts_,zpd_batch_copies_;
  std::array<uint64_t,32> zpd_batch_submission_{};
  std::deque<ZpdInterval> zpd_intervals_;
  std::unordered_map<uint32_t,uint64_t> ht_zpd_address_estimate_,zpd_interval_estimate_;
  uint64_t zpd_counter_estimate_=0;
  Buffer deferred_command_buffer_{&in_render_pass_};
  Device device;
  Device* GetVulkanDevice(){return &device;}
  uint64_t GetCurrentSubmission(){return 1;}
  static constexpr uint32_t kZpdBatchSize=256;
  void ZpdBatchBeginSegment();void ZpdBatchEndSegment();void ZpdBatchCopy();void ZpdBatchClose(uint32_t);
  void BeginPass(){assert(!in_render_pass_);in_render_pass_=true;ZpdBatchBeginSegment();}
  void EndPass(){if(in_render_pass_){ZpdBatchEndSegment();in_render_pass_=false;ZpdBatchCopy();}}
};
'''
test=r'''
int main() {
  VulkanCommandProcessor cp;
  std::mt19937 rng(0x90);
  uint64_t expected=0, compared=0, mismatch=0, positive=0;
  for(uint32_t round=0;round<400;round++) {
    cp.zpd_batch_remaining_=8192;cp.occlusion_query_cursor_=0;
    for(uint32_t event=0;event<100;event++) {
      cp.ZpdBatchClose(0x10000+event);
      cp.zpd_batch_interval_open_=true;
      if(cp.in_render_pass_)cp.ZpdBatchBeginSegment();
      for(uint32_t draw=0,n=rng()%20;draw<n;draw++) {
        if(rng()%3==0)cp.EndPass();
        if(!cp.in_render_pass_)cp.BeginPass();
        uint64_t samples=rng()%999999;
        cp.deferred_command_buffer_.Draw(samples);expected+=samples;
      }
      cp.ZpdBatchClose(0x20000+event);
      cp.EndPass();
      assert(cp.zpd_intervals_.size()==1);
      auto interval=cp.zpd_intervals_.front();cp.zpd_intervals_.pop_front();
      uint64_t sum=0;for(auto part:interval.parts)sum+=cp.deferred_command_buffer_.results[part];
      for(uint64_t scale: {1,4,9}) {
        uint64_t reference=(expected+(scale>>1))/scale;
        uint64_t actual=(sum+(scale>>1))/scale;
        mismatch+=reference!=actual;compared++;
        positive+=reference!=((sum+scale+(scale>>1))/scale);
      }
      expected=0;
    }
  }
  assert(mismatch==0 && positive==compared && compared>0);
  std::cout<<"comparisons="<<compared<<" mismatches="<<mismatch<<" positive_detected="<<positive<<"\n";
}
'''
Path(sys.argv[2]).write_text(prefix+methods+test)
