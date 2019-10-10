#include "mc_get_command.h"

#include "logging.h"

#include "error_code.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

struct MemcachedGetCommand::BackendQuery {
  BackendQuery(const Endpoint& ep, std::string&& query_data)
      : query_data_(query_data)
      , backend_endpoint_(ep) {
  }
  std::string query_data_;
  Endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
};

MemcachedGetCommand::MemcachedGetCommand(std::shared_ptr<ClientConnection> client,
                     const char* cmd_data, size_t cmd_size)
    : Command(client)
{
  ParseQuery(cmd_data, cmd_size);
}

MemcachedGetCommand::~MemcachedGetCommand() {
  for(auto& query : subqueries_) {
    if (query->backend_conn_) {
      backend_pool()->Release(query->backend_conn_);
    }
  }
}

void MemcachedGetCommand::ParseQuery(const char* cmd_data, size_t cmd_size) {
  std::map<Endpoint, std::string> ep_keys;
  for(const char* p = cmd_data + 4/*strlen("get ")*/;
      p < cmd_data + cmd_size - 2/*strlen("\r\n")*/; ++p) {
    const char* q = p;
    while(*q != ' ' && *q != '\r') {
      ++q;
    }
    auto ep = backend_locator()->Locate(p, q - p, ProtocolType::MEMCACHED);
    auto it = ep_keys.find(ep);
    if (it == ep_keys.end()) {
      it = ep_keys.emplace(ep, "get").first; // TODO : don't use insert anymore
    }

    it->second.append(p - 1, 1 + q - p);
    p = q;
  }
  for(auto& it : ep_keys) {
    it.second.append("\r\n"); // TODO : may I only iterate over values?
    subqueries_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
}

bool MemcachedGetCommand::WriteQuery() {
  for(auto& query : subqueries_) {
    assert(!query->backend_conn_);
    query->backend_conn_ = AllocateBackend(query->backend_endpoint_);
    query->backend_conn_->WriteQuery(query->query_data_.data(),
                                     query->query_data_.size());
  }
  return false;
}

void MemcachedGetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == subqueries_.size()) {
      last_backend_ = backend;
    }
  }
}

bool MemcachedGetCommand::TryActivateReplyingBackend(
        std::shared_ptr<BackendConn> backend) {
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    return true;
  }
  return backend == replying_backend_;
}

void MemcachedGetCommand::BackendReadyToReply(std::shared_ptr<BackendConn> backend) {
  if (client_conn_->IsFirstCommand(shared_from_this())
      && TryActivateReplyingBackend(backend)) {
    if (backend->finished()) {
      // newly received data might needn't forwarding, eg. some "END\r\n"
      RotateReplyingBackend(backend->recyclable());
    } else {
      TryWriteReply(backend);
    }
  } else {
    if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(),
                  backend) == waiting_reply_queue_.end()) {
      waiting_reply_queue_.push_back(backend);
    }
  }
}

void MemcachedGetCommand::OnBackendReplyReceived(
        std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  TryMarkLastBackend(backend);
  if (ec != ErrorCode::E_SUCCESS
      || ParseReply(backend) == false) {
    LOG_WARN << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  BackendReadyToReply(backend);
  backend->TryReadMoreReply();
}


void MemcachedGetCommand::OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  LOG_DEBUG << "MemcachedGetCommand::OnBackendConnectError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  TryMarkLastBackend(backend);

  if (backend == last_backend_) {
    if (subqueries_.size() > 1) {
      static const char END_RN[] = "END\r\n"; // TODO : 统一放置错误码
      backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
    } else {
      static const char CONNECT_ERROR[] = "SERVER_ERROR: Backend Connect Fail\r\n"; // TODO : 统一放置错误码
      backend->SetReplyData(CONNECT_ERROR, sizeof(CONNECT_ERROR) - 1);
    }
    LOG_DEBUG << "MemcachedGetCommand::OnBackendConnectError last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  } else {
    // ++completed_backends_;
    LOG_DEBUG << "MemcachedGetCommand::OnBackendConnectError not last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  }
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  BackendReadyToReply(backend);
}

void MemcachedGetCommand::StartWriteReply() {
  NextBackendStartReply();
}

void MemcachedGetCommand::NextBackendStartReply() {
  LOG_WARN << "NextBackendStartReply cmd=" << this
            << " last replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    auto next_backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();

    LOG_WARN << "NextBackendStartReply activate ready backend,"
              << " subqueries_.size=" << subqueries_.size()
              << " completed_backends_=" << completed_backends_
              << " backend=" << next_backend;
    if (next_backend->finished()) {
      RotateReplyingBackend(next_backend->recyclable());
    } else {
      TryWriteReply(next_backend);
      replying_backend_ = next_backend;
    }
  } else {
    replying_backend_ = nullptr;
  }
}

bool MemcachedGetCommand::HasUnfinishedBanckends() const {
  return completed_backends_ < subqueries_.size();
}

void MemcachedGetCommand::RotateReplyingBackend(bool) {
  ++completed_backends_;
  if (HasUnfinishedBanckends()) {
  // if (HasUnfinishedBanckends() || waiting_reply_queue_.size() > 0) {
    LOG_DEBUG << "MemcachedGetCommand::Rotate to next backend, HasUnfinishedBanckends="
              << HasUnfinishedBanckends()
              << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
    NextBackendStartReply();
  } else {
    LOG_WARN << "MemcachedGetCommand::Rotate to next COMMAND"
             << " completed_backends_=" << completed_backends_
             << " waiting_reply_queue_.size=" << waiting_reply_queue_.size();
    client_conn_->RotateReplyingCommand();
  }
}

size_t MemcachedGetCommand::ParseReplyBodyBytes(const char * data, const char * end) {
  // "VALUE <key> <flag> <bytes>\r\n"
  const char * p = data + sizeof("VALUE ");
  int count = 0;
  while(p != end) {
    if (*p == ' ') {
      if (++count == 2) {
        return std::stoi(p + 1);
      }
    }
    ++p;
  }
  return 0;
}

bool MemcachedGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
    const char * p = static_cast<const char *>(memchr(entry, '\n', unparsed_bytes));
    if (p == nullptr) {
      LOG_DEBUG << "ParseReply no enough data for parsing, please read more,"
                << " unparsed_bytes=" << backend->buffer()->unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = ParseReplyBodyBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;
      backend->buffer()->update_parsed_bytes(entry_bytes);
      // return true; // 这里如果return, 则每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0
          && backend->buffer()->unparsed_bytes() == (sizeof("END\r\n") - 1)) {
        backend->set_reply_recv_complete();
        if (backend == last_backend_) {
          backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
          LOG_DEBUG << "ParseReply END, is last, backend=" << backend
                   << " unprocessed_bytes=" << backend->buffer()->unprocessed_bytes();
        } else {
          // backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1); // for debug only
          backend->buffer()->cut_received_tail(sizeof("END\r\n") - 1);
          LOG_DEBUG << "ParseReply END, is not last, backend=" << backend
                   << " unprocessed_bytes=" << backend->buffer()->unprocessed_bytes();
        }
        return true;
      } else {
        LOG_WARN << "ParseReply ERROR, data=(" << std::string(entry, p - entry)
                 << ") backend=" << backend;
        return false;
      }
    }
  }
  // all received data is parsed, no more no less
  return true;
}

}

