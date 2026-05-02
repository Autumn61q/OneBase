#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTree<int, RID, std::less<int>>;


template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // TODO(student): Insert a key-value pair into the B+ tree
  // 1. If tree is empty, create a new leaf root
  // 2. Find the leaf page for key
  // 3. Insert into leaf; if overflow, split and propagate up
  // 1. 如果树为空，创建根节点
  if (IsEmpty()) {
    Page *page = bpm_->NewPage(&root_page_id_);
    auto *leaf = reinterpret_cast<LeafPage*>(page->GetData());
    leaf->Init(leaf_max_size_);
    leaf->Insert(key, value, comparator_);
    bpm_->UnpinPage(root_page_id_, true);
    return true;
  }
  
  // 2. 找到叶子节点
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  
  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t next_page_id = internal->Lookup(key, comparator_);
    
    bpm_->UnpinPage(page->GetPageId(), false);
    page = bpm_->FetchPage(next_page_id);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  }
  
  // 3. 插入到叶子节点
  auto *leaf = reinterpret_cast<LeafPage*>(node);
  
  // 检查是否已存在
  ValueType old_value;
  if (leaf->Lookup(key, &old_value, comparator_)) {
    bpm_->UnpinPage(page->GetPageId(), false);
    return false;  // 重复的 key
  }
  
  leaf->Insert(key, value, comparator_);
  
  // TODO: 如果 leaf->GetSize() > leaf_max_size_，需要分裂
  // 这部分比较复杂，暂时先不实现
  
  bpm_->UnpinPage(page->GetPageId(), true);
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // TODO(student): Remove a key from the B+ tree
  // 1. Find the leaf page containing key
  // 2. Remove from leaf; if underflow, merge or redistribute
  if (IsEmpty()) {
    return;
  }
  
  // 找到叶子节点
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  
  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t next_page_id = internal->Lookup(key, comparator_);
    
    bpm_->UnpinPage(page->GetPageId(), false);
    page = bpm_->FetchPage(next_page_id);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  }
  
  // 删除
  auto *leaf = reinterpret_cast<LeafPage*>(node);
  leaf->RemoveAndDeleteRecord(key, comparator_);
  
  // TODO: 检查是否需要合并或重新分配
  
  bpm_->UnpinPage(page->GetPageId(), true);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // TODO(student): Search for key and add matching values to result
  if (IsEmpty()) {
    return false;
  }
  
  // 从根节点开始查找
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  
  // 一直向下找到叶子节点
  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t next_page_id = internal->Lookup(key, comparator_);
    
    bpm_->UnpinPage(page->GetPageId(), false);  // 用完 unpin
    page = bpm_->FetchPage(next_page_id);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  }
  
  // 在叶子节点中查找
  auto *leaf = reinterpret_cast<LeafPage*>(node);
  ValueType value;
  bool found = leaf->Lookup(key, &value, comparator_);
  
  if (found) {
    result->push_back(value);
  }
  
  bpm_->UnpinPage(page->GetPageId(), false);
  return found;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  // TODO(student): Return an iterator pointing to the first key
  if (IsEmpty()) {
    return End();
  }
  
  // 从根节点开始
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  
  // 一直往左走
  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t leftmost = internal->ValueAt(0);  // 第一个子节点
    
    bpm_->UnpinPage(page->GetPageId(), false);
    page = bpm_->FetchPage(leftmost);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  }
  
  page_id_t leaf_page_id = page->GetPageId();
  bpm_->UnpinPage(leaf_page_id, false);
  
  return Iterator(leaf_page_id, 0);  // 返回第一个叶子节点的索引 0
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  // TODO(student): Return an iterator pointing to the given key
  if (IsEmpty()) {
    return End();
  }
  
  // 找到叶子节点
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  
  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t next_page_id = internal->Lookup(key, comparator_);
    
    bpm_->UnpinPage(page->GetPageId(), false);
    page = bpm_->FetchPage(next_page_id);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  }
  
  // 在叶子节点中找到 >= key 的位置
  auto *leaf = reinterpret_cast<LeafPage*>(node);
  int index = leaf->KeyIndex(key, comparator_);
  page_id_t leaf_page_id = page->GetPageId();

  // 在 unpin 之前保存需要的信息
  int leaf_size = leaf->GetSize();
  page_id_t next_page_id = leaf->GetNextPageId();

  bpm_->UnpinPage(leaf_page_id, false);

  // 如果 index >= size，说明这个叶子节点没有 >= key 的元素
  if (index >= leaf_size) {
    // 需要移动到下一个叶子节点
    if (next_page_id == INVALID_PAGE_ID) {
      return End();
    }
    return Iterator(next_page_id, 0);
  }

  return Iterator(leaf_page_id, index);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0);
}

}  // namespace onebase
