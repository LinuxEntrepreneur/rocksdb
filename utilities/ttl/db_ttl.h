// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef LEVELDB_UTILITIES_TTL_DB_TTL_H_
#define LEVELDB_UTILITIES_TTL_DB_TTL_H_

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/compaction_filter.h"
#include "leveldb/merge_operator.h"
#include "db/db_impl.h"

namespace leveldb {

class DBWithTTL : public DB, CompactionFilter {
 public:
  DBWithTTL(const int32_t ttl,
            const Options& options,
            const std::string& dbname,
            Status& st,
            bool read_only);

  virtual ~DBWithTTL();

  virtual Status Put(const WriteOptions& o,
                     const Slice& key,
                     const Slice& val);

  virtual Status Get(const ReadOptions& options,
                     const Slice& key,
                     std::string* value);

  virtual std::vector<Status> MultiGet(const ReadOptions& options,
                                       const std::vector<Slice>& keys,
                                       std::vector<std::string>* values);

  virtual bool KeyMayExist(ReadOptions& options,
                           const Slice& key,
                           std::string* value,
                           bool* value_found = nullptr);

  virtual Status Delete(const WriteOptions& wopts, const Slice& key);

  virtual Status Merge(const WriteOptions& options,
                       const Slice& key,
                       const Slice& value);


  virtual Status Write(const WriteOptions& opts, WriteBatch* updates);

  virtual Iterator* NewIterator(const ReadOptions& opts);

  virtual const Snapshot* GetSnapshot();

  virtual void ReleaseSnapshot(const Snapshot* snapshot);

  virtual bool GetProperty(const Slice& property, std::string* value);

  virtual void GetApproximateSizes(const Range* r, int n, uint64_t* sizes);

  virtual void CompactRange(const Slice* begin, const Slice* end,
                            bool reduce_level = false);

  virtual int NumberLevels();

  virtual int MaxMemCompactionLevel();

  virtual int Level0StopWriteTrigger();

  virtual Status Flush(const FlushOptions& fopts);

  virtual Status DisableFileDeletions();

  virtual Status EnableFileDeletions();

  virtual Status GetLiveFiles(std::vector<std::string>& vec, uint64_t* mfs);

  virtual SequenceNumber GetLatestSequenceNumber();

  virtual Status GetUpdatesSince(SequenceNumber seq_number,
                                 unique_ptr<TransactionLogIterator>* iter);

  // Simulate a db crash, no elegant closing of database.
  void TEST_Destroy_DBWithTtl();

  // The following two methods are for CompactionFilter
  virtual bool Filter(int level,
                      const Slice& key,
                      const Slice& old_val,
                      std::string* new_val,
                      bool* value_changed) const override;

  virtual const char* Name() const override;

  static bool IsStale(const Slice& value, int32_t ttl);

  static Status AppendTS(const Slice& val, std::string& val_with_ts);

  static Status SanityCheckTimestamp(const Slice& str);

  static Status StripTS(std::string* str);

  static Status GetCurrentTime(int32_t& curtime);

  static const int32_t kTSLength = sizeof(int32_t); // size of timestamp

  static const int32_t kMinTimestamp = 1368146402; // 05/09/2013:5:40PM GMT-8

  static const int32_t kMaxTimestamp = 2147483647; // 01/18/2038:7:14PM GMT-8

 private:
  DB* db_;
  int32_t ttl_;
  unique_ptr<MergeOperator> ttl_merge_op_;
};

class TtlIterator : public Iterator {

 public:
  explicit TtlIterator(Iterator* iter)
    : iter_(iter) {
    assert(iter_);
  }

  ~TtlIterator() {
    delete iter_;
  }

  bool Valid() const {
    return iter_->Valid();
  }

  void SeekToFirst() {
    iter_->SeekToFirst();
  }

  void SeekToLast() {
    iter_->SeekToLast();
  }

  void Seek(const Slice& target) {
    iter_->Seek(target);
  }

  void Next() {
    iter_->Next();
  }

  void Prev() {
    iter_->Prev();
  }

  Slice key() const {
    return iter_->key();
  }

  int32_t timestamp() const {
    return DecodeFixed32(
      iter_->value().data() + iter_->value().size() - DBWithTTL::kTSLength);
  }

  Slice value() const {
    //TODO: handle timestamp corruption like in general iterator semantics
    assert(DBWithTTL::SanityCheckTimestamp(iter_->value()).ok());
    Slice trimmed_value = iter_->value();
    trimmed_value.size_ -= DBWithTTL::kTSLength;
    return trimmed_value;
  }

  Status status() const {
    return iter_->status();
  }

 private:
  Iterator* iter_;
};

class TtlMergeOperator : public MergeOperator {

 public:
  explicit TtlMergeOperator(const MergeOperator* merge_op)
    : user_merge_op_(merge_op) {
    assert(merge_op);
  }

  virtual void Merge(const Slice& key,
                     const Slice* existing_value,
                     const Slice& value,
                     std::string* new_value,
                     Logger* logger) const {
    const uint32_t& ts_len = DBWithTTL::kTSLength;
    if ((existing_value && existing_value->size() < ts_len) ||
        value.size() < ts_len) {
      Log(logger, "Error: Could not remove timestamp correctly from value.");
      assert(false);
      //TODO: Remove assert and make this function return false.
      //TODO: Change Merge semantics and add a counter here
    }
    Slice value_without_ts(value.data(), value.size() - ts_len);
    if (existing_value) {
      Slice existing_value_without_ts(existing_value->data(),
                                      existing_value->size() - ts_len);
      user_merge_op_->Merge(key, &existing_value_without_ts, value_without_ts,
                            new_value, logger);
    } else {
      user_merge_op_->Merge(key, nullptr, value_without_ts, new_value, logger);
    }
    int32_t curtime;
    if (!DBWithTTL::GetCurrentTime(curtime).ok()) {
      Log(logger, "Error: Could not get current time to be attached internally "
                  "to the new value.");
      assert(false);
      //TODO: Remove assert and make this function return false.
    } else {
      char ts_string[ts_len];
      EncodeFixed32(ts_string, curtime);
      new_value->append(ts_string, ts_len);
    }
  }

  virtual const char* Name() const {
    return "Merge By TTL";
  }

 private:
  const MergeOperator* user_merge_op_;
};

}
#endif  // LEVELDB_UTILITIES_TTL_DB_TTL_H_
