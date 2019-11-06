#include "redis_mget2_command.h"

#include "logging.h"

#include "error_code.h"
#include "backend_conn.h"
#include "key_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"

namespace yarmproxy {

// TODO: supprt incomplete MGET command
std::atomic_int redis_mget2_cmd_count;

struct RedisMget2Command::Subquery {
  Subquery(std::shared_ptr<BackendConn> backend) : backend_(backend) {
  }
  std::shared_ptr<BackendConn> backend_;
  std::string query_data_;

  size_t key_count_ = 0;
  size_t reply_absent_bulks_ = 0;
};

RedisMget2Command::RedisMget2Command(std::shared_ptr<ClientConnection> client,
                                   const redis::BulkArray& ba)
    : Command(client, ProtocolType::REDIS)
    , reply_prefix_(redis::BulkArray::SerializePrefix(ba.total_bulks() - 1))
{
  for(size_t i = 1; i < ba.total_bulks(); ++i) {
    const redis::Bulk& bulk = ba[i];
    Endpoint endpoint = key_locator()->Locate(
        bulk.payload_data(), bulk.payload_size(), ProtocolType::REDIS);

    LOG_DEBUG << "ParseQuery key=" << bulk.to_string()
              << " ep=" << endpoint;
    std::shared_ptr<Subquery> subquery;
    auto it = subqueries_.find(endpoint);
    if (it != subqueries_.cend()) {
      subquery = it->second;
    } else {
      auto backend = backend_pool()->Allocate(endpoint);
      subquery.reset(new Subquery(backend));
      subqueries_.emplace(endpoint, subquery);
    }
    ++subquery->key_count_;
    subquery->query_data_.append(bulk.raw_data(), bulk.total_size());
    if (waiting_reply_queue_.empty() ||
        waiting_reply_queue_.back().first != subquery->backend_) {
      waiting_reply_queue_.emplace_back(subquery->backend_, 1);
    } else {
      ++waiting_reply_queue_.back().second;
    }
  }
  LOG_DEBUG << "RedisMget2Command ctor, count=" << ++redis_mget2_cmd_count
            << " reply_prefix_=" << reply_prefix_;
}

RedisMget2Command::~RedisMget2Command() {
  for(auto& it : subqueries_) {
    if (!it.second->backend_) {
      LOG_DEBUG << "RedisMget2Command dtor Release null backend";
    } else {
      LOG_DEBUG << "RedisMget2Command dtor Release backend";
      backend_pool()->Release(it.second->backend_);
    }
  }
  LOG_DEBUG << "RedisMget2Command " << this << " dtor, count=" << --redis_mget2_cmd_count;
}

bool RedisMget2Command::StartWriteQuery() {
  for(auto& it : subqueries_) {
    auto& query = it.second;
    auto backend = query->backend_;
    assert(backend);
    backend->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, backend),
        WeakBind(&Command::OnBackendReplyReceived, backend));

    // TODO : too much memory copy
    std::string prefix(redis::BulkArray::SerializePrefix(query->key_count_ + 1));
    prefix.append("$4\r\nmget\r\n");
    query->query_data_ =  prefix + query->query_data_;

    LOG_DEBUG << "RedisMget2Command StartWrireQuery cmd=" << this
              << " subquery=" << query
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size()
              << " backend=" << query->backend_->remote_endpoint()
              << " keys=" << query->key_count_
              << " data=[" << query->query_data_ << "]";

    query->backend_->WriteQuery(query->query_data_.data(),
                                query->query_data_.size());
  }
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    StartWriteReply();
  } else {
    LOG_DEBUG << "RedisMget2Command no StartWriteReply cmd=" << this;
  }
  return false;
}

void RedisMget2Command::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  LOG_DEBUG << "RedisMget2Command " << this
            << " OnWriteReplyFinished, backend=" << backend
            << " ec=" << ErrorCodeString(ec);
  if (backend == nullptr) {
    if (ec != ErrorCode::E_SUCCESS) {
      client_conn_->Abort();
      return;
    }
    set_reply_prefix_complete();

    LOG_DEBUG << "RedisMget2Command ReplyPrefix complete, backend=" << backend;
    NextBackendStartReply();
    return;
  }

  if (ec != ErrorCode::E_SUCCESS) {
    LOG_WARN << "Command::OnWriteReplyFinished error, backend="
             << backend;
    client_conn_->Abort();
    return;
  }

 LOG_DEBUG << "mget Command " << this << " OnWriteReplyFinished ok, backend="
          << backend << " backend->finished=" << backend->finished()
          << " backend->unprocessed=" << backend->buffer()->unprocessed_bytes()
          << " backend_buf=" << backend->buffer();
  assert(is_writing_reply_);
  is_writing_reply_ = false;
  backend->buffer()->dec_recycle_lock();
  assert(backend == waiting_reply_queue_.front().first);
  assert(backend == replying_backend_);

  if (backend->buffer()->parsed_unreceived_bytes() == 0) {
    LOG_DEBUG << "mget Command waiting_reply_queue_ front bulk count "
             << waiting_reply_queue_.front().second;
    if (waiting_reply_queue_.front().second == 0) {
      waiting_reply_queue_.pop_front();
    }
    RotateReplyingBackend();
  } else {
    backend->TryReadMoreReply();
  }
}

void RedisMget2Command::BackendReadyToReply(
    std::shared_ptr<BackendConn> backend) {
  if (!client_conn_->IsFirstCommand(shared_from_this())) {
    return;
  }

  if (waiting_reply_queue_.empty()) {
    // bad data
    client_conn_->Abort();
    return;
  }

  if (!reply_prefix_complete()) {
    return;
  }
  if (backend != waiting_reply_queue_.front().first) {
    return;
  }

  replying_backend_ = backend;
  bool ret = ParseReply(backend);
  if (!ret) {
    // LOG_WARN << "RedisMget parse error, backend=" << backend;
    client_conn_->Abort();
    return;
  }
  if (backend->buffer()->unprocessed_bytes() == 0) {
    LOG_DEBUG << "RedisMget waiting for data from backend=" << backend;
    backend->TryReadMoreReply();
    return;
  }

  // assert(!backend->finished()); // TODO : checkou error condition
  LOG_DEBUG << "RedisMget backend=" << backend
      << " unprocessed_bytes=" << backend->buffer()->unprocessed_bytes()
      << " parsed_unreceived_bytes=" << backend->buffer()->parsed_unreceived_bytes()
      << " unparsed_bytes=" << backend->buffer()->unparsed_bytes();
  TryWriteReply(backend);
}

void RedisMget2Command::OnBackendReplyReceived(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_DEBUG << "OnBackendReplyReceived error, backend=" << backend;
    if (backend == replying_backend_) { // the backend is replying
      client_conn_->Abort();
    } else {
      OnBackendRecoverableError(backend, ec);
    }
    return;
  }

  LOG_DEBUG << "OnBackendReplyReceived ok, endpoint="
           << backend->remote_endpoint() << " backend=" << backend;
  BackendReadyToReply(backend);
}

static std::string ErrorReplyBody(size_t keys) {
  std::ostringstream oss;
  // oss << "*" << keys << "\r\n";
  for(size_t i = 0; i < keys; ++i) {
    oss << "$-1\r\n";
  }
  return oss.str();
}

void RedisMget2Command::OnBackendRecoverableError(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  auto err_reply = ErrorReplyBody(
                       subqueries_[backend->remote_endpoint()]->key_count_);
  backend->SetReplyData(err_reply.data(), err_reply.size());
  LOG_DEBUG << "RedisMget2Command " << this
           << " OnBackendRecoverableError backend=" << backend
           << " ec=" << ErrorCodeString(ec)
           << " err_reply=[" << err_reply << "]";

  backend->set_reply_recv_complete();
  backend->set_no_recycle();
  BackendReadyToReply(backend);
}

void RedisMget2Command::StartWriteReply() {
  LOG_DEBUG << "RedisMget2Command " << this << " StartWriteReply";
  client_conn_->WriteReply(reply_prefix_.data(), reply_prefix_.size(),
          WeakBind(&Command::OnWriteReplyFinished, nullptr));
}

void RedisMget2Command::NextBackendStartReply() {
  LOG_DEBUG << "RedisMget2Command " << this << " NextBackendStartReply"
            << " last_replying_backend_=" << replying_backend_;
  if (!reply_prefix_complete()) {
    return;
  }

  assert(!waiting_reply_queue_.empty());
  auto front = waiting_reply_queue_.front().first;
  if (front->buffer()->unparsed_bytes() > 0) {
    BackendReadyToReply(front);
  }
}

bool RedisMget2Command::BackendErrorRecoverable(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  return !backend->has_read_some_reply();
}

void RedisMget2Command::RotateReplyingBackend() {
  if (waiting_reply_queue_.empty()) {
    LOG_DEBUG << "RedisMget2Command " << this << " Rotate to next COMMAND";
    client_conn_->RotateReplyingCommand();
  } else {
    LOG_DEBUG << "RedisMget2Command " << this << " Rotate to next backend";
    NextBackendStartReply();
  }
}

bool RedisMget2Command::ParseReply(std::shared_ptr<BackendConn> backend) {
  if (backend->buffer()->unprocessed_bytes() > 0) {
    if (backend->buffer()->parsed_unreceived_bytes() == 0 &&
        subqueries_[backend->remote_endpoint()]->reply_absent_bulks_ == 0) {
      backend->set_reply_recv_complete();
    }
    return true;
  }

  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();

    size_t& absent_bulks =
        subqueries_[backend->remote_endpoint()]->reply_absent_bulks_;
    if (absent_bulks == 0) {
      redis::BulkArray bulk_array(entry, unparsed_bytes);
      if (bulk_array.parsed_size() < 0) {
        LOG_DEBUG << "RedisMget ParseReply error, backend=" << backend;
        return false;
      }
      if (bulk_array.parsed_size() == 0 || bulk_array.present_bulks() == 0) {
        LOG_DEBUG << "RedisMget ParseReply need more data, backend=" << backend;
        return true;
      }
      backend->buffer()->update_parsed_bytes(bulk_array[0].raw_data()
          + bulk_array[0].total_size() - bulk_array.raw_data());
      backend->buffer()->update_processed_offset(
          bulk_array[0].raw_data() - bulk_array.raw_data());

      LOG_DEBUG << "RedisMget ParseReply skip prefix data="
               << std::string(bulk_array.raw_data(),
                   bulk_array[0].raw_data() - bulk_array.raw_data() - 2)
               << ", unparsed_bytes=" << backend->buffer()->unparsed_bytes()
               << ", unprocessed_bytes=" << backend->buffer()->unprocessed_bytes()
               << ", parsed_unreceived_bytes="
               << backend->buffer()->parsed_unreceived_bytes()
               << ", backend=" << backend;

      size_t total_bulks = bulk_array.total_bulks();
      if (total_bulks == 0) {
        LOG_WARN << "RedisMget ParseReply total_bulks error, backend=" << backend;
        return false;
      }
      // absent_bulks = bulk_array.absent_bulks();
      absent_bulks = bulk_array.total_bulks() - 1;
      LOG_DEBUG << "RedisMget ParseReply absent_bulks="
                << absent_bulks << ", backend=" << backend;
      if (absent_bulks == 0 && bulk_array.completed()) {
        LOG_DEBUG << "RedisMget ParseReply completed, backend=" << backend;
        backend->set_reply_recv_complete();
      }
    } else {
      redis::Bulk bulk(entry, unparsed_bytes);
      if (bulk.present_size() < 0) {
        return false;
      }
      if (bulk.present_size() == 0) {
        return true;
      }
      backend->buffer()->update_parsed_bytes(bulk.total_size());

      if (--absent_bulks == 0 && bulk.completed()) {
        backend->set_reply_recv_complete();
      }
    }
    LOG_DEBUG << "RedisMget ParseReply waiting_reply_queue_ front_count="
              << waiting_reply_queue_.front().second << ", backend=" << backend;
    assert(waiting_reply_queue_.front().second > 0);
    if (--waiting_reply_queue_.front().second == 0) {
      break;
    }
  //break;
  }

  return true;
}

}

