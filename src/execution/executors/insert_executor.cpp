#include "onebase/execution/executors/insert_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  has_inserted_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_inserted_) {
    return false;
  }

  auto table_oid = plan_->GetTableOid();
  auto table_info = GetExecutorContext()->GetCatalog()->GetTable(table_oid);
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info->name_);

  int count = 0;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto new_rid_opt = table_info->table_->InsertTuple(child_tuple);
    if (new_rid_opt.has_value()) {
      count++;
      RID new_rid = new_rid_opt.value();

      // Update indexes
      for (auto index_info : indexes) {
        // Get the key attribute value (assuming single-column integer index)
        auto key_attr = index_info->key_attrs_[0];
        auto key_value = child_tuple.GetValue(&table_info->schema_, key_attr);
        int32_t key = key_value.GetAsInteger();
        index_info->InsertEntry(key, new_rid);
      }
    }
  }

  // Return count as a single integer tuple
  std::vector<Value> values;
  values.emplace_back(TypeId::INTEGER, count);
  *tuple = Tuple(values);
  has_inserted_ = true;
  return true;
}

}  // namespace onebase
