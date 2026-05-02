#include "onebase/execution/executors/delete_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_deleted_) {
    return false;
  }

  auto table_oid = plan_->GetTableOid();
  auto table_info = GetExecutorContext()->GetCatalog()->GetTable(table_oid);
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info->name_);

  int count = 0;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // Remove from indexes first
    for (auto index_info : indexes) {
      auto key_attr = index_info->key_attrs_[0];
      auto key_value = child_tuple.GetValue(&table_info->schema_, key_attr);
      int32_t key = key_value.GetAsInteger();
      index_info->RemoveEntry(key, child_rid);
    }

    // Delete from table
    table_info->table_->DeleteTuple(child_rid);
    count++;
  }

  // Return count as a single integer tuple
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, count);
  *tuple = Tuple(values);
  has_deleted_ = true;
  return true;
}

}  // namespace onebase
