#include "onebase/execution/executors/hash_join_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                    std::unique_ptr<AbstractExecutor> left_executor,
                                    std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  hash_table_.clear();
  result_tuples_.clear();
  cursor_ = 0;

  // Phase 1: Build hash table from left child
  Tuple left_tuple;
  RID left_rid;
  while (left_executor_->Next(&left_tuple, &left_rid)) {
    auto key_value = plan_->GetLeftKeyExpression()->Evaluate(&left_tuple, &left_executor_->GetOutputSchema());
    std::string key = key_value.ToString();
    hash_table_[key].push_back(left_tuple);
  }

  // Phase 2: Probe hash table with right child tuples
  Tuple right_tuple;
  RID right_rid;
  while (right_executor_->Next(&right_tuple, &right_rid)) {
    auto key_value = plan_->GetRightKeyExpression()->Evaluate(&right_tuple, &right_executor_->GetOutputSchema());
    std::string key = key_value.ToString();

    auto it = hash_table_.find(key);
    if (it != hash_table_.end()) {
      for (const auto &left_tuple : it->second) {
        // Combine left and right tuples
        std::vector<Value> values;
        for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(left_tuple.GetValue(&left_executor_->GetOutputSchema(), i));
        }
        for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.push_back(right_tuple.GetValue(&right_executor_->GetOutputSchema(), i));
        }
        result_tuples_.emplace_back(values);
      }
    }
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
