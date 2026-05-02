#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/common/exception.h"
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template class BPlusTreeIterator<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  // TODO(student): Dereference the iterator
  Page *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(page->GetData());

  current_ = std::make_pair(leaf->KeyAt(index_), leaf->ValueAt(index_));

  bpm_->UnpinPage(page_id_, false);

  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  // TODO(student): Advance the iterator to the next key-value pair
  Page *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>*>(page->GetData());

  index_++;

  if (index_ >= leaf->GetSize()) {
    page_id_t next_page_id = leaf->GetNextPageId();

    bpm_->UnpinPage(page_id_, false);

    if (next_page_id == INVALID_PAGE_ID) {
      page_id_ = INVALID_PAGE_ID;
      index_ = 0;
    } else {
      page_id_ = next_page_id;
      index_ = 0;
    }
  } else {
    bpm_->UnpinPage(page_id_, false);
  }

  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

}  // namespace onebase
