#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::lock_guard<std::mutex> lock(latch_);

  size_t inf_min_access_id = std::numeric_limits<size_t>::max();
  size_t inf_min_access_val = std::numeric_limits<size_t>::max();

  size_t k_max_dist_id = std::numeric_limits<size_t>::max();
  size_t k_max_dist_val = std::numeric_limits<size_t>::min();

  // - Find the frame with the largest backward k-distance
  for (auto &[id, entry] : entries_) {
    // - Only consider evictable frames
    if (!entry.is_evictable_) {
      continue;
    }

    // +∞ 帧
    if (entry.history_.size() < k_) {
      if (entry.history_.front() < inf_min_access_val) {  // 首次访问时间最小的
        inf_min_access_id = id;
        inf_min_access_val = entry.history_.front();
      }
    }
    // 有限 k-distance 帧
    else {
      size_t k_distance = current_timestamp_ - entry.history_.front();
      if (k_distance > k_max_dist_val) {  // k-distance 最大的
        k_max_dist_id = id;
        k_max_dist_val = k_distance;
      }
    }
  }

  size_t victim_id;
  if (inf_min_access_id != std::numeric_limits<size_t>::max()) {
    victim_id = inf_min_access_id;
  } else if (k_max_dist_id != std::numeric_limits<size_t>::max()) {
    victim_id = k_max_dist_id;
  } else {
    return false;
  }

  *frame_id = static_cast<frame_id_t>(victim_id);
  entries_.erase(victim_id);
  curr_size_--;

  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  // TODO(student): Record a new access for frame_id at current timestamp
  std::lock_guard<std::mutex> lock(latch_);  // 获取锁.
  
  auto it = entries_.find(frame_id);

  // - If frame_id is new, create an entry
  if (it == entries_.end()) {
    FrameEntry new_entry;
    entries_.emplace(frame_id, new_entry);
    it = entries_.find(frame_id);
  }

  // - Add current_timestamp_ to the frame's history
  auto &entry = it->second; // // it->first  是 frame_id  it->second 是对应的 FrameEntry 对象
  entry.history_.push_back(current_timestamp_);

  // 如果访问历史超过 k，移除最旧的时间戳
  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }

  // - Increment current_timestamp_
  current_timestamp_++;

}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  // TODO(student): Set whether a frame is evictable

  std::lock_guard<std::mutex> lock(latch_); 

  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }


  if (set_evictable != it->second.is_evictable_) {
    it->second.is_evictable_ = set_evictable;
    if (set_evictable) {
      curr_size_ ++;
    }
    else {
      curr_size_ --;
    }
  }

}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  // TODO(student): Remove a frame from the replacer
  std::lock_guard<std::mutex> lock(latch_); 
  // - The frame must be evictable; throw if not
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }

  if (!it->second.is_evictable_) {
    throw std::logic_error("Frame is not evictable");
  }

  entries_.erase(it);
  curr_size_ --;

}

auto LRUKReplacer::Size() const -> size_t {
  // std::lock_guard<std::mutex> lock(latch_);
  return curr_size_;
}

}  // namespace onebase
