#include "onebase/execution/executors/seq_scan_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  auto table_oid = plan_->GetTableOid();
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(table_oid);
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  auto predicate = plan_->GetPredicate();

  while (iter_ != end_) {
    Tuple table_tuple = *iter_;
    *rid = iter_.GetRID();
    ++iter_;

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
