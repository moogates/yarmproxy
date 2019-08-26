#include "parallel_get_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "worker_pool.h"
#include "backend_conn.h"

namespace mcproxy {

std::atomic_int parallel_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);
size_t GetValueBytes(const char * data, const char * end);

ParallelGetCommand::BackendQuery::~BackendQuery() {
}

ParallelGetCommand::ParallelGetCommand(std::shared_ptr<ClientConnection> owner,
                   std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map)
    : MemcCommand(owner)
    , finished_count_(0)
{
  for(auto& it : endpoint_query_map) {
    LOG_DEBUG << "ParallelGetCommand ctor, create query ep=" << it.first << " query=" << it.second;
    query_set_.emplace_back(new BackendQuery(it.first, std::move(it.second)));
  }
  LOG_DEBUG << "ParallelGetCommand ctor " << ++parallel_get_cmd_count;
}

ParallelGetCommand::~ParallelGetCommand() {
  for(auto& query : query_set_) {
    if (query->backend_conn_ == nullptr) {
      LOG_DEBUG << "ParallelGetCommand dtor Release null backend";
    } else {
      LOG_DEBUG << "ParallelGetCommand dtor Release backend";
      context_.backend_conn_pool()->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "ParallelGetCommand dtor " << --parallel_get_cmd_count;
}

void ParallelGetCommand::ForwardQuery(const char *, size_t) {
  DoForwardQuery(nullptr, 0);
}

void ParallelGetCommand::OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) {
  if (error) {
    // TODO : error handling
    LOG_DEBUG << "ParallelGetCommand OnForwardQueryFinished error";
    return;
  }
  LOG_DEBUG << "ParallelGetCommand::OnForwardQueryFinished 转发了当前命令, 等待backend的响应.";
  backend->ReadReply();
}

void ParallelGetCommand::PushReadyQueue(BackendConn* backend) {
  if (ready_set_.insert(backend).second) {
    ready_queue_.push(backend);
    LOG_DEBUG << "ParallelGetCommand PushReadyQueue, backend=" << backend << " ready_queue_.size=" << ready_queue_.size();
  } else {
    LOG_DEBUG << "ParallelGetCommand PushReadyQueue already in queue, backend=" << backend << " ready_queue_.size=" << ready_queue_.size();
  }
}

void ParallelGetCommand::OnForwardReplyEnabled() {
  if (ready_queue_.size() > 0) {
    replying_backend_ = ready_queue_.front();
    ready_queue_.pop();
    LOG_WARN << __func__ << " activate ready backend,"
              << " replying_backend_=" << replying_backend_;
    TryForwardReply(replying_backend_);
  } else {
    LOG_DEBUG << __func__ << " no ready backend to activate,"
              << " replying_backend_=" << replying_backend_;
  }
}

void ParallelGetCommand::RotateFirstBackend() {
  ++finished_count_;
  LOG_DEBUG << "ParallelGetCommand RotateFirstBackend finished_count_=" << finished_count_;
  if (ready_queue_.size() > 0) {
    replying_backend_ = ready_queue_.front();
    ready_queue_.pop();
    LOG_WARN << "ParallelGetCommand RotateFirstBackend, activate ready backend"
              << " replying_backend_=" << replying_backend_;
    TryForwardReply(replying_backend_);
  } else {
    LOG_DEBUG << "ParallelGetCommand RotateFirstBackend, no ready backend, wait";
    replying_backend_ = nullptr;
  }
}

bool ParallelGetCommand::ParseReply(BackendConn* backend) {
  bool valid = true;
  while(backend->buffer()->unparsed_bytes() > 0) {
    const char * entry = backend->buffer()->unparsed_data();
    size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
    const char * p = GetLineEnd(entry, unparsed_bytes);
    if (p == nullptr) {
      // TODO : no enough data for parsing, please read more
      LOG_DEBUG << "ParseReply no enough data for parsing, please read more"
                << " bytes=" << backend->buffer()->unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;
      LOG_DEBUG << __func__ << " VALUE data, backend=" << backend << " recv_body=(" << std::string(entry, std::min(unparsed_bytes, entry_bytes)) << ")";
      backend->buffer()->update_parsed_bytes(entry_bytes);
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        // backend->buffer()->update_parsed_bytes(sizeof("END\r\n") - 1);
        if (backend->buffer()->unparsed_bytes() != (sizeof("END\r\n") - 1)) { // TODO : pipeline的情况呢?
          valid = false;
          LOG_DEBUG << "ParseReply END not really end! backend=" << backend;
        } else {
          LOG_DEBUG << "ParseReply END is really end! set_reply_complete, backend=" << backend;
          backend->set_reply_complete();
          backend->buffer()->cut_received_tail(sizeof("END\r\n") - 1);
        }
        break;
      } else {
        LOG_WARN << "ParseReply BAD DATA";
        // TODO : ERROR
        valid = false;
        break;
      }
    }
  }
  return valid;
}

void ParallelGetCommand::DoForwardQuery(const char *, size_t) {
  for(auto& query : query_set_) {
    BackendConn* backend = query->backend_conn_;
    if (backend == nullptr) {
      // LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn, worker_id=" << WorkerPool::CurrentWorkerId();
      LOG_DEBUG << "ParallelGetCommand sub query(" << query->query_line_.substr(0, query->query_line_.size() - 2) << ") create backend conn";
      backend = context_.backend_conn_pool()->Allocate(query->backend_addr_);
      backend->SetReadWriteCallback(WeakBind(&MemcCommand::OnForwardQueryFinished, backend),
                                 WeakBind(&MemcCommand::OnUpstreamReplyReceived, backend));
      query->backend_conn_ = backend;
    }
    LOG_DEBUG << __func__ << " ForwardQuery, query=(" << query->query_line_.substr(0, query->query_line_.size() - 2) << ")";
    backend->ForwardQuery(query->query_line_.data(), query->query_line_.size(), false);
  }
}

}


