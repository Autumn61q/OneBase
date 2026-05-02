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

  // 2. 找到叶子节点，同时记录路径用于分裂传播
  std::vector<page_id_t> path;
  Page *page = bpm_->FetchPage(root_page_id_);
  auto *node = reinterpret_cast<BPlusTreePage*>(page->GetData());
  path.push_back(root_page_id_);

  while (!node->IsLeafPage()) {
    auto *internal = reinterpret_cast<InternalPage*>(node);
    page_id_t next_page_id = internal->Lookup(key, comparator_);

    bpm_->UnpinPage(page->GetPageId(), false);
    page = bpm_->FetchPage(next_page_id);
    node = reinterpret_cast<BPlusTreePage*>(page->GetData());
    path.push_back(next_page_id);
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

  // 4. 检查是否需要分裂并向上传播
  if (leaf->GetSize() > leaf_max_size_) {
    // 创建新的叶子节点
    page_id_t new_page_id;
    Page *new_page = bpm_->NewPage(&new_page_id);
    auto *new_leaf = reinterpret_cast<LeafPage*>(new_page->GetData());
    new_leaf->Init(leaf_max_size_);

    // 移动一半数据到新节点
    leaf->MoveHalfTo(new_leaf);

    // 更新链表指针
    new_leaf->SetNextPageId(leaf->GetNextPageId());
    leaf->SetNextPageId(new_page_id);

    // 获取新节点的第一个 key 用于向上传播
    KeyType split_key = new_leaf->KeyAt(0);
    page_id_t right_child = new_page_id;

    bpm_->UnpinPage(page->GetPageId(), true);
    bpm_->UnpinPage(new_page_id, true);

    // 向上传播分裂
    for (int i = static_cast<int>(path.size()) - 2; i >= 0; i--) {
      page_id_t parent_id = path[i];
      page_id_t child_id = path[i + 1];

      Page *parent_page = bpm_->FetchPage(parent_id);
      auto *parent = reinterpret_cast<InternalPage*>(parent_page->GetData());

      parent->InsertNodeAfter(child_id, split_key, right_child);

      // 检查父节点是否需要分裂
      if (parent->GetSize() > internal_max_size_) {
        // 创建新的内部节点
        page_id_t new_parent_id;
        Page *new_parent_page = bpm_->NewPage(&new_parent_id);
        auto *new_parent = reinterpret_cast<InternalPage*>(new_parent_page->GetData());
        new_parent->Init(internal_max_size_);

        // 获取中间 key
        int mid = parent->GetSize() / 2;
        KeyType middle_key = parent->KeyAt(mid);

        // 移动一半数据到新节点
        parent->MoveHalfTo(new_parent, middle_key);

        split_key = middle_key;
        right_child = new_parent_id;

        bpm_->UnpinPage(parent_id, true);
        bpm_->UnpinPage(new_parent_id, true);
      } else {
        bpm_->UnpinPage(parent_id, true);
        return true;
      }
    }

    // 如果到达这里，说明根节点也分裂了，需要创建新根
    page_id_t new_root_id;
    Page *new_root_page = bpm_->NewPage(&new_root_id);
    auto *new_root = reinterpret_cast<InternalPage*>(new_root_page->GetData());
    new_root->Init(internal_max_size_);

    // 新根包含：左子节点（旧根）、分裂 key、右子节点（新节点）
    new_root->PopulateNewRoot(root_page_id_, split_key, right_child);

    root_page_id_ = new_root_id;
    bpm_->UnpinPage(new_root_id, true);
  } else {
    bpm_->UnpinPage(page->GetPageId(), true);
  }

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

  // 如果删除后叶子节点为空且是根节点
  if (leaf->GetSize() == 0 && page->GetPageId() == root_page_id_) {
    root_page_id_ = INVALID_PAGE_ID;
    bpm_->UnpinPage(page->GetPageId(), true);
    bpm_->DeletePage(page->GetPageId());
    return;
  }

  bpm_->UnpinPage(page->GetPageId(), true);

  // 检查根节点是否需要调整
  Page *root_page = bpm_->FetchPage(root_page_id_);
  auto *root_node = reinterpret_cast<BPlusTreePage*>(root_page->GetData());

  // 如果根节点是内部节点，检查是否所有叶子节点都为空
  if (!root_node->IsLeafPage()) {
    auto *root_internal = reinterpret_cast<InternalPage*>(root_node);

    // 检查第一个叶子节点是否为空
    page_id_t first_leaf_id = root_internal->ValueAt(0);
    Page *first_leaf_page = bpm_->FetchPage(first_leaf_id);
    auto *first_leaf = reinterpret_cast<LeafPage*>(first_leaf_page->GetData());

    if (first_leaf->GetSize() == 0) {
      // 第一个叶子为空，说明树应该变空
      bpm_->UnpinPage(first_leaf_id, false);
      bpm_->UnpinPage(root_page_id_, false);

      // 删除所有叶子节点和根节点
      for (int i = 0; i < root_internal->GetSize(); i++) {
        bpm_->DeletePage(root_internal->ValueAt(i));
      }
      bpm_->DeletePage(root_page_id_);
      root_page_id_ = INVALID_PAGE_ID;
      return;
    }

    bpm_->UnpinPage(first_leaf_id, false);

    // 如果根是内部节点且只有一个子节点，提升该子节点为新根
    if (root_node->GetSize() == 1) {
      page_id_t new_root_id = root_internal->RemoveAndReturnOnlyChild();
      page_id_t old_root_id = root_page_id_;

      root_page_id_ = new_root_id;

      bpm_->UnpinPage(old_root_id, true);
      bpm_->DeletePage(old_root_id);
      return;
    }
  }

  bpm_->UnpinPage(root_page_id_, false);
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
  auto *leaf = reinterpret_cast<LeafPage*>(node);

  // Skip empty leaf nodes
  while (leaf->GetSize() == 0 && leaf->GetNextPageId() != INVALID_PAGE_ID) {
    page_id_t next_page_id = leaf->GetNextPageId();
    bpm_->UnpinPage(leaf_page_id, false);

    page = bpm_->FetchPage(next_page_id);
    leaf = reinterpret_cast<LeafPage*>(page->GetData());
    leaf_page_id = next_page_id;
  }

  bpm_->UnpinPage(leaf_page_id, false);

  // If we found a non-empty node, return iterator to it; otherwise return End()
  if (leaf->GetSize() > 0) {
    return Iterator(leaf_page_id, 0, bpm_);
  }
  return End();
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
    return Iterator(next_page_id, 0, bpm_);
  }

  return Iterator(leaf_page_id, index, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0, bpm_);
}

}  // namespace onebase
