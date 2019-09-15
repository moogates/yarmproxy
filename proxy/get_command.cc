#include "get_command.h"

#include "base/logging.h"

#include "error_code.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int parallel_get_cmd_count;

void ParallelGetCommand::GroupKeysByEndpoint(const char* cmd_data,
        size_t cmd_size,
        std::map<ip::tcp::endpoint, std::string>* endpoint_key_map) {
  LOG_DEBUG << "GroupKeysByEndpoint begin, cmd=["
            << std::string(cmd_data, cmd_size - 2) << "]";
  for(const char* p = cmd_data + 4/*strlen("get ")*/
      ; p < cmd_data + cmd_size - 2/*strlen("\r\n")*/; ++p) {
    const char* q = p;
    while(*q != ' ' && *q != '\r') {
      ++q;
    }
    ip::tcp::endpoint ep = BackendLoactor::Instance().Locate(p, q - p);

    auto it = endpoint_key_map->find(ep);
    if (it == endpoint_key_map->end()) {
      it = endpoint_key_map->insert(std::make_pair(ep, std::string("get"))).first;
    }

    it->second.append(p - 1, 1 + q - p);
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first
              << " key=[" << std::string(p-1, 1+q-p) << "]"
              << " current=" << it->second;

    p = q;
  }
  for(auto& it : *endpoint_key_map) {
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it.first << " keys=" << it.second;
    it.second.append("\r\n");
  }
  LOG_DEBUG << "GroupKeysByEndpoint end, endpoint_key_map.size="
            << endpoint_key_map->size();
}


ParallelGetCommand::ParallelGetCommand(std::shared_ptr<ClientConnection> client,
                     const char* cmd_data, size_t cmd_size)
    : Command(client)
    , completed_backends_(0)
    , unreachable_backends_(0)
{
  std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
  GroupKeysByEndpoint(cmd_data, cmd_size, &endpoint_key_map);
  for(auto& it : endpoint_key_map) {
    LOG_DEBUG << "ParallelGetCommand ctor, create query ep=" << it.first
              << " query=" << it.second;
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
}

ParallelGetCommand::ParallelGetCommand(std::shared_ptr<ClientConnection> client,
    std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map)
    : Command(client)
    , completed_backends_(0)
    , unreachable_backends_(0)
{
  for(auto& it : endpoint_query_map) {
    LOG_DEBUG << "ParallelGetCommand ctor, create query ep=" << it.first
              << " query=" << it.second;
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
  LOG_DEBUG << "ParallelGetCommand ctor, query_set_.size=" << query_set_.size()
            << " count=" << ++parallel_get_cmd_count;
}

ParallelGetCommand::~ParallelGetCommand() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      LOG_DEBUG << "ParallelGetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "ParallelGetCommand dtor Release backend";
      backend_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "ParallelGetCommand dtor, cmd=" << this
            << " count=" << --parallel_get_cmd_count;
}

void ParallelGetCommand::WriteQuery() {
  for(auto& query : query_set_) {
    if (!query->backend_conn_) {
      query->backend_conn_ = AllocateBackend(query->backend_endpoint_);
    }
    LOG_DEBUG << "ParallelGetCommand WriteQuery cmd=" << this
              << " backend=" << query->backend_conn_ << ", query=("
              << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    query->backend_conn_->WriteQuery(query->query_line_.data(), query->query_line_.size());
  }
}

void ParallelGetCommand::TryMarkLastBackend(std::shared_ptr<BackendConn> backend) {
  if (received_reply_backends_.insert(backend).second) {
    if (received_reply_backends_.size() == query_set_.size()) {
      last_backend_ = backend;
    }
  }
}

// return : is the backend successfully activated
bool ParallelGetCommand::TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend) {
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    LOG_DEBUG << "TryActivateReplyingBackend ok, backend=" << backend
              << " replying_backend_=" << replying_backend_;
    return true;
  }
  return backend == replying_backend_;
}

void ParallelGetCommand::BackendReadyToReply(std::shared_ptr<BackendConn> backend) {
  if (client_conn_->IsFirstCommand(shared_from_this())
      && TryActivateReplyingBackend(backend)) {
    if (backend->finished()) { // newly received data might needn't forwarding, eg. some "END\r\n"
      RotateReplyingBackend(backend->recyclable());
    } else {
      TryWriteReply(backend);
    }
  } else {
    // push backend into waiting_reply_queue_ if doesn't exist
    if (std::find(waiting_reply_queue_.begin(), waiting_reply_queue_.end(),
                  backend) == waiting_reply_queue_.end()) {
      waiting_reply_queue_.push_back(backend);
    }
  }
}

void ParallelGetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
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


void ParallelGetCommand::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_DEBUG << "ParallelGetCommand::OnBackendConnectError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  ++unreachable_backends_;
  TryMarkLastBackend(backend);

  if (backend == last_backend_) {
    static const char END_RN[] = "END\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(END_RN, sizeof(END_RN) - 1);
    LOG_WARN << "ParallelGetCommand::OnBackendConnectError last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  } else {
    LOG_WARN << "ParallelGetCommand::OnBackendConnectError not last, endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
  }
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  BackendReadyToReply(backend);
}

void ParallelGetCommand::StartWriteReply() {
  NextBackendStartReply();
}

void ParallelGetCommand::NextBackendStartReply() {
  LOG_DEBUG << "NextBackendStartReply cmd=" << this
            << " last replying_backend_=" << replying_backend_;
  if (waiting_reply_queue_.size() > 0) {
    auto next_backend = waiting_reply_queue_.front();
    waiting_reply_queue_.pop_front();

    LOG_DEBUG << "NextBackendStartReply activate ready backend,"
              << " backend=" << next_backend;
    if (next_backend->finished()) {
      LOG_WARN << "NextBackendStartReply backend=" << next_backend << " empty reply";
      RotateReplyingBackend(next_backend->recyclable());
    } else {
      TryWriteReply(next_backend);
      replying_backend_ = next_backend;
    }
  } else {
    LOG_DEBUG << "ParallelGetCommand::OnWriteReplyEnabled no ready backend to activate,"
              << " last_replying_backend_=" << replying_backend_;
    replying_backend_ = nullptr;
  }
}

bool ParallelGetCommand::HasUnfinishedBanckends() const {
  return unreachable_backends_ + completed_backends_ < query_set_.size();
}

void ParallelGetCommand::RotateReplyingBackend(bool success) {
  if (success) {
    ++completed_backends_;
  }
  if (HasUnfinishedBanckends()) {
    LOG_DEBUG << "ParallelGetCommand::Rotate to next backend";
    NextBackendStartReply();
  } else {
    LOG_DEBUG << "ParallelGetCommand::Rotate to next COMMAND";
    client_conn_->RotateReplyingCommand();
  }
}

size_t ParallelGetCommand::ReplyBodyBytes(const char * data, const char * end) {
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

bool ParallelGetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
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
      size_t body_bytes = ReplyBodyBytes(entry, p);
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
          LOG_WARN << "ParseReply END, is last, backend=" << backend
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

