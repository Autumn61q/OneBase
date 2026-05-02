#include "onebase/execution/executors/update_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }

  auto table_oid = plan_->GetTableOid();
  auto table_info = GetExecutorContext()->GetCatalog()->GetTable(table_oid);
  auto indexes = GetExecutorContext()->GetCatalog()->GetTableIndexes(table_info->name_);
  auto update_exprs = plan_->GetUpdateExpressions();

  int count = 0;
  Tuple child_tuple;
  RID child_rid;

  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // Evaluate update expressions to generate new values
    std::vector<Value> new_values;
    for (const auto &expr : update_exprs) {
      new_values.push_back(expr->Evaluate(&child_tuple, &table_info->schema_));
    }

    // Create new tuple with updated values
    Tuple new_tuple(new_values);

    // Delete old tuple from indexes
    for (auto index_info : indexes) {
      auto key_attr = index_info->GetLookupAttr();
      auto old_key_value = child_tuple.GetValue(&table_info->schema_, key_attr);
      index_info->RemoveEntry(old_key_value.GetAsInteger(), child_rid);
    }

    // Update tuple in table
    table_info->table_->UpdateTuple(child_rid, new_tuple);

    // Insert new tuple into indexes
    for (auto index_info : indexes) {
      auto key_attr = index_info->GetLookupAttr();
      auto new_key_value = new_tuple.GetValue(&table_info->schema_, key_attr);
      index_info->InsertEntry(new_key_value.GetAsInteger(), child_rid);
    }

    count++;
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  has_updated_ = true;
  return true;
}

}  // namespace onebase
