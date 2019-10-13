#include "redis_mset_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "redis_protocol.h"

namespace yarmproxy {

std::atomic_int redis_mset_cmd_count;

struct RedisMsetCommand::Subquery {
  Subquery(std::shared_ptr<BackendConn> backend,
           const char* data, size_t present_bytes)
      : backend_(backend) {
    segments_.emplace_back(data, present_bytes);
  }

  enum Phase {
    INIT_SEND_QUERY    = 0, // write query for the first time
    SENDING_MORE_QUERY = 1, // sending more query data
    WAITING_MORE_QUERY = 2, // waiting others to launch read-more-query
    READING_MORE_QUERY = 3, // launch read-more-query
    READING_REPLY      = 4, // read reply
  };

  std::shared_ptr<BackendConn> backend_;

  size_t keys_count_ = 1;
  Phase phase_ = INIT_SEND_QUERY;
  bool query_recv_complete_ = false;
  bool backend_error_ = false; // remove it
  std::list<std::pair<const char*, size_t>> segments_;
};

static const std::string& RedisMsetPrefix(size_t keys_count) {
  static std::map<size_t, std::string> prefix_cache {
        {1, "*3\r\n$4\r\nmset\r\n"},
        {2, "*5\r\n$4\r\nmset\r\n"},
        {3, "*7\r\n$4\r\nmset\r\n"},
        {4, "*9\r\n$4\r\nmset\r\n"}
      };
  const auto& it = prefix_cache.find(keys_count);
  if (it != prefix_cache.end()) {
    return it->second;
  }

  std::ostringstream oss;
  oss << '*' << (keys_count * 2 + 1) << "\r\n$4\r\nmset\r\n";
  const auto& new_it = prefix_cache.emplace(keys_count, oss.str()).first;
  return new_it->second;
}

void RedisMsetCommand::PushSubquery(const Endpoint& ep, const char* data,
                                    size_t bytes) {
  const auto& it = waiting_subqueries_.find(ep);
  if (it == waiting_subqueries_.cend()) {
    LOG_DEBUG << "PushSubquery inc_recycle_lock add new endpoint " << ep
              << " , key=" << redis::Bulk(data, bytes).to_string();
    client_conn_->buffer()->inc_recycle_lock();
    auto backend = backend_pool()->Allocate(ep);
    std::shared_ptr<Subquery> query(new Subquery(backend, data, bytes));
    auto res = waiting_subqueries_.emplace(ep, query);
    tail_query_ = res.first->second;
    return;
  }
  tail_query_ = it->second;
  ++(it->second->keys_count_);

  auto& segment = it->second->segments_.back();
  if (segment.first + segment.second == data) {
    LOG_DEBUG << "PushSubquery append adjcent segment, ep=" << ep
              << " key=" << redis::Bulk(data, bytes).to_string();
    segment.second += bytes;
  } else {
    LOG_DEBUG << "PushSubquery add new segment, ep=" << ep
              << " key=" << redis::Bulk(data, bytes).to_string();
    it->second->segments_.emplace_back(data, bytes);
  }
}

RedisMsetCommand::RedisMsetCommand(std::shared_ptr<ClientConnection> client,
                                   const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , total_bulks_(ba.total_bulks())
    , unparsed_bulks_(ba.absent_bulks())
{
  // don't parse the 'key' now if 'value' absent
  unparsed_bulks_ += unparsed_bulks_ % 2;

  for(size_t i = 1; (i + 1) < ba.present_bulks(); i += 2) {
    Endpoint ep = backend_locator()->Locate(
        ba[i].payload_data(), ba[i].payload_size(), ProtocolType::REDIS);
    PushSubquery(ep, ba[i].raw_data(),
        ba[i].present_size() + ba[i+1].present_size());
  }
  LOG_DEBUG << "RedisMsetCommand ctor " << ++redis_mset_cmd_count;
}

RedisMsetCommand::~RedisMsetCommand() {
  LOG_DEBUG << "RedisMsetCommand dtor " << --redis_mset_cmd_count
           << " pending_subqueries_.size=" << pending_subqueries_.size();

  if (pending_subqueries_.size() != 1) {
    assert(client_conn_->aborted());
  }

  for(auto& it : pending_subqueries_) {
    backend_pool()->Release(it.second->backend_);
  }
}

bool RedisMsetCommand::query_recv_complete() {
  return tail_query_->query_recv_complete_ && query_parsing_complete();
}

bool RedisMsetCommand::ContinueWriteQuery() {
  assert(!init_write_query_);
  assert(tail_query_);

  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    LOG_DEBUG << "RedisMsetCommand WriteQuery query_recv_complete_ is true";
    tail_query_->query_recv_complete_ = true;
  }

  //if (tail_query_->backend_error_ || // TODO : connect error if not enough. mset has the same bug
  //    (tail_query_->backend_ && tail_query_->backend_->closed())) {
    if (tail_query_->backend_ && tail_query_->backend_->error()) {
      if (tail_query_->query_recv_complete_) {
        if (unparsed_bulks_ == 0 &&
            waiting_subqueries_.size() == 0 &&
            pending_subqueries_.size() == 1) {
          // should write reply in this subquery
          if (client_conn_->IsFirstCommand(shared_from_this())) {
            LOG_DEBUG << "last pending, try write reply";
            TryWriteReply(tail_query_->backend_);
          } else {
            LOG_DEBUG << "last pending, wait to write reply";
            replying_backend_ = tail_query_->backend_;
          }
        } else {
          // not last pending, don't write reply in this subquery
          pending_subqueries_.erase(tail_query_->backend_);
          backend_pool()->Release(tail_query_->backend_);
        }
        return false;
      } else {
        return true; // no callback, try read more query directly
      }
    }

    LOG_DEBUG << "mset ContinueWriteQuery tail_query_ phase " << tail_query_->phase_
            << " backend=" << tail_query_->backend_
            << " query=" << tail_query_
            << " cmd=" << this
            << " bytes=" << client_conn_->buffer()->unprocessed_bytes();

    assert(tail_query_->phase_ == Subquery::WAITING_MORE_QUERY ||
           tail_query_->phase_ == Subquery::READING_MORE_QUERY);
    if (tail_query_->phase_ == Subquery::WAITING_MORE_QUERY) {
      tail_query_->phase_ = Subquery::READING_MORE_QUERY;
    }
    client_conn_->buffer()->inc_recycle_lock();
    tail_query_->backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
        client_conn_->buffer()->unprocessed_bytes());
    return false;
}

bool RedisMsetCommand::StartWriteQuery() {
  assert(tail_query_);
  if (client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    LOG_DEBUG << "RedisMsetCommand WriteQuery query_recv_complete_ is true";
    tail_query_->query_recv_complete_ = true;
  }
  assert(init_write_query_);
  if (!init_write_query_) {
  //if (tail_query_->backend_error_ || // TODO : connect error if not enough. mset has the same bug
  //    (tail_query_->backend_ && tail_query_->backend_->closed())) {
    if (tail_query_->backend_ && tail_query_->backend_->error()) {
      if (tail_query_->query_recv_complete_) {
        if (unparsed_bulks_ == 0 &&
            waiting_subqueries_.size() == 0 &&
            pending_subqueries_.size() == 1) {
          // should write reply in this subquery
          if (client_conn_->IsFirstCommand(shared_from_this())) {
            LOG_DEBUG << "last pending, try write reply";
            TryWriteReply(tail_query_->backend_);
          } else {
            LOG_DEBUG << "last pending, wait to write reply";
            replying_backend_ = tail_query_->backend_;
          }
        } else {
          // not last pending, don't write reply in this subquery
          pending_subqueries_.erase(tail_query_->backend_);
          backend_pool()->Release(tail_query_->backend_);
        }
        return false;
      } else {
        return true; // no callback, try read more query directly
      }
    }

    LOG_DEBUG << "WriteQuery tail_query_ phase " << tail_query_->phase_
            << " backend=" << tail_query_->backend_
            << " query=" << tail_query_
            << " cmd=" << this
            << " bytes=" << client_conn_->buffer()->unprocessed_bytes();

    assert(tail_query_->phase_ == Subquery::WAITING_MORE_QUERY ||
           tail_query_->phase_ == Subquery::READING_MORE_QUERY);
    if (tail_query_->phase_ == Subquery::WAITING_MORE_QUERY) {
      tail_query_->phase_ = Subquery::READING_MORE_QUERY;
    }
    client_conn_->buffer()->inc_recycle_lock();
    tail_query_->backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
        client_conn_->buffer()->unprocessed_bytes());
    return false;
  }

  init_write_query_ = false;
  ActivateWaitingSubquery();
  return false;
}

void RedisMsetCommand::OnBackendRecoverableError(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(BackendErrorRecoverable(backend, ec));
  LOG_DEBUG << "redismset OnBackendRecoverableError ec="
            << ErrorCodeString(ec) << " backend=" << backend
            << " endpoint=" << backend->remote_endpoint();
  auto& err_reply(RedisErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  auto& subquery = pending_subqueries_[backend];
  if (subquery->query_recv_complete_) {
    if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
      if (client_conn_->IsFirstCommand(shared_from_this())) {
        LOG_DEBUG << "redismset OnBackendConnectError write reply, ep="
                  << subquery->backend_->remote_endpoint();
        TryWriteReply(backend);
      } else {
        LOG_DEBUG << "redismset OnBackendConnectError waiting to write reply, ep="
                  << subquery->backend_->remote_endpoint();
        replying_backend_ = backend;
      }
    } else {
      LOG_DEBUG << "RedisMsetCommand OnBackendConnectError need not reply, ep="
                << subquery->backend_->remote_endpoint()
                << " is_tail=" << (subquery == tail_query_)
                << " pending_subqueries_.size=" << pending_subqueries_.size();
      backend->set_reply_recv_complete();
      backend->set_no_recycle();
      pending_subqueries_.erase(backend);
      backend_pool()->Release(backend);
    }
  } else {
    subquery->backend_error_ = true; // waiting for more query
    LOG_DEBUG << "RedisMsetCommand OnBackendConnectError backend_error_, waiting for more query, ep="
              << subquery->backend_->remote_endpoint();
  }
}

void RedisMsetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                              ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "Command::OnBackendReplyReceived error, backend=" << backend;
    // TODO : more friendly reply on recoverable error
    client_conn_->Abort();
    return;
  }

  LOG_DEBUG << "Command::OnBackendReplyReceived ok, backend=" << backend << " cmd=" << this;
  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  assert(waiting_subqueries_.size() == 0);
  if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      LOG_DEBUG << "OnBackendReplyReceived query="
               << backend->remote_endpoint()
               << " command=" << this
               << " write reply, backend=" << backend;
      TryWriteReply(backend);
    } else {
      LOG_DEBUG << "OnBackendReplyReceived query="
               << backend->remote_endpoint()
               << " command=" << this
               << " waiting to write reply, backend=" << backend;
      replying_backend_ = backend;
    }
  } else {
    assert(backend->buffer()->unparsed_bytes() == 0);
    backend->buffer()->update_processed_bytes(
        backend->buffer()->unprocessed_bytes());
    // backend->set_reply_recv_complete();
    assert(backend->reply_recv_complete());
    pending_subqueries_.erase(backend);
    backend_pool()->Release(backend);

    if (!client_conn_->buffer()->recycle_locked() &&
        unparsed_bulks_ > 0) {
      client_conn_->TryReadMoreQuery("redis_mset_6");
    }

    LOG_DEBUG << "OnBackendReplyReceived command=" << this
             << " unparsed_bulks_=" << unparsed_bulks_
             << " pending_subqueries_.size=" << pending_subqueries_.size()
             << " don't write reply, backend=" << backend;
  }
}

/*
void RedisMsetCommand::StartWriteReply() {
  if (replying_backend_) {
    TryWriteReply(replying_backend_);
    LOG_DEBUG << "StartWriteReply TryWriteReply called";
  }
}
*/
// try to keep pace with parent class impl
void RedisMsetCommand::OnWriteQueryFinished(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      OnBackendRecoverableError(backend, ec);
      // 等同于转发完成已收数据
      client_conn_->buffer()->dec_recycle_lock();

      if (!client_conn_->buffer()->recycle_locked() && // 全部完成write query 才能释放recycle_lock
          (!tail_query_->query_recv_complete_ || !query_parsing_complete())) {
        assert(client_conn_->buffer()->parsed_unreceived_bytes() > 0 ||
               !query_parsing_complete());
        assert(client_conn_->buffer()->recycle_lock_count() == 0);
        client_conn_->TryReadMoreQuery("redis_mset_1");
        LOG_DEBUG << "OnWriteQueryFinished E_CONNECT TryReadMoreQuery";
      }
    } else {
      client_conn_->Abort();
      LOG_DEBUG << "OnWriteQueryFinished error, ec=" << ErrorCodeString(ec);
    }
    return;
  }

  auto& query = pending_subqueries_[backend];
  switch(query->phase_) {
  case Subquery::INIT_SEND_QUERY: // TODO : use switch & state machine
    query->phase_ = Subquery::SENDING_MORE_QUERY; // sending more query data
    assert(!query->segments_.empty());
    query->backend_->WriteQuery(query->segments_.front().first,
                                query->segments_.front().second);
    LOG_DEBUG << "OnWriteQueryFinished backend=" << query->backend_
              << " ep=" << query->backend_->remote_endpoint()
              << " phase 1 finished, phase 2 launched";
    return;
  case Subquery::SENDING_MORE_QUERY:
    query->segments_.pop_front();
    if (!query->segments_.empty()) {
      // phase_ is still SENDING_MORE_QUERY
      query->backend_->WriteQuery(query->segments_.front().first,
                                  query->segments_.front().second);
      return;
    }
    client_conn_->buffer()->dec_recycle_lock();

    if (query == tail_query_) {
      if (query->query_recv_complete_) {
        query->phase_ = Subquery::READING_REPLY;
        backend->ReadReply();
      } else {
        if (!client_conn_->buffer()->recycle_locked()) {
          query->phase_ = Subquery::READING_MORE_QUERY;
          client_conn_->TryReadMoreQuery("redis_mset_2");
        } else {
          assert(query->phase_ == Subquery::SENDING_MORE_QUERY);
          query->phase_ = Subquery::WAITING_MORE_QUERY;
        }
      }
    } else {
      query->phase_ = Subquery::READING_REPLY;
      backend->ReadReply();
      if (!client_conn_->buffer()->recycle_locked()) {
        if (!tail_query_->query_recv_complete_) {
          tail_query_->phase_ = Subquery::READING_MORE_QUERY;
          client_conn_->TryReadMoreQuery("redis_mset_3");
        }
      }
    }
    return;
  case Subquery::READING_MORE_QUERY:
    client_conn_->buffer()->dec_recycle_lock();
    if (query->query_recv_complete_) {
      query->phase_ = Subquery::READING_REPLY;
      backend->ReadReply();
    } else {
      assert(query == tail_query_);
      assert(client_conn_->buffer()->recycle_lock_count() == 0);
      client_conn_->TryReadMoreQuery("redis_mset_4");
    }
    return;
  default:
    LOG_WARN << "OnWriteQueryFinished bad phase " << query->phase_
             << " , backend=" << backend;
    assert(false);
    client_conn_->Abort();
    return;
  }
}

bool RedisMsetCommand::query_parsing_complete() {
  return unparsed_bulks_ == 0;
}

void RedisMsetCommand::ActivateWaitingSubquery() {
  for(auto& it : waiting_subqueries_) {
    auto& query = it.second;
    auto backend = query->backend_;
    assert(backend);

    backend->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, backend),
        WeakBind(&Command::OnBackendReplyReceived, backend));

    pending_subqueries_[backend] = query;
    if (query != tail_query_ ||
        client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
      query->query_recv_complete_ = true;
    }
    const std::string& mset_prefix = RedisMsetPrefix(query->keys_count_);
    backend->WriteQuery(mset_prefix.data(), mset_prefix.size());
  }
  waiting_subqueries_.clear();
}

// TODO : merge with ctor parsing
bool RedisMsetCommand::ProcessUnparsedPart() {
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

    if (bulk.present_size() == 0) {
      new_bulks.pop_back();
      break;
    }
    total_parsed += bulk.total_size();
    LOG_DEBUG << "ProcessUnparsedPart current bulk parsed_bytes="
              << bulk.total_size();
  }

  if (new_bulks.size() % 2 == 1) {
    total_parsed -= new_bulks.back().total_size();
    new_bulks.pop_back();
  }
  LOG_DEBUG << "ProcessUnparsedPart new_bulks.size=" << new_bulks.size()
            << " total_parsed=" << total_parsed;

  if (new_bulks.size() == 0) {
    // assert(client_conn_->buffer()->recycle_lock_count() == 0);
    if (!client_conn_->buffer()->recycle_locked()) {
      client_conn_->TryReadMoreQuery("redis_mset_5");
    }
    return true;
  }

  LOG_DEBUG << "ProcessUnparsedPart new_bulks.size=" << new_bulks.size()
            << " unparsed_bulks_=" << unparsed_bulks_;

  size_t to_process_bytes = 0;
  for(size_t i = 0; i + 1 < new_bulks.size(); i += 2) {
    Endpoint ep = backend_locator()->Locate(
                                new_bulks[i].payload_data(),
                                new_bulks[i].payload_size(),
                                ProtocolType::REDIS);
    to_process_bytes += new_bulks[i].present_size();
    to_process_bytes += new_bulks[i + 1].present_size();
    PushSubquery(ep, new_bulks[i].raw_data(),
        new_bulks[i].present_size() + new_bulks[i+1].present_size());
  }

  assert(total_parsed >= to_process_bytes);
  buffer->update_processed_bytes(to_process_bytes);
  buffer->update_parsed_bytes(total_parsed);
  unparsed_bulks_ -= new_bulks.size();
  ActivateWaitingSubquery();
  return true;
}

/*
bool RedisMsetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != '+' && entry[0] != '-' && entry[0] != '$') {
    return false;
  }

  const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    return true;
  }
  if (entry[0] == '$' && entry[1] != '-') {
    p = static_cast<const char *>(memchr(p + 1, '\n', entry + unparsed - p));
    if (p == nullptr) {
      return true;
    }
  }

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  assert(unparsed == p - entry + 1);

  assert(backend->buffer()->unparsed_bytes() == 0);
  backend->set_reply_recv_complete();
  return true;
}
*/

}

