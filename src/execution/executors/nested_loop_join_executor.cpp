#include "onebase/execution/executors/nested_loop_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx,
                                                const NestedLoopJoinPlanNode *plan,
                                                std::unique_ptr<AbstractExecutor> left_executor,
                                                std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  // Perform nested loop join and materialize results
  Tuple left_tuple;
  RID left_rid;
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    right_executor_->Init();  // Reset right executor for each left tuple
    Tuple right_tuple;
    RID right_rid;
    while (right_executor_->Next(&right_tuple, &right_rid)) {
      // Combine left and right tuples
      std::vector<Value> values;
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(left_tuple.GetValue(&left_executor_->GetOutputSchema(), i));
      }
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.push_back(right_tuple.GetValue(&right_executor_->GetOutputSchema(), i));
      }
      Tuple combined_tuple(values);

      // Evaluate predicate
      auto predicate = plan_->GetPredicate();
      if (predicate == nullptr || predicate->EvaluateJoin(&left_tuple, &(left_executor_->GetOutputSchema()),
                                                           &right_tuple, &(right_executor_->GetOutputSchema())).GetAsBoolean()) {
        result_tuples_.push_back(combined_tuple);
      }
    }
  }
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
