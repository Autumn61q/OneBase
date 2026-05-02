#include "onebase/storage/page/b_plus_tree_leaf_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeLeafPage<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  next_page_id_ = INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  // TODO(student): Binary search for the index of key
  int left = 0, right = GetSize() - 1;
  int ans = GetSize();

  while (left <= right) {
    int mid = left + (right - left) / 2;
    if (comparator(array_[mid].first, key)) {  // array[mid] < key
      left = mid + 1;
    } else {
      ans = mid;
      right = mid - 1;
    }
  }
  return ans;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType *value,
                                         const KeyComparator &comparator) const -> bool {
  // TODO(student): Look up a key and return its associated value
  int index = KeyIndex(key, comparator);

  if (index < GetSize() && !comparator(key, array_[index].first) && !comparator(array_[index].first, key)) {
    *value = array_[index].second;
    return true;
  }

  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value,
                                         const KeyComparator &comparator) -> int {
  // TODO(student): Insert a key-value pair in sorted order
  int index = KeyIndex(key, comparator);

  for (int i = GetSize(); i > index; i--) {
    array_[i] = array_[i-1];  // 把元素都往后移动，为插入腾空
  }

  array_[index] = std::make_pair(key, value);
  IncreaseSize(1);

  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(const KeyType &key,
                                                        const KeyComparator &comparator) -> int {
  // TODO(student): Remove a key-value pair
  int index = KeyIndex(key, comparator);
  // 检查是否找到了 key（key 相等的条件）
  if (index >= GetSize() || comparator(key, array_[index].first) || comparator(array_[index].first, key)) {
    return GetSize();  // 没找到，直接返回
  }
  
  // 向前移动后面的元素
  for (int i = index; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  
  IncreaseSize(-1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  // TODO(student): Move second half of entries to recipient during split
  int start_index = GetSize() / 2;  // 从中间开始
  int move_count = GetSize() - start_index;  // 要移动的元素数量
  
  // 复制后一半到 recipient
  for (int i = 0; i < move_count; i++) {
    recipient->array_[i] = array_[start_index + i];
  }
  
  // 更新 size
  recipient->SetSize(move_count);
  SetSize(start_index);
  
  // 更新链表指针：recipient 指向原来 this 的下一个节点
  recipient->SetNextPageId(GetNextPageId());
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  // TODO(student): Move all entries to recipient during merge
  int recipient_size = recipient->GetSize();
  
  // 把所有元素复制到 recipient 的末尾
  for (int i = 0; i < GetSize(); i++) {
    recipient->array_[recipient_size + i] = array_[i];
  }
  
  // 更新 size
  recipient->IncreaseSize(GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  // TODO(student): Move first entry to end of recipient
  // 把第一个元素追加到 recipient 末尾
  recipient->array_[recipient->GetSize()] = array_[0];
  recipient->IncreaseSize(1);
  
  // 把剩余元素向前移动
  for (int i = 0; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  // TODO(student): Move last entry to front of recipient
  // recipient 的元素向后移动
  for (int i = recipient->GetSize(); i > 0; i--) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  
  // 把最后一个元素放到 recipient 开头
  recipient->array_[0] = array_[GetSize() - 1];
  recipient->IncreaseSize(1);
  
  IncreaseSize(-1);
}

}  // namespace onebase
