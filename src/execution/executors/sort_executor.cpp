#include "onebase/execution/executors/sort_executor.h"
#include <algorithm>
#include "onebase/common/exception.h"

namespace onebase {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() {
  child_executor_->Init();
  sorted_tuples_.clear();
  cursor_ = 0;

  // Materialize all tuples from child
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    sorted_tuples_.push_back(child_tuple);
  }

  // Sort tuples according to ORDER BY expressions
  auto order_bys = plan_->GetOrderBys();
  std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
    [&order_bys, this](const Tuple &a, const Tuple &b) -> bool {
      for (const auto &order_by : order_bys) {
        bool is_ascending = order_by.first;
        const auto &expr = order_by.second;

        Value val_a = expr->Evaluate(&a, &(child_executor_->GetOutputSchema()));
        Value val_b = expr->Evaluate(&b, &(child_executor_->GetOutputSchema()));

        if (val_a.CompareEquals(val_b).GetAsBoolean()) {
          continue;  // Equal, check next order by expression
        }

        bool less_than = val_a.CompareLessThan(val_b).GetAsBoolean();
        return is_ascending ? less_than : !less_than;
      }
      return false;  // All order by expressions are equal
    });
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= sorted_tuples_.size()) {
    return false;
  }
  *tuple = sorted_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
