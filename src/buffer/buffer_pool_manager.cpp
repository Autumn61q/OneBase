#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

// 用于分配一个全新的页面，之前不存在于磁盘中
BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  // TODO(student): Allocate a new page in the buffer pool
  std::lock_guard<std::mutex> lock(latch_);
  // 1. Pick a victim frame from free list or replacer
  frame_id_t frame_id;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } 
  else if (replacer_->Evict(&frame_id)) {
    // 2. 如果是驱逐出来的，必须要做两件事：写回脏页 + 移除旧映射
    Page &victim_page = pages_[frame_id];
    if (victim_page.IsDirty()) {
      disk_manager_->WritePage(victim_page.page_id_, victim_page.GetData());
    }
    page_table_.erase(victim_page.page_id_); // 这一步非常重要！
  } 
  else {
    return nullptr;
  }

  // 3. Allocate a new page_id via disk_manager_
  *page_id = disk_manager_->AllocatePage();
  // 4. Update page_table_ and page metadata
  Page &page = pages_[frame_id];
  page_table_[*page_id] = frame_id;
  page.ResetMemory();
  page.page_id_ = *page_id;
  page.pin_count_ = 1;
  page.is_dirty_ = false;

  replacer_ -> RecordAccess(frame_id);
  replacer_ -> SetEvictable(frame_id, false);

  return &page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
  // TODO(student): Fetch a page from the buffer pool
  std::lock_guard<std::mutex> lock(latch_);
  // 1. Search page_table_ for existing mapping

  auto it = page_table_.find(page_id);
  if (it != page_table_.end()){
    frame_id_t frame_id = it->second;
    Page &page = pages_[frame_id];
    page.pin_count_ ++;
    replacer_ -> RecordAccess(frame_id);
    replacer_ -> SetEvictable(frame_id, false);
    return &page;
  }

  // 2. If not found, pick a victim frame
  // 当我们调用 FetchPage 时，目的是操作页面的
  // 数据，而页面的操作必须在内存中进行。如果页面
  // 不在缓冲池中，我们需要将它从磁盘加载到内存。
  frame_id_t frame_id;
  if (!free_list_.empty()) {
    frame_id = free_list_.front();
    free_list_.pop_front();
  } else if (replacer_->Evict(&frame_id)) {
    Page &victim_page = pages_[frame_id];
    if (victim_page.is_dirty_) {
      disk_manager_->WritePage(victim_page.page_id_, victim_page.GetData());
    }
    page_table_.erase(victim_page.page_id_);
  } else {
    return nullptr;
  }
  
  // 3. Read page from disk into the frame
  Page &page = pages_[frame_id];
  disk_manager_->ReadPage(page_id, page.GetData());

  // 4. 更新元数据和映射表
  page.page_id_ = page_id;
  page.pin_count_ = 1;
  page.is_dirty_ = false;
  page_table_[page_id] = frame_id;

  // 别忘了更新替换器的状态
  replacer_->RecordAccess(frame_id);
  replacer_->SetEvictable(frame_id, false);

  return &page;
}

// pin_count_ 是页面的一个元数据，用于跟踪页面当前被多少用户或操作固定
// pin_count_ > 0 时，页面被认为是固定的，不能被驱逐
// 当页面被访问时（例如通过 FetchPage），pin_count_ 会增加。
// 当页面的使用者完成操作后，会调用 UnpinPage，pin_count_ 减少。
auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
  // TODO(student): Unpin a page, decrementing pin count
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()){
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  // 如果 pin_count_ 小于等于 0，说明页面已经被解锁或未被固定
  if (page.pin_count_ <= 0) {
    return false;
  }

  // 如果调用者传入 is_dirty == true，将页面标记为脏页。
  if (is_dirty) {
    page.is_dirty_ = true;
  }

  page.pin_count_ --;

  // 如果 pin_count_ 减少到 0，调用替换器的 SetEvictable 方法，将页面标记为可驱逐。
  if (page.pin_count_ == 0) {
    replacer_ -> SetEvictable(frame_id, true);
  }

  return true;

}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  // TODO(student): Delete a page from the buffer pool
  // - Page must have pin_count == 0
  // - Remove from page_table_, reset memory, add frame to free_list_
  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);

  // Page 不在 buffer pool 里，也可以视为删除成功
  if (it == page_table_.end()) {
    disk_manager_ -> DeallocatePage(page_id);  // 如果一个页面已经不在 buffer pool 里，就这个页面在磁盘上的空间释放掉
    return true;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  // 还在使用，不能删
  if (page.pin_count_ > 0) {
    return false;
  }

  // 从 replacer 中移除，避免保留旧的访问历史
  replacer_ -> Remove(frame_id);

  // 移除 page table 映射
  page_table_.erase(it);

  // 重置 frame 内容和元数据
  page.ResetMemory();
  page.page_id_ = INVALID_PAGE_ID;
  page.pin_count_ = 0;
  page.is_dirty_ = false;

  // frame 回收到 free list
  free_list_.push_back(frame_id);

  // 磁盘层释放 page
  disk_manager_ -> DeallocatePage(page_id);
  
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  // TODO(student): Force flush a page to disk regardless of dirty flag
  // 强制把指定 page_id 的页面内容写回磁盘

  std::lock_guard<std::mutex> lock(latch_);

  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }

  frame_id_t frame_id = it->second;
  Page &page = pages_[frame_id];

  disk_manager_->WritePage(page.page_id_, page.GetData());
  page.is_dirty_ = false;

  return true;

}

void BufferPoolManager::FlushAllPages() {
  // TODO(student): Flush all pages in the buffer pool to disk
  std::lock_guard<std::mutex> lock(latch_);

  for (auto &entry: page_table_) {
    frame_id_t frame_id = entry.second;
    Page &page = pages_[frame_id];

    disk_manager_ -> WritePage(page.page_id_, page.GetData());
    page.is_dirty_ = false;
  }
}

}  // namespace onebase
