//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/rocksdb/table/get_context.h"

#include "yb/rocksdb/db/merge_context.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/util/perf_context_imp.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/rocksdb/util/stop_watch.h"

#include "yb/util/stats/perf_step_timer.h"

namespace rocksdb {

namespace {

void appendToReplayLog(std::string* replay_log, ValueType type, Slice value) {
#ifndef ROCKSDB_LITE
  if (replay_log) {
    if (replay_log->empty()) {
      // Optimization: in the common case of only one operation in the
      // log, we allocate the exact amount of space needed.
      replay_log->reserve(1 + VarintLength(value.size()) + value.size());
    }
    replay_log->push_back(type);
    PutLengthPrefixedSlice(replay_log, value);
  }
#endif  // ROCKSDB_LITE
}

}  // namespace

GetContext::GetContext(const Comparator* ucmp,
                       const MergeOperator* merge_operator, Logger* logger,
                       Statistics* statistics, GetState init_state,
                       const Slice& user_key, std::string* ret_value,
                       bool* value_found, MergeContext* merge_context, Env* env,
                       SequenceNumber* seq)
    : ucmp_(ucmp),
      merge_operator_(merge_operator),
      logger_(logger),
      statistics_(statistics),
      state_(init_state),
      user_key_(user_key),
      value_(ret_value),
      value_found_(value_found),
      merge_context_(merge_context),
      env_(env),
      seq_(seq),
      replay_log_(nullptr) {
  if (seq_) {
    *seq_ = kMaxSequenceNumber;
  }
}

// Called from TableCache::Get and Table::Get when file/block in which
// key may exist are not there in TableCache/BlockCache respectively. In this
// case we can't guarantee that key does not exist and are not permitted to do
// IO to be certain.Set the status=kFound and value_found=false to let the
// caller know that key may exist but is not there in memory
void GetContext::MarkKeyMayExist() {
  state_ = kFound;
  if (value_found_ != nullptr) {
    *value_found_ = false;
  }
}

void GetContext::SaveValue(const Slice& value, SequenceNumber seq) {
  assert(state_ == kNotFound);
  appendToReplayLog(replay_log_, kTypeValue, value);

  state_ = kFound;
  if (value_ != nullptr) {
    value_->assign(value.cdata(), value.size());
  }
}

bool GetContext::SaveValue(const ParsedInternalKey& parsed_key,
                           const Slice& value) {
  assert((state_ != kMerge && parsed_key.type != kTypeMerge) ||
         merge_context_ != nullptr);
  if (ucmp_->Equal(parsed_key.user_key, user_key_)) {
    appendToReplayLog(replay_log_, parsed_key.type, value);

    if (seq_ != nullptr) {
      // Set the sequence number if it is uninitialized
      if (*seq_ == kMaxSequenceNumber) {
        *seq_ = parsed_key.sequence;
      }
    }

    // Key matches. Process it
    switch (parsed_key.type) {
      case kTypeValue:
        assert(state_ == kNotFound || state_ == kMerge);
        if (kNotFound == state_) {
          state_ = kFound;
          if (value_ != nullptr) {
            value_->assign(value.cdata(), value.size());
          }
        } else if (kMerge == state_) {
          assert(merge_operator_ != nullptr);
          state_ = kFound;
          if (value_ != nullptr) {
            bool merge_success = false;
            {
              StopWatchNano timer(env_, statistics_ != nullptr);
              PERF_TIMER_GUARD(merge_operator_time_nanos);
              merge_success = merge_operator_->FullMerge(
                  user_key_, &value, merge_context_->GetOperands(), value_,
                  logger_);
              RecordTick(statistics_, MERGE_OPERATION_TOTAL_TIME,
                         timer.ElapsedNanosSafe());
            }
            if (!merge_success) {
              RecordTick(statistics_, NUMBER_MERGE_FAILURES);
              state_ = kCorrupt;
            }
          }
        }
        return false;

      case kTypeDeletion:
      case kTypeSingleDeletion:
        // TODO(noetzli): Verify correctness once merge of single-deletes
        // is supported
        assert(state_ == kNotFound || state_ == kMerge);
        if (kNotFound == state_) {
          state_ = kDeleted;
        } else if (kMerge == state_) {
          state_ = kFound;
          if (value_ != nullptr) {
            bool merge_success = false;
            {
              StopWatchNano timer(env_, statistics_ != nullptr);
              PERF_TIMER_GUARD(merge_operator_time_nanos);
              merge_success = merge_operator_->FullMerge(
                  user_key_, nullptr, merge_context_->GetOperands(), value_,
                  logger_);
              RecordTick(statistics_, MERGE_OPERATION_TOTAL_TIME,
                         timer.ElapsedNanosSafe());
            }
            if (!merge_success) {
              RecordTick(statistics_, NUMBER_MERGE_FAILURES);
              state_ = kCorrupt;
            }
          }
        }
        return false;

      case kTypeMerge:
        assert(state_ == kNotFound || state_ == kMerge);
        state_ = kMerge;
        merge_context_->PushOperand(value);
        return true;

      default:
        assert(false);
        break;
    }
  }

  // state_ could be Corrupt, merge or notfound
  return false;
}

void replayGetContextLog(const Slice& replay_log, const Slice& user_key,
                         GetContext* get_context) {
#ifndef ROCKSDB_LITE
  Slice s = replay_log;
  while (s.size()) {
    auto type = static_cast<ValueType>(*s.data());
    s.remove_prefix(1);
    Slice value;
    bool ret = GetLengthPrefixedSlice(&s, &value);
    assert(ret);
    (void)ret;

    // Since SequenceNumber is not stored and unknown, we will use
    // kMaxSequenceNumber.
    get_context->SaveValue(
        ParsedInternalKey(user_key, kMaxSequenceNumber, type), value);
  }
#else   // ROCKSDB_LITE
  assert(false);
#endif  // ROCKSDB_LITE
}

}  // namespace rocksdb
