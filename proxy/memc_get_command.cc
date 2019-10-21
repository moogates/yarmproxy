#include "memc_get_command.h"

#include "logging.h"

#include "error_code.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"

namespace yarmproxy {

struct MemcGetCommand::Subquery {
  Subquery(std::shared_ptr<BackendConn> backend, std::string&& query_data)
      : backend_(backend)
      , query_data_(query_data) {
  }
  std::shared_ptr<BackendConn> backend_;
  std::string query_data_;
};

MemcGetCommand::MemcGetCommand(std::shared_ptr<ClientConnection> client,
                     const char* cmd_data, size_t cmd_size)
    : Command(client, ProtocolType::MEMCACHED)
{
  ParseQuery(cmd_data, cmd_size);
}

MemcGetCommand::~MemcGetCommand() {
  for(auto& query : subqueries_) {
    if (query->backend_) {
      backend_pool()->Release(query->backend_);
    }
  }
}

void MemcGetCommand::ParseQuery(const char* cmd_data, size_t cmd_size) {
  std::map<Endpoint, std::string> ep_keys;
  for(const char* p = cmd_data + (sizeof("get ") - 1);
      p < cmd_data + cmd_size - (sizeof("\r\n") - 1); ++p) {
    const char* q = p;
    while(*q != ' ' && *q != '\r') {
      ++q;
    }
    auto ep = backend_locator()->Locate(p, q - p, ProtocolType::MEMCACHED);
    auto it = ep_keys.find(ep);
    if (it == ep_keys.end()) {
      it = ep_keys.emplace(ep, "get").first;
    }

    it->second.append(p - 1, 1 + q - p);
    p = q;
  }
  for(auto& it : ep_keys) {
    it.second.append("\r\n");
    auto backend = backend_pool()->Allocate(it.first);
    subqueries_.emplace_back(new Subquery(backend, std::move(it.second)));
  }
}

bool MemcGetCommand::StartWriteQuery() {
  for(auto& query : subqueries_) {
    auto backend = query->backend_;
    assert(backend);
    backend->SetReadWriteCallback(
        WeakBind(&Command::OnWriteQueryFinished, backend),
        WeakBind(&Command::OnBackendReplyReceived, backend));
    backend->WriteQuery(query->query_data_.data(), query->query_data_.size());
  }
  return false;
}

void MemcGetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == subqueries_.size()) {
      last_backend_ = backend;
    }
  }
}

bool MemcGetCommand::TryActivateReplyingBackend(
        std::shared_ptr<BackendConn> backend) {
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    return true;
  }
  return backend == replying_backend_;
}

void MemcGetCommand::BackendReadyToReply(
    std::shared_ptr<BackendConn> backend) {
  if (client_conn_->IsFirstCommand(shared_from_this())
      && TryActivateReplyingBackend(backend)) {
    if (backend->finished()) {
      // newly received data might needn't forwarding, eg. some "END\r\n"
      RotateReplyingBackend();
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

void MemcGetCommand::OnBackendReplyReceived(
        std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  TryMarkLastBackend(backend);
  if (ec == ErrorCode::E_SUCCESS && !ParseReply(backend)) {
    ec = ErrorCode::E_PROTOCOL;
  }
  if (ec != ErrorCode::E_SUCCESS) {
    if (!BackendErrorRecoverable(backend, ec)) {
      client_conn_->Abort();
    } else {
      OnBackendRecoverableError(backend, ec);
    }
    return;
  }


  BackendReadyToReply(backend);
  backend->TryReadMoreReply();
}

bool MemcGetCommand::BackendErrorRecoverable(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  return !backend->has_read_some_reply();
}

void MemcGetCommand::OnBackendRecoverableError(
    std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  LOG_DEBUG << "MemcGetCommand::OnBackendRecoverableError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  TryMarkLastBackend(backend);

  if (backend == last_backend_) {
    // last backend, send reply
    if (subqueries_.size() > 1) {
      static const char END_RN[] = "END\r\n";
      backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
    } else {
      const auto& err_reply(MemcErrorReply(ec));
      backend->SetReplyData(err_reply.data(), err_reply.size());
    }
  } else {
    // not last backend, do nothing
  }
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  BackendReadyToReply(backend);
}

void MemcGetCommand::StartWriteReply() {
  NextBackendStartReply();
}

void MemcGetCommand::NextBackendStartReply() {
  LOG_DEBUG << "NextBackendStartReply cmd=" << this
            << " last replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    auto next_backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();

    LOG_DEBUG << "NextBackendStartReply activate ready backend,"
              << " subqueries_.size=" << subqueries_.size()
              << " completed_backends_=" << completed_backends_
              << " backend=" << next_backend;
    if (next_backend->finished()) {
      RotateReplyingBackend();
    } else {
      TryWriteReply(next_backend);
      replying_backend_ = next_backend;
    }
  } else {
    replying_backend_ = nullptr;
  }
}

bool MemcGetCommand::HasUnfinishedBanckends() const {
  return completed_backends_ < subqueries_.size();
}

void MemcGetCommand::RotateReplyingBackend() {
  ++completed_backends_;
  if (HasUnfinishedBanckends()) {
    NextBackendStartReply();
  } else {
    client_conn_->RotateReplyingCommand();
  }
}

size_t MemcGetCommand::ParseReplyBodySize(const char * data, const char * end) {
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

bool MemcGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
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
      size_t body_bytes = ParseReplyBodySize(entry, p);
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
        LOG_INFO << "ParseReply ERROR, data=(" << std::string(entry, p - entry)
                 << ") backend=" << backend;
        return false;
      }
    }
  }
  // all received data is parsed, no more no less
  return true;
}

}

