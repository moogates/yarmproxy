#include "redis_del_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

#include "redis_protocol.h"

namespace yarmproxy {

static const std::string& DelPrefix(size_t keys_count) {
  static std::map<size_t, std::string> prefix_cache {
        {1, "*2\r\n$3\r\ndel\r\n"},
        {2, "*3\r\n$3\r\ndel\r\n"},
        {3, "*4\r\n$3\r\ndel\r\n"},
        {4, "*5\r\n$3\r\ndel\r\n"}
      };
  const auto& it = prefix_cache.find(keys_count);
  if (it != prefix_cache.end()) {
    return it->second;
  }

  std::ostringstream oss;
  oss << '*' << (keys_count + 1) << "\r\n$3\r\ndel\r\n";
  const auto& new_it = prefix_cache.emplace(keys_count, oss.str()).first;
  return new_it->second;
}


std::atomic_int redis_del_cmd_count;
// TODO : 系统调用 vs. redis_key内存拷贝，哪个代价更大呢？
void RedisDelCommand::PushSubquery(const ip::tcp::endpoint& ep, const char* data, size_t bytes) {
  const auto& it = waiting_subqueries_.find(ep);
  if (it == waiting_subqueries_.cend()) {
    LOG_DEBUG << "PushSubquery inc_recycle_lock add new endpoint " << ep
              << " , key=" << redis::Bulk(data, bytes).to_string();
    client_conn_->buffer()->inc_recycle_lock();
    std::shared_ptr<DelSubquery> query(new DelSubquery(ep, data, bytes));
    auto res = waiting_subqueries_.emplace(ep, query);
    tail_query_ = res.first->second;
    return;
  }
  tail_query_ = it->second;
  ++(it->second->keys_count_);

  auto& segment = it->second->segments_.back();
  if (segment.first + segment.second == data) {
    LOG_DEBUG << "PushSubquery append adjcent segment, ep=" << ep << " key=" << redis::Bulk(data, bytes).to_string();
    segment.second += bytes;
  } else {
    LOG_DEBUG << "PushSubquery add new segment, ep=" << ep << " key=" << redis::Bulk(data, bytes).to_string();
    it->second->segments_.emplace_back(data, bytes);
  }
}

RedisDelCommand::RedisDelCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba)
    : Command(client)
    , unparsed_bulks_(ba.absent_bulks())
{
  for(size_t i = 1; i < ba.present_bulks(); ++i) {
    if (i == ba.present_bulks() - 1 && !ba[i].completed()) {
      ++unparsed_bulks_;// don't parse the last key if it's not complete
      break;
    }
    ip::tcp::endpoint ep = BackendLoactor::Instance().Locate(ba[i].payload_data(), ba[i].payload_size(), "REDIS_bj");
    PushSubquery(ep, ba[i].raw_data(), ba[i].present_size());
  }
  LOG_WARN << "RedisDelCommand ctor " << ++redis_del_cmd_count;
}

RedisDelCommand::~RedisDelCommand() {
  LOG_WARN << "RedisDelCommand dtor " << --redis_del_cmd_count
           << " pending_subqueries_.size=" << pending_subqueries_.size();

  // TODO : release all backends
  if (pending_subqueries_.size() != 1) {
    LOG_WARN << "RedisDelCommand dtor pending_subqueries_.size error";
    assert(client_conn_->aborted()); // TODO : check this
  }
  for(auto& it : pending_subqueries_) {
    backend_pool()->Release(it.second->backend_);
  }
}

bool RedisDelCommand::query_recv_complete() {
  return unparsed_bulks_ == 0; // 只解析completed bulk, 因而解析完就是接收完
}

bool RedisDelCommand::WriteQuery() {
  assert(tail_query_);
  if (!init_write_query_) {
    assert(false);
  }
  init_write_query_ = false;
  ActivateWaitingSubquery();
  return false;
}

static void SetBackendReplyCount(std::shared_ptr<BackendConn> backend, size_t count) {
  std::ostringstream oss;
  oss << ":" << count << "\r\n";
  backend->SetReplyData(oss.str().data(), oss.str().size());
}

void RedisDelCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  auto& subquery = pending_subqueries_[backend];
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

    if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
      SetBackendReplyCount(backend, total_del_count_);
      if (client_conn_->IsFirstCommand(shared_from_this())) {
        LOG_DEBUG << "RedisDelCommand OnBackendConnectError write reply, ep="
                  << subquery->backend_endpoint_;
        TryWriteReply(backend);
      } else {
        LOG_DEBUG << "RedisDelCommand OnBackendConnectError waiting to write reply, ep="
                  << subquery->backend_endpoint_;
        replying_backend_ = backend;
      }
    } else {
      LOG_DEBUG << "RedisDelCommand OnBackendConnectError need not reply, ep="
                << subquery->backend_endpoint_
                << " is_tail=" << (subquery == tail_query_)
                << " pending_subqueries_.size=" << pending_subqueries_.size();
      pending_subqueries_.erase(backend);
      backend_pool()->Release(backend);
    }
}

void RedisDelCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                              ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  LOG_WARN << "Command::OnBackendReplyReceived ok, backend=" << backend << " cmd=" << this;
  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  assert(waiting_subqueries_.size() == 0);
  if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
    SetBackendReplyCount(backend, total_del_count_);
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      LOG_WARN << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->backend_endpoint_
               << " command=" << this
               << " write reply, backend=" << backend;
      TryWriteReply(backend);
    } else {
      LOG_WARN << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->backend_endpoint_
               << " command=" << this
               << " waiting to write reply, backend=" << backend;
      replying_backend_ = backend;
    }
  } else {
    // TODO : prepare for recycling, a bit tedious
    assert(backend->buffer()->unparsed_bytes() == 0);
    backend->buffer()->update_processed_bytes(backend->buffer()->unprocessed_bytes());
    pending_subqueries_.erase(backend);
    backend_pool()->Release(backend);

    if (!client_conn_->buffer()->recycle_locked() && unparsed_bulks_ > 0) {
      // TODO : newly added
      // assert(client_conn_->buffer()->recycle_lock_count() == 0);
      client_conn_->TryReadMoreQuery("RedisDelCommand::OnBackendReplyReceived 1");
    }

    LOG_WARN << "OnBackendReplyReceived command=" << this
             << " unparsed_bulks_=" << unparsed_bulks_
             << " pending_subqueries_.size=" << pending_subqueries_.size()
             << " don't write reply, backend=" << backend;
  }
}

void RedisDelCommand::StartWriteReply() {
  if (replying_backend_) {
    // TODO : 如何模拟触发这个调用?
    TryWriteReply(replying_backend_);
    LOG_DEBUG << "StartWriteReply TryWriteReply called";
  }
}

void RedisDelCommand::RotateReplyingBackend(bool success) {
  if (unparsed_bulks_ > 0) {
    LOG_WARN << "RedisDelCommand::RotateReplyingBackend unparsed_bulks_=" << unparsed_bulks_;
    assert(false);
  }
  client_conn_->RotateReplyingCommand();
}

// try to keep pace with parent class impl
void RedisDelCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      OnBackendConnectError(backend);
      // 等同于转发完成已收数据
      client_conn_->buffer()->dec_recycle_lock();
      if (!client_conn_->buffer()->recycle_locked() && // 全部完成write query 才能释放recycle_lock
          !query_recv_complete()) {
        client_conn_->TryReadMoreQuery("redis_del_1");
        LOG_DEBUG << "OnWriteQueryFinished E_CONNECT TryReadMoreQuery";
      }
    } else {
      client_conn_->Abort();
      LOG_DEBUG << "OnWriteQueryFinished error, ec=" << int(ec);
    }
    return;
  }

  auto& query = pending_subqueries_[backend];
  LOG_DEBUG << "OnWriteQueryFinished enter. query=" << query->backend_endpoint_ << " phase=" << query->phase_;
  if (query->phase_ == 0) {
    query->phase_ = 1; // SendingQueryData
    assert(!query->segments_.empty());
    query->backend_->WriteQuery(query->segments_.front().first, query->segments_.front().second);
    LOG_DEBUG << "OnWriteQueryFinished query=" << query->backend_endpoint_
              << " backend=" << query->backend_
              << " phase 1 finished, phase 2 launched";
  } else if (query->phase_ == 1) {
    query->segments_.pop_front();
    if (query->segments_.empty()) {
      client_conn_->buffer()->dec_recycle_lock();
      query->phase_ = 2; // read reply
      LOG_WARN << "OnWriteQueryFinished subquery write all read reply. query=" << query << "@" << query->backend_endpoint_
                << " key_count=" << query->keys_count_
                << " backend=" << backend
                << " conn=" << client_conn_ << " cmd=" << this;
      backend->ReadReply();
    } else {
      // phase_ is still 1
      query->backend_->WriteQuery(query->segments_.front().first, query->segments_.front().second);
    }
  } else {
    // TODO
    LOG_WARN << "OnWriteQueryFinished bad phase " << query->phase_ << " , backend=" << backend;
    client_conn_->Abort();
    assert(false);
  }
}

bool RedisDelCommand::query_parsing_complete() {
  return unparsed_bulks_ == 0;
}

void RedisDelCommand::ActivateWaitingSubquery() {
  for(auto& it : waiting_subqueries_) {  // TODO : 能否直接遍历values?
    auto& query = it.second;
    assert(query->backend_ == nullptr);
    query->backend_ = AllocateBackend(query->backend_endpoint_);
    pending_subqueries_[query->backend_] = query;

    LOG_WARN << "ActivateWaitingSubquery client=" << client_conn_ << " cmd=" << this << " query=" << query
             << " phase=" << query->phase_ << " backend=" << query->backend_;
    const std::string& mset_prefix = DelPrefix(query->keys_count_);
    query->backend_->WriteQuery(mset_prefix.data(), mset_prefix.size());
  }
  waiting_subqueries_.clear();
}

bool RedisDelCommand::ProcessUnparsedPart() {
  ReadBuffer* buffer = client_conn_->buffer();
  std::vector<redis::Bulk> new_bulks;
  size_t total_parsed = 0;

  while(new_bulks.size() < unparsed_bulks_ &&
        total_parsed < buffer->unparsed_received_bytes()) {
    const char * entry = buffer->unparsed_data() + total_parsed;
    size_t unparsed_bytes = buffer->unparsed_received_bytes() - total_parsed;
    new_bulks.emplace_back(entry, unparsed_bytes);
    redis::Bulk& bulk = new_bulks.back();

    if (bulk.present_size() < 0) {
      return false;
    }

    if (bulk.present_size() == 0 || !bulk.completed()) {
      new_bulks.pop_back();
      break;
    }
    total_parsed += bulk.total_size();
    LOG_DEBUG << "ProcessUnparsedPart current bulk parsed_bytes="
              << bulk.total_size();
  }

  LOG_DEBUG << "ProcessUnparsedPart new_bulks.size=" << new_bulks.size()
            << " total_parsed=" << total_parsed;

  if (new_bulks.size() == 0) {
    if (!client_conn_->buffer()->recycle_locked()) {
      client_conn_->TryReadMoreQuery("mset_call_5");
    }
    return true;
  }

  LOG_DEBUG << "ProcessUnparsedPart new_bulks.size=" << new_bulks.size()
            << " unparsed_bulks_=" << unparsed_bulks_;
  unparsed_bulks_ -= new_bulks.size();

  auto& prev_tail_query = tail_query_;
  for(size_t i = 0; i < new_bulks.size(); ++i) {
    assert(new_bulks[i].completed());
  //if (i == new_bulks.size() - 1 && !new_bulks[i].completed()) {
  //  ++unparsed_bulks_;// don't parse the last key if it's not complete
  //  break;
  //}
    ip::tcp::endpoint ep = BackendLoactor::Instance().Locate(
                                new_bulks[i].payload_data(),
                                new_bulks[i].payload_size(),
                                "REDIS_bj");
    PushSubquery(ep, new_bulks[i].raw_data(), new_bulks[i].present_size());
  }

  buffer->update_processed_bytes(total_parsed);
  buffer->update_parsed_bytes(total_parsed);
  ActivateWaitingSubquery();
  return true;
}

bool RedisDelCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != ':') {
    LOG_DEBUG << "RedisDelCommand ParseReply bad format";
    return false;
  }

  const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    return true;
  }

  int del_count; // TODO : add into redis_protocol.h
  try {
    del_count = std::stoi(entry + 1);
  } catch (...) {
    return false;
  }
  total_del_count_ += del_count;

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisDelCommand ParseReply complete, resp.size=" << p - entry + 1
            << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

