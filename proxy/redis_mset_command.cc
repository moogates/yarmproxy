#include "redis_mset_command.h"

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

std::atomic_int redis_mset_cmd_count;

static const std::string& MsetPrefix(size_t keys_count) {
  static std::map<size_t, std::string> prefix_cache {
        {3, "*3\r\n$4\r\nmset\r\n"},
        {5, "*5\r\n$4\r\nmset\r\n"},
        {7, "*7\r\n$4\r\nmset\r\n"},
        {9, "*9\r\n$4\r\nmset\r\n"}
      };
  const auto& it = prefix_cache.find(keys_count);
  if (it != prefix_cache.end()) {
    return it->second;
  }

  std::ostringstream oss;
  oss << '*' << (keys_count * 2 + 1) << "\r\n$4\r\nmset\r\n";
  const auto& new_it = prefix_cache.insert(std::make_pair(keys_count, oss.str())).first;
  return new_it->second;
}

void RedisMsetCommand::PushSubquery(const ip::tcp::endpoint& ep, const char* data, size_t bytes) {
  const auto& it = subqueries_.find(ep);
  if (it == subqueries_.cend()) {
    LOG_DEBUG << "PushSubquery inc_recycle_lock add new endpoint " << ep << " , key=" << redis::Bulk(data, bytes).to_string();
    client_conn_->buffer()->inc_recycle_lock();
    auto res = subqueries_.insert(std::make_pair(ep, new Subquery(ep, 2, data,
                             bytes)));
    tail_query_ = res.first->second;
    return;
  }

  tail_query_ = it->second;
  it->second->bulks_count_ += 2;

  auto& segment = it->second->segments_.back();
  if (segment.first + segment.second == data) {
    LOG_DEBUG << "PushSubquery append adjcent segment, ep=" << ep << " key=" << redis::Bulk(data, bytes).to_string();
    segment.second += bytes;
  } else {
    LOG_DEBUG << "PushSubquery add new segment, ep=" << ep << " key=" << redis::Bulk(data, bytes).to_string();
    it->second->segments_.emplace_back(data, bytes);
  }
}

RedisMsetCommand::RedisMsetCommand(std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba)
    : Command(client)
    , unparsed_bulks_(ba.absent_bulks())
{
  unparsed_bulks_ += unparsed_bulks_ % 2;  // don't parse the 'key' now if 'value' absent
  for(size_t i = 1; (i + 1) < ba.present_bulks(); i += 2) { // only 'key' is inadequate, 'value' field must be present
    ip::tcp::endpoint ep = BackendLoactor::Instance().Locate(ba[i].payload_data(), ba[i].payload_size(), "REDIS_bj");
    PushSubquery(ep, ba[i].raw_data(), ba[i].present_size() + ba[i+1].present_size());
  }
  LOG_DEBUG << "RedisMsetCommand ctor " << ++redis_mset_cmd_count;
}

RedisMsetCommand::~RedisMsetCommand() {
  // TODO : release all backends
//if (backend_conn_) {
//  backend_pool()->Release(backend_conn_);
//}
  LOG_DEBUG << "RedisMsetCommand dtor " << --redis_mset_cmd_count;
}

void RedisMsetCommand::WriteQuery() {
  if (!init_write_query_) {
    assert(tail_query_);
    tail_query_->phase_ = 3;
    LOG_DEBUG << "WriteQuery non-init, data.size="
             << client_conn_->buffer()->unprocessed_bytes()
             << " backend=" << tail_query_->backend_;
    client_conn_->buffer()->inc_recycle_lock();
    tail_query_->backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
        client_conn_->buffer()->unprocessed_bytes());
    return;
  }

  init_write_query_ = false;
  ActivateWaitingSubquery();
}

void RedisMsetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  if (unparsed_bulks_ == 0 && subqueries_.size() == 0 && pending_subqueries_.size() == 1) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      TryWriteReply(backend);
      LOG_DEBUG << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->backend_endpoint_
               << " write reply, backend=" << backend;
    } else {
      LOG_DEBUG << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->backend_endpoint_
               << " waiting to write reply, backend=" << backend;
      replying_backend_ = backend;
    }
  } else {
    LOG_DEBUG << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->backend_endpoint_
             << " don't write reply, backend=" << backend;
    // TODO : prepare for recycling, a bit tedious
    assert(backend->buffer()->unparsed_bytes() == 0);
    backend->buffer()->update_processed_bytes(backend->buffer()->unprocessed_bytes());
    backend->set_reply_recv_complete();
    pending_subqueries_.erase(backend);
  }
}

void RedisMsetCommand::StartWriteReply() {
  if (replying_backend_) {
    // TODO : 如何模拟触发这个调用?
    TryWriteReply(replying_backend_);
    LOG_DEBUG << "StartWriteReply TryWriteReply called";
  }
}

void RedisMsetCommand::RotateReplyingBackend(bool success) {
  if (unparsed_bulks_ != 0) {
    LOG_WARN << "RedisMsetCommand::RotateReplyingBackend unparsed_bulks_=" << unparsed_bulks_;
    assert(false);
  }
  client_conn_->RotateReplyingCommand();
}

// try to keep pace with parent class impl
void RedisMsetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
    //LOG_DEBUG << "OnWriteQueryFinished conn_refused, endpoint=" << backend->remote_endpoint()
    //         << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_DEBUG << "OnWriteQueryFinished error, ec=" << int(ec);
    }
    return;
  }

  auto& query = pending_subqueries_[backend];
  LOG_DEBUG << "OnWriteQueryFinished enter. query=" << query->backend_endpoint_ << " phase=" << query->phase_;
  if (query->phase_ == 0) {
    query->phase_ = 1;
    assert(!query->segments_.empty());
    query->backend_->WriteQuery(query->segments_.front().first, query->segments_.front().second);
    LOG_DEBUG << "OnWriteQueryFinished query=" << query->backend_endpoint_ << " phase 1 finished, phase 2 launched";
    return;
  } else if (query->phase_ == 1) {
    query->segments_.pop_front();
    if (query->segments_.empty()) {
      query->phase_ = 2;
      client_conn_->buffer()->dec_recycle_lock();
      // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
      if (!client_conn_->buffer()->recycle_locked() // 全部完成write query 才能释放recycle_lock
          && (client_conn_->buffer()->parsed_unreceived_bytes() > 0
              || !query_parsing_complete())) {
        client_conn_->TryReadMoreQuery();
      }

      if (query != tail_query_ || client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
        LOG_DEBUG << "OnWriteQueryFinished query=" << query->backend_endpoint_ << " read reply, backend=" << backend;
        backend->ReadReply();
      }
      return;
    } else {
      LOG_DEBUG << "OnWriteQueryFinished index=" << query->backend_endpoint_
                << " " << query->segments_.size() << " setments to write";
      query->backend_->WriteQuery(query->segments_.front().first, query->segments_.front().second);
      return;
    }
  } else if (query->phase_ == 3) {
    client_conn_->buffer()->dec_recycle_lock();
    if (query == tail_query_ && client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
      LOG_DEBUG << "OnWriteQueryFinished query=" << query->backend_endpoint_ << " phase 3, TryReadMoreQuery"
               << " query_parsed_unreceived_bytes=" << client_conn_->buffer()->parsed_unreceived_bytes();
      client_conn_->TryReadMoreQuery();
    } else {
      LOG_DEBUG << "OnWriteQueryFinished query=" << query->backend_endpoint_ << " phase 3 completed";
      query->phase_ = 4;
      backend->ReadReply();
    }
    return;  // TODO : should return here?
  } else {
    LOG_ERROR << "OnWriteQueryFinished query=" << query->backend_endpoint_ << " phase=" << query->phase_;
    assert(false);
  }
}

bool RedisMsetCommand::query_parsing_complete() {
  // assert(unparsed_bulks_ == 0);
  return unparsed_bulks_ == 0;
}

void RedisMsetCommand::ActivateWaitingSubquery() {
  LOG_DEBUG << "ActivateWaitingSubquery begin, cmd=" << this;
  for(auto& it : subqueries_) {  // TODO : 能否直接遍历values?
    auto& query = it.second;
    assert(query->backend_ == nullptr);
    query->backend_ = AllocateBackend(query->backend_endpoint_);
    pending_subqueries_[query->backend_] = query;

    const std::string& mset_prefix = MsetPrefix(query->bulks_count_ / 2);

    LOG_DEBUG << "ActivateWaitingSubquery, cmd=" << this
              << " backend=" << query->backend_
              << " ep=" << query->backend_endpoint_
              << " query=" << query->backend_endpoint_
              << " segments_.size=" << query->segments_.size()
              << " bulks_count_=" << query->bulks_count_
              << " prefix=[" << mset_prefix << "]";

    // TODO : merge adjcent shared-endpoint queries
    query->backend_->WriteQuery(mset_prefix.data(), mset_prefix.size());
  }
  subqueries_.clear();
}

bool RedisMsetCommand::ParseIncompleteQuery2() {
  LOG_DEBUG << "RedisMsetCommand::ParseIncompleteQuery2 unparsed_bulks_=" << unparsed_bulks_;
  ReadBuffer* buffer = client_conn_->buffer();
  std::vector<redis::Bulk> new_bulks;
  size_t total_parsed = 0;

  while(new_bulks.size() < unparsed_bulks_ && total_parsed < buffer->unparsed_received_bytes()) {
    const char * entry = buffer->unparsed_data() + total_parsed;
    size_t unparsed_bytes = buffer->unparsed_received_bytes() - total_parsed;
    new_bulks.emplace_back(entry, unparsed_bytes);
    redis::Bulk& bulk = new_bulks.back();

    if (bulk.present_size() < 0) {
      return false;
    }

    if (bulk.present_size() == 0) {
      new_bulks.pop_back();
      break;
    }
    total_parsed += bulk.total_size();
    LOG_DEBUG << "ParseIncompleteQuery2 parsed_bytes=" << bulk.total_size() << " total_parsed=" << total_parsed;
  }

  if (new_bulks.size() % 2 == 1) {
    total_parsed -= new_bulks.back().total_size();
    new_bulks.pop_back();
  }
  LOG_DEBUG << "ParseIncompleteQuery2 new_bulks.size=" << new_bulks.size() << " unparsed_bulks_=" << unparsed_bulks_;

  size_t to_process_bytes = 0;
  for(size_t i = 0; i + 1 < new_bulks.size(); i += 2) {
    // TODO : limit max pending subqueries
    ip::tcp::endpoint ep = BackendLoactor::Instance().Locate(new_bulks[i].payload_data(), new_bulks[i].payload_size(), "REDIS_bj");
    LOG_DEBUG << "ParseIncompleteQuery2, key=" << new_bulks[i].to_string()
             << " v.present_size=[" << new_bulks[i + 1].present_size() << "] ep=" << ep;
    to_process_bytes += new_bulks[i].present_size();
    to_process_bytes += new_bulks[i + 1].present_size();
    PushSubquery(ep, new_bulks[i].raw_data(), new_bulks[i].present_size() + new_bulks[i+1].present_size());
  }

  assert(total_parsed >= to_process_bytes);
  buffer->update_processed_bytes(to_process_bytes);
  buffer->update_parsed_bytes(total_parsed);
  unparsed_bulks_ -= new_bulks.size();
  ActivateWaitingSubquery();
  return true;
}

bool RedisMsetCommand::ParseIncompleteQuery() {
  return ParseIncompleteQuery2();
}

bool RedisMsetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != '+' && entry[0] != '-' && entry[0] != '$') {
    LOG_DEBUG << "RedisMsetCommand ParseReply unknown reply format(" << std::string(entry, unparsed) << ")";
    return false;
  }

  const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    LOG_DEBUG << "RedisMsetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend->buffer()->unparsed_bytes())
              << " bytes=" << backend->buffer()->unparsed_bytes();
    return true;
  }

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisMsetCommand ParseReply resp.size=" << p - entry + 1
            << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_recv_complete, backend=" << backend
            << " query=" << pending_subqueries_[backend]->backend_endpoint_;
  backend->set_reply_recv_complete();
  return true;
}

}

