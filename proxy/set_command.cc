#include "set_command.h"

#include "logging.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int write_cmd_count;
SetCommand::SetCommand(const ip::tcp::endpoint & ep,
        std::shared_ptr<ClientConnection> client, const char * buf, size_t cmd_len, size_t body_bytes)
    : Command(client, std::string(buf, cmd_len))
    , query_header_bytes_(cmd_len)
    , query_written_bytes_(0)
    , query_body_bytes_(body_bytes)
    , query_writing_bytes_(0)
    , backend_endpoint_(ep)
    , backend_conn_(nullptr)
{
  LOG_DEBUG << "SetCommand ctor " << ++write_cmd_count;
}

SetCommand::~SetCommand() {
  if (backend_conn_) {
    context().backend_conn_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "SetCommand dtor " << --write_cmd_count;
}

size_t SetCommand::query_body_upcoming_bytes() const {
  return query_header_bytes_ + query_body_bytes_ - query_writing_bytes_ - query_written_bytes_;
}

void SetCommand::WriteQuery(const char * data, size_t bytes) {
  if (!backend_conn_) {
    backend_conn_ = context().backend_conn_pool()->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&Command::OnWriteQueryFinished, backend_conn_),
                               WeakBind(&Command::OnBackendReplyReceived, backend_conn_));
    LOG_DEBUG << "SetCommand::WriteQuery allocated backend=" << backend_conn_.get();
  }

  DoWriteQuery(data, bytes);
}

void SetCommand::DoWriteQuery(const char * query_data, size_t client_buf_received_bytes) {
  client_conn_->buffer()->inc_recycle_lock();
  query_writing_bytes_ = std::min(client_buf_received_bytes, query_header_bytes_ + query_body_bytes_); // FIXME
  backend_conn_->WriteQuery(query_data, query_writing_bytes_, query_body_upcoming_bytes() != 0);
}

void SetCommand::OnWriteReplyEnabled() {
  LOG_DEBUG << "OnWriteReplyEnabled TryWriteReply backend_conn_=" << backend_conn_;
  // TODO : if connection refused, should report error & rotate
  TryWriteReply(backend_conn_);
}

void SetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(backend == backend_conn_);
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "SetCommand OnWriteQueryFinished connection_refused, endpoint=" << backend->remote_endpoint()
               << " backend=" << backend.get();
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_WARN << "SetCommand OnWriteQueryFinished error";
    }
    return;
  }
  assert(backend == backend_conn_);
  LOG_DEBUG << "SetCommand OnWriteQueryFinished ok, query_writing_bytes_=" << query_writing_bytes_;
  client_conn_->buffer()->dec_recycle_lock();

  query_written_bytes_ += query_writing_bytes_;
  query_writing_bytes_ = 0;

  if (query_written_bytes_ < query_header_bytes_ + query_body_bytes_) {
    LOG_DEBUG << "SetCommand::OnWriteQueryFinished 转发了当前所有可转发数据, 但还要转发更多来自client的数据.";
    client_conn_->TryReadMoreQuery();
  } else {
    LOG_DEBUG << "SetCommand::OnWriteQueryFinished 转发了当前命令的所有数据, 等待 backend 的响应.";
    backend_conn_->ReadReply();
  }
}

bool SetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(backend_conn_ == backend);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  const char * p = GetLineEnd(entry, backend_conn_->buffer()->unparsed_bytes());
  if (p == nullptr) {
    LOG_DEBUG << "SetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
              << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "SetCommand ParseReply resp.size=" << p - entry + 1
            // << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_complete, backend=" << backend.get();
  backend->set_reply_complete();
  return true;
}

}

