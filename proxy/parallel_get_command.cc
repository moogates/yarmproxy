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
  for(auto it : endpoint_query_map) {
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
    // context_.backend_conn_pool_->Release(query->backend_conn_);
    context_.backend_conn_pool_->Release(query->backend_conn_);
    }
  }
  LOG_DEBUG << "ParallelGetCommand dtor " << --parallel_get_cmd_count;
}

void ParallelGetCommand::ForwardRequest(const char *, size_t) {
  DoForwardRequest(nullptr, 0);
}

void ParallelGetCommand::PushReadyQueue(BackendConn* backend) {
  LOG_DEBUG << "ParallelGetCommand PushReadyQueue, backend=" << backend;
  ready_queue_.push(backend);
}

bool ParallelGetCommand::ParseUpstreamResponse(BackendConn* backend) {
  bool valid = true;
  while(backend->read_buffer_.unparsed_bytes() > 0) {
    const char * entry = backend->read_buffer_.unparsed_data();
    const char * p = GetLineEnd(entry, backend->read_buffer_.unparsed_bytes());
    if (p == nullptr) {
      // TODO : no enough data for parsing, please read more
      LOG_DEBUG << "ParseUpstreamResponse no enough data for parsing, please read more"
                << " bytes=" << backend->read_buffer_.unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;

      backend->read_buffer_.update_parsed_bytes(entry_bytes);
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        backend->read_buffer_.update_parsed_bytes(sizeof("END\r\n") - 1);
        if (backend->read_buffer_.unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
          valid = false;
          LOG_WARN << "ParseUpstreamResponse END not really end!";
        } else {
          LOG_INFO << "ParseUpstreamResponse END is really end!";
        }
        break;
      } else {
        LOG_WARN << "ParseUpstreamResponse BAD DATA";
        // TODO : ERROR
        valid = false;
        break;
      }
    }
  }
  return valid;
}

void ParallelGetCommand::DoForwardRequest(const char *, size_t) {
  for(auto& query : query_set_) {
    BackendConn* backend = query->backend_conn_;
    if (backend == nullptr) {
      // LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn, worker_id=" << WorkerPool::CurrentWorkerId();
      LOG_DEBUG << "ParallelGetCommand sub query(" << query->query_line_.substr(0, query->query_line_.size() - 2) << ") create backend conn";
      backend = context_.backend_conn_pool_->Allocate(query->backend_addr_);
      backend->SetReadWriteCallback(WeakBind(&MemcCommand::OnForwardMoreRequest),
                                 WeakBind2(&MemcCommand::OnUpstreamResponseReceived, backend));
      query->backend_conn_ = backend;
    }
    backend->ForwardRequest(query->query_line_.data(), query->query_line_.size(), false);
  }
}

}


