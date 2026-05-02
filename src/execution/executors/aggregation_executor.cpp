#include "onebase/execution/executors/aggregation_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  // Build hash table: group_key -> aggregate values
  std::unordered_map<std::string, std::vector<Value>> agg_map;

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    // Compute group-by key
    std::vector<Value> group_values;
    for (const auto &group_expr : plan_->GetGroupBys()) {
      group_values.push_back(group_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema())));
    }

    // Convert group key to string for hashing
    std::string group_key;
    for (const auto &val : group_values) {
      group_key += val.ToString() + "|";
    }

    // Initialize aggregates for new group
    if (agg_map.find(group_key) == agg_map.end()) {
      std::vector<Value> init_aggs;
      for (size_t i = 0; i < plan_->GetAggregateTypes().size(); i++) {
        auto agg_type = plan_->GetAggregateTypes()[i];
        if (agg_type == AggregationType::CountStarAggregate || agg_type == AggregationType::CountAggregate) {
          init_aggs.push_back(Value(TypeId::INTEGER, 0));
        } else if (agg_type == AggregationType::SumAggregate) {
          init_aggs.push_back(Value(TypeId::INTEGER, 0));
        } else {
          // MIN/MAX: initialize with first value
          auto agg_expr = plan_->GetAggregates()[i];
          init_aggs.push_back(agg_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema())));
        }
      }
      agg_map[group_key] = init_aggs;
    }

    // Update aggregates
    auto &agg_values = agg_map[group_key];
    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); i++) {
      auto agg_type = plan_->GetAggregateTypes()[i];
      auto agg_expr = plan_->GetAggregates()[i];

      if (agg_type == AggregationType::CountStarAggregate) {
        agg_values[i] = Value(TypeId::INTEGER, agg_values[i].GetAsInteger() + 1);
      } else if (agg_type == AggregationType::CountAggregate) {
        auto val = agg_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema()));
        if (!val.IsNull()) {
          agg_values[i] = Value(TypeId::INTEGER, agg_values[i].GetAsInteger() + 1);
        }
      } else if (agg_type == AggregationType::SumAggregate) {
        auto val = agg_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema()));
        if (!val.IsNull()) {
          agg_values[i] = Value(TypeId::INTEGER, agg_values[i].GetAsInteger() + val.GetAsInteger());
        }
      } else if (agg_type == AggregationType::MinAggregate) {
        auto val = agg_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema()));
        if (!val.IsNull() && val.GetAsInteger() < agg_values[i].GetAsInteger()) {
          agg_values[i] = val;
        }
      } else if (agg_type == AggregationType::MaxAggregate) {
        auto val = agg_expr->Evaluate(&child_tuple, &(child_executor_->GetOutputSchema()));
        if (!val.IsNull() && val.GetAsInteger() > agg_values[i].GetAsInteger()) {
          agg_values[i] = val;
        }
      }
    }
  }

  // Convert hash table to result tuples
  for (const auto &[group_key, agg_values] : agg_map) {
    std::vector<Value> output_values;

    // Add group-by columns
    size_t pos = 0;
    for (size_t i = 0; i < plan_->GetGroupBys().size(); i++) {
      size_t end = group_key.find('|', pos);
      std::string val_str = group_key.substr(pos, end - pos);
      // Parse back to Value - for simplicity, assume INTEGER type
      output_values.push_back(Value(TypeId::INTEGER, std::stoi(val_str)));
      pos = end + 1;
    }

    // Add aggregate columns
    for (const auto &agg_val : agg_values) {
      output_values.push_back(agg_val);
    }

    result_tuples_.emplace_back(output_values);
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  return true;
}

}  // namespace onebase
