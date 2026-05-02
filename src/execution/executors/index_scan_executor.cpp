#include "onebase/execution/executors/index_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  // Get table and index info from catalog
  auto catalog = GetExecutorContext()->GetCatalog();
  table_info_ = catalog->GetTable(plan_->GetTableOid());
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());

  matching_rids_.clear();
  cursor_ = 0;

  // Evaluate the lookup key expression to get the search key
  // For index scan, the lookup key is typically a constant value expression
  auto lookup_key_expr = plan_->GetLookupKey();
  if (lookup_key_expr != nullptr) {
    // Evaluate the lookup key (it's a constant expression, so we pass nullptr for tuple)
    Value lookup_value = lookup_key_expr->Evaluate(nullptr, &table_info_->schema_);

    // Use the index to find matching RIDs
    if (index_info_->SupportsPointLookup() && lookup_value.GetTypeId() == TypeId::INTEGER) {
      int32_t key = lookup_value.GetAsInteger();
      const auto *rids = index_info_->LookupInteger(key);
      if (rids != nullptr) {
        matching_rids_ = *rids;
      }
    }
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto predicate = plan_->GetPredicate();

  // Iterate through matching RIDs from the index
  while (cursor_ < matching_rids_.size()) {
    RID current_rid = matching_rids_[cursor_++];

    // Fetch the tuple from the table heap using the RID
    Tuple table_tuple = table_info_->table_->GetTuple(current_rid);
    *rid = current_rid;

    // Apply optional predicate filter
    if (predicate == nullptr || predicate->Evaluate(&table_tuple, &table_info_->schema_).GetAsBoolean()) {
      // Reconstruct tuple with values_ populated for proper serialization
      std::vector<Value> values;
      for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); i++) {
        values.push_back(table_tuple.GetValue(&table_info_->schema_, i));
      }
      *tuple = Tuple(values);
      return true;
    }
  }

  return false;
}

}  // namespace onebase
