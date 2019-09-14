#include "set_command.h"

#include "base/logging.h"

#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

namespace yarmproxy {

std::atomic_int write_cmd_count;

SetCommand::SetCommand(std::shared_ptr<ClientConnection> client,
          const char* buf, size_t cmd_len, size_t* body_bytes) 
    : Command(client) {
  std::string key;
  ParseCommandLine(buf, cmd_len, &key, body_bytes);
  backend_endpoint_ = BackendLoactor::Instance().GetEndpointByKey(key);
  LOG_WARN << "SetCommand key=" << key << " body_bytes=" << *body_bytes
            << " ctor " << ++write_cmd_count;
}

int SetCommand::ParseCommandLine(const char* cmd_line, size_t cmd_len, std::string* key, size_t* bytes) {
  //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
  {
    const char *p = cmd_line;
    while(*(p++) != ' ') {
      ;
    }
    const char *q = p;
    while(*(++q) != ' ') {
      ;
    }
    key->assign(p, q - p);
  }

  {
    const char *p = cmd_line + cmd_len - 2;
    while(*(p-1) != ' ') {
      --p;
    }
    *bytes = std::atoi(p) + 2; // 2 is lenght of the ending "\r\n"
  }
  LOG_DEBUG << "ParseCommandLine cmd=" << std::string(cmd_line, cmd_len - 2)
            << " key=[" << *key << "]" << " body_bytes=" << *bytes;

  return 0;
}

SetCommand::~SetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "SetCommand dtor " << --write_cmd_count;
}

void SetCommand::WriteQuery() {
  if (!backend_conn_) {
    backend_conn_ = AllocateBackend(backend_endpoint_);
    LOG_DEBUG << "SetCommand::WriteQuery allocated backend=" << backend_conn_;
  }

  client_conn_->buffer()->inc_recycle_lock();
  backend_conn_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
          client_conn_->buffer()->unprocessed_bytes());
}

void SetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "SetCommand::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  // 判断是否最靠前的command, 是才可以转发
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}

void SetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply backend_conn_=" << backend_conn_;
  // TODO : if connection refused, should report error & rotate
  TryWriteReply(backend_conn_);
}

void SetCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

/*
void SetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(backend == backend_conn_);
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "SetCommand OnWriteQueryFinished connection_refused, endpoint=" << backend->remote_endpoint()
               << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_WARN << "SetCommand OnWriteQueryFinished error";
    }
    return;
  }
  assert(backend == backend_conn_);
  client_conn_->buffer()->dec_recycle_lock();

  // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
  if (client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
    client_conn_->TryReadMoreQuery();
  } else {
    backend_conn_->ReadReply();
  }
}
*/

bool SetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(backend_conn_ == backend);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  const char * p = static_cast<const char *>(memchr(entry, '\n',
                       backend_conn_->buffer()->unparsed_bytes()));
  if (p == nullptr) {
    LOG_DEBUG << "SetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
              << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "SetCommand ParseReply resp.size=" << p - entry + 1
            << " set_reply_recv_complete, backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

