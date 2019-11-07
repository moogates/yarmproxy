#include "redis_del_command.h"

#include "logging.h"

#include "backend_conn.h"
#include "key_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "redis_protocol.h"

namespace yarmproxy {

struct RedisDelCommand::Subquery {
  Subquery(std::shared_ptr<BackendConn> backend,
              const char* data, size_t present_bytes)
      : backend_(backend)
      , keys_count_(1) {
    segments_.emplace_back(data, present_bytes);
  }

  std::shared_ptr<BackendConn> backend_;

  size_t keys_count_;
  size_t phase_ = 0;
  bool connect_error_ = false;
  std::list<std::pair<const char*, size_t>> segments_;
};

static const std::string& RedisDelPrefix(const std::string& cmd_name,
                                        size_t keys_count) {
  static std::map<size_t, std::string> del_prefix_cache {
        {1, "*2\r\n$3\r\ndel\r\n"},
        {2, "*3\r\n$3\r\ndel\r\n"},
        {3, "*4\r\n$3\r\ndel\r\n"},
        {4, "*5\r\n$3\r\ndel\r\n"}
      };
  static std::map<size_t, std::string> exists_prefix_cache;
  static std::map<size_t, std::string> touch_prefix_cache;

  std::map<size_t, std::string>* prefix_cache = nullptr;
  if (cmd_name == "del") {
    prefix_cache = &del_prefix_cache;
  } else if (cmd_name == "exists") {
    prefix_cache = &exists_prefix_cache;
  } else if (cmd_name == "touch") {
    prefix_cache = &touch_prefix_cache;
  } else {
    assert(false);
  }

  const auto& it = prefix_cache->find(keys_count);
  if (it != prefix_cache->end()) {
    return it->second;
  }

  std::ostringstream oss;
  oss << '*' << (keys_count + 1) << "\r\n$" << cmd_name.size()
      << "\r\n" << cmd_name << "\r\n";
  const auto& new_it = prefix_cache->emplace(keys_count, oss.str()).first;
  return new_it->second;
}


void RedisDelCommand::PushSubquery(const Endpoint& ep, const char* data,
       size_t bytes) {
  const auto& it = waiting_subqueries_.find(ep);
  if (it == waiting_subqueries_.cend()) {
    LOG_DEBUG << "PushSubquery inc_recycle_lock add new endpoint " << ep
              << " , key=" << redis::Bulk(data, bytes).to_string();
    client_conn_->buffer()->inc_recycle_lock();
    auto backend = backend_pool()->Allocate(ep);
    std::shared_ptr<Subquery> query(new Subquery(backend, data, bytes));
    waiting_subqueries_.emplace(ep, query);
    return;
  }
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

RedisDelCommand::RedisDelCommand(std::shared_ptr<ClientConnection> client,
                                 const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , unparsed_bulks_(ba.absent_bulks())
{
  for(const char* p = ba[0].payload_data();
      p - ba[0].payload_data() < int(ba[0].payload_size());
      ++p) {
    cmd_name_.push_back(std::tolower(*p));
  }
  for(size_t i = 1; i < ba.present_bulks(); ++i) {
    if (i == ba.present_bulks() - 1 && !ba[i].completed()) {
      ++unparsed_bulks_; // don't parse the last key if it's not complete
      break;
    }
    Endpoint ep = key_locator()->Locate(
        ba[i].payload_data(), ba[i].payload_size(), ProtocolType::REDIS);
    PushSubquery(ep, ba[i].raw_data(), ba[i].present_size());
  }
}

RedisDelCommand::~RedisDelCommand() {
  if (pending_subqueries_.size() != 1) {
    assert(client_conn_->aborted());
  }
  for(auto& it : pending_subqueries_) {
    backend_pool()->Release(it.second->backend_);
  }
}

bool RedisDelCommand::query_recv_complete() {
  return unparsed_bulks_ == 0; // 只解析completed bulk, 因而解析完就是接收完
}

bool RedisDelCommand::StartWriteQuery() {
  assert(init_write_query_);
  init_write_query_ = false;
  ActivateWaitingSubquery();
  return false;
}

static void SetBackendReplyCount(std::shared_ptr<BackendConn> backend,
                                 size_t count) {
  std::ostringstream oss;
  oss << ":" << count << "\r\n";
  backend->SetReplyData(oss.str().data(), oss.str().size());
}

void RedisDelCommand::OnBackendRecoverableError(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  LOG_DEBUG << "RedisDelCommand::OnBackendRecoverableError ec="
           << ErrorCodeString(ec) << "backend=" << backend;
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
    SetBackendReplyCount(backend, total_del_count_);
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(backend);
    } else {
      // waiting to write reply
      replying_backend_ = backend;
    }
  } else {
    // need not reply
    pending_subqueries_.erase(backend);
    backend_pool()->Release(backend);
  }
}

void RedisDelCommand::OnBackendReplyReceived(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  LOG_DEBUG << "Command::OnBackendReplyReceived ok, backend="
            << backend << " cmd=" << this;
  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  if (unparsed_bulks_ == 0 && pending_subqueries_.size() == 1) {
    SetBackendReplyCount(backend, total_del_count_);
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
    backend->buffer()->update_processed_bytes(backend->buffer()->unprocessed_bytes());
    pending_subqueries_.erase(backend);
    backend_pool()->Release(backend);

    if (!client_conn_->buffer()->recycle_locked() && unparsed_bulks_ > 0) {
      client_conn_->TryReadMoreQuery("redis_del_1");
    }

    LOG_DEBUG << "OnBackendReplyReceived command=" << this
             << " unparsed_bulks_=" << unparsed_bulks_
             << " pending_subqueries_.size=" << pending_subqueries_.size()
             << " don't write reply, backend=" << backend;
  }
}

// try to keep pace with parent class impl
void RedisDelCommand::OnWriteQueryFinished(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      OnBackendRecoverableError(backend, ec);
      // 等同于转发完成已收数据
      client_conn_->buffer()->dec_recycle_lock();
      if (!client_conn_->buffer()->recycle_locked() && // 全部完成write query 才能释放recycle_lock
          !query_recv_complete()) {
        client_conn_->TryReadMoreQuery("redis_del_2");
        LOG_DEBUG << "OnWriteQueryFinished E_CONNECT TryReadMoreQuery";
      }
    } else {
      client_conn_->Abort();
      LOG_DEBUG << "OnWriteQueryFinished error, ec=" << ErrorCodeString(ec);
    }
    return;
  }

  auto& query = pending_subqueries_[backend];
  if (query->phase_ == 0) {
    query->phase_ = 1; // SendingQueryData
    assert(!query->segments_.empty());
    query->backend_->WriteQuery(query->segments_.front().first,
                                query->segments_.front().second);
  } else if (query->phase_ == 1) {
    query->segments_.pop_front();
    if (query->segments_.empty()) {
      client_conn_->buffer()->dec_recycle_lock();
      query->phase_ = 2; // read reply
      backend->ReadReply();
    } else {
      query->backend_->WriteQuery(query->segments_.front().first,
                                  query->segments_.front().second);
    }
  } else {
    assert(false);
  }
}

bool RedisDelCommand::query_parsing_complete() {
  return unparsed_bulks_ == 0;
}

void RedisDelCommand::ActivateWaitingSubquery() {
  for(auto& it : waiting_subqueries_) {
    auto query = it.second;
    auto backend = query->backend_;
    assert(backend);
    backend->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, backend),
        WeakBind(&Command::OnBackendReplyReceived, backend));

    pending_subqueries_[backend] = query;
    LOG_DEBUG << "ActivateWaitingSubquery client=" << client_conn_
             << " cmd=" << this << " query=" << query
             << " phase=" << query->phase_
             << " backend=" << backend;
    const auto& mset_prefix = RedisDelPrefix(cmd_name_, query->keys_count_);
    backend->WriteQuery(mset_prefix.data(), mset_prefix.size());
  }
  waiting_subqueries_.clear();
}

bool RedisDelCommand::ProcessUnparsedPart() {
  ReadBuffer* buffer = client_conn_->buffer();
  std::vector<redis::Bulk> new_bulks;

  int parsed_bytes = redis::BulkArray::ParseBulkItems(buffer->unparsed_data(),
       buffer->unparsed_received_bytes(), unparsed_bulks_, &new_bulks);
  if (parsed_bytes < 0) {
    LOG_INFO << "redisdel ProcessUnparsedPart parse error. cmd=" << this;
    return false;
  }
  if (new_bulks.size() > 0 && !new_bulks.back().completed()) {
    parsed_bytes -= new_bulks.back().total_size();
    new_bulks.pop_back();
  }

  if (new_bulks.empty()) {
    if (!client_conn_->buffer()->recycle_locked()) {
      client_conn_->TryReadMoreQuery("redis_del_3");
    }
    return true;
  }

  LOG_DEBUG << "ProcessUnparsedPart new_bulks.size=" << new_bulks.size()
            << " unparsed_bulks_=" << unparsed_bulks_;
  unparsed_bulks_ -= new_bulks.size();

  for(size_t i = 0; i < new_bulks.size(); ++i) {
    Endpoint ep = key_locator()->Locate(new_bulks[i].payload_data(),
        new_bulks[i].payload_size(), ProtocolType::REDIS);
    PushSubquery(ep, new_bulks[i].raw_data(), new_bulks[i].present_size());
  }

  buffer->update_processed_bytes(parsed_bytes);
  buffer->update_parsed_bytes(parsed_bytes);
  ActivateWaitingSubquery();
  return true;
}

bool RedisDelCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != ':') {
    LOG_WARN << "RedisDelCommand ParseReply error ["
             << std::string(entry, unparsed) << "]";
    return false;
  }

  auto p = static_cast<const char *>(memchr(entry, '\n', unparsed));
  if (p == nullptr) {
    return true;
  }

  int del_count;
  try {
    del_count = std::stoi(entry + 1);
  } catch (...) {
    return false;
  }
  total_del_count_ += del_count;

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisDelCommand ParseReply complete, resp.size="
            << p - entry + 1 << " backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

