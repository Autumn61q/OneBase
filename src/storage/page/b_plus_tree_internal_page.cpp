#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeInternalPage<int, page_id_t, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  array_[index].first = key;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  array_[index].second = value;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  // TODO(student): Find the index of the given value in the internal page
  for (int i = 0; i < GetSize(); i++) {
    if (array_[i].second == value) {
      return i;
    }
  }

  return -1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  // TODO(student): Find the child page that should contain the given key
  // 在某个特定的 internal page 内部进行查找，返回下一个要接着查找的 page
  int left = 1;
  int right = GetSize() - 1;
  int ans = 0;

  while (left <= right) {
    int mid = left + (right - left) / 2;

    if (!comparator(key, array_[mid].first)) {
      ans = mid;
      left = mid + 1;
    }
    else {
      right = mid - 1;
    }
  }

  return array_[ans].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) {
  // TODO(student): Create a new root with one key and two children
  array_[0].second = old_value;
  array_[1].first = key;
  array_[1].second = new_value;
  SetSize(2);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) -> int {
  // TODO(student): Insert a new key-value pair after old_value
  int index = ValueIndex(old_value);

  for (int i = GetSize(); i > index + 1; i--) {
    array_[i] = array_[i - 1];
  }

  array_[index + 1].first = key;
  array_[index + 1].second = new_value;
  SetSize(GetSize() + 1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  // TODO(student): Remove the key-value pair at the given index
  for (int i = index; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }
  SetSize(GetSize() - 1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() -> ValueType {
  // TODO(student): Remove all entries and return the only remaining child
  // 典型用途是 B+ 树根节点 shrink（收缩）时，把唯一的 child 提升为新根。
  ValueType only_child = array_[0].second;
  SetSize(0);
  return only_child;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move all entries to recipient during merge。我们这里将 recipient 设置为 left child
  int recipient_size = recipient->Getsize();

  recipient->array_[recipient_size].first = middle_key;
  recipient->array_[recipient_size].second = array_[0].second;

  for (int i = 1; i < GetSize(); i++) {
    recipient->array_[recipient_size + i] = array_[i];
  }

  recipient->SetSize(recipient_size + GetSize());
  SetSize(0);  // 把当前节点清空
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move the second half of entries to recipient during split
  // 分裂用
  int start = GetMinSize();
  int move_count = GetSize() - start;

  recipient->array_[0].first = middle_key;
  recipient->array_[0].second = array_[start].second;

  for (int i = 1; i < move_count; i++) {
    recipient->array_[i] = array_[i+1];
  }

  recipient->SetSize(move_count);
  SetSize(start);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move first entry to end of recipient (redistribute)
  // 当左兄弟不够、右兄弟多一个时，把右兄弟的第一个 entry移到左兄弟的末尾，保持平衡。
  int recipient_size = recipient->GetSize();

  recipient->array_[recipient_size].first = middle_key;
  recipient->array_[recipient_size].second = array_[0].second;
  recipient->SetSize(recipient_size + 1);

  for (int i = 0; i < GetSize() - 1; i++) {
    array_[i] = array_[i + 1];
  }

  SetSize(GetSize() - 1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // TODO(student): Move last entry to front of recipient (redistribute)
  for (int i = recipient->GetSize(); i > 0; i--) {
    recipient->array_[i] = recipient->array_[i - 1];
  }

  recipient->array_[1].first = middle_key;
  recipient->array_[1].second = recipient->array_[0].second;
  recipient->array_[0].second = array_[GetSize() - 1].second;
  recipient->SetSize(recipient->GetSize() + 1);

  SetSize(GetSize() - 1);
}

}  // namespace onebase
