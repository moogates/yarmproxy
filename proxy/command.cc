#include "command.h"

#include <vector>
#include <functional>

#include "logging.h"
#include "error_code.h"
#include "worker_pool.h"
#include "client_conn.h"
#include "backend_locator.h"
#include "backend_conn.h"
#include "read_buffer.h"

#include "single_get_command.h"
#include "parallel_get_command.h"
#include "write_command.h"

namespace yarmproxy {

// 指向行尾的'\n'字符
const char * GetLineEnd(const char * buf, size_t len) {
  const char * p = buf + 1; // 首字符肯定不是'\n'
  for(;;) {
    p = (const char *)memchr(p, '\n', len - (p - buf));
    if (p == nullptr) {
      break;
    }
    if(*(p - 1) == '\r') {
      break;
    }
    ++ p; // p 指向 '\n' 的下一个字符
  }

  return p;
}

bool GroupKeysByEndpoint(const char* cmd_line, size_t cmd_line_size, std::map<ip::tcp::endpoint, std::string>* endpoint_key_map) {
  LOG_DEBUG << "GroupKeysByEndpoint begin, cmd=[" << std::string(cmd_line, cmd_line_size - 2) << "]";
  for(const char* p = cmd_line + 4/*strlen("get ")*/; p < cmd_line + cmd_line_size - 2/*strlen("\r\n")*/; ++p) {
    const char* q = p;
    while(*q != ' ' && *q != '\r') {
      ++q;
    }
    ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(p, q - p);

    auto it = endpoint_key_map->find(ep);
    if (it == endpoint_key_map->end()) {
      it = endpoint_key_map->insert(std::make_pair(ep, std::string("get"))).first;
    }

    it->second.append(p - 1, 1 + q - p);
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first << " key=[" << std::string(p-1, 1+q-p) << "]"
              << " current=" << it->second;

    p = q;
  }
  for(auto& it : *endpoint_key_map) {
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it.first << " keys=" << it.second;
    it.second.append("\r\n");
  }
  LOG_DEBUG << "GroupKeysByEndpoint end, endpoint_key_map.size=" << endpoint_key_map->size();
  return true;
}

int ParseWriteCommandLine(const char* cmd_line, size_t cmd_len, std::string* key, size_t* bytes) {
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
  LOG_DEBUG << "ParseWriteCommandLine cmd=" << std::string(cmd_line, cmd_len - 2)
            << " key=[" << *key << "]" << " body_bytes=" << *bytes;

  return 0;
}


std::atomic_int cmd_count;

//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
Command::Command(std::shared_ptr<ClientConnection> client, const std::string& original_header)
    : is_transfering_reply_(false)
    , replying_backend_(nullptr)
    , completed_backends_(0)
    , unreachable_backends_(0)
    , client_conn_(client)
    , original_header_(original_header)
    , loaded_(false) {
  LOG_DEBUG << "Command ctor " << ++cmd_count;
};

Command::~Command() {
  LOG_DEBUG << "Command dtor " << --cmd_count;
}

WorkerContext& Command::context() {
  return client_conn_->context();
}

// 0 : ok, 数据不够解析
// >0 : ok, 解析成功，返回已解析的字节数
// <0 : error, 未知命令
int Command::CreateCommand(std::shared_ptr<ClientConnection> client, const char* buf, size_t size,
          std::shared_ptr<Command>* command) {
  const char * p = GetLineEnd(buf, size);
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand no complete cmd line found";
    return 0;
  }

  size_t cmd_line_bytes = p - buf + 1; // 请求 命令行 长度

  if (strncmp(buf, "get ", 4) == 0) {
#define SINGLE_GET_ONLY 0
#if SINGLE_GET_ONLY
    auto ep = BackendLoactor::Instance().GetEndpointByKey("1");
    std::shared_ptr<Command> cmd(new SingleGetCommand(ep, client, buf, cmd_line_bytes));
    // command->push_back(cmd);
    *command = cmd;
    LOG_DEBUG << "SINGLE_GET_ONLY ep=" << ep << " keys=" << std::string(buf, cmd_line_bytes - 2);
#else
    std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
    GroupKeysByEndpoint(buf, cmd_line_bytes, &endpoint_key_map);
    if (endpoint_key_map.size() == 1) {
      auto it = endpoint_key_map.begin();
      std::shared_ptr<Command> cmd(new SingleGetCommand(it->first, client, it->second.c_str(), it->second.size()));
      *command = cmd;
    } else {
      std::shared_ptr<Command> cmd(new ParallelGetCommand(client, std::string(buf, cmd_line_bytes), std::move(endpoint_key_map)));
      *command = cmd;
    }
#endif
    return cmd_line_bytes;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0 || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    std::string key;
    size_t body_bytes;
    ParseWriteCommandLine(buf, cmd_line_bytes, &key, &body_bytes);

    //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
    std::shared_ptr<Command> cmd(new WriteCommand(BackendLoactor::Instance().GetEndpointByKey(key),
              client, buf, cmd_line_bytes, body_bytes));
    *command = cmd;
    return cmd_line_bytes + body_bytes;
  } else {
    LOG_WARN << "CreateCommand unknown command(" << std::string(buf, cmd_line_bytes - 2)
             << ") len=" << cmd_line_bytes << " client_conn=" << client.get();
    return -1;
  }
}

void Command::OnUpstreamReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  HookOnUpstreamReplyReceived(backend);
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_DEBUG << "Command::OnUpstreamReplyReceived read error, backend=" << backend.get();
    client_conn_->Abort(); // TODO : error
    return;
  }

  LOG_DEBUG << "OnUpstreamReplyReceived, backend=" << backend.get();
  bool valid = ParseReply(backend);
  if (!valid) {
    LOG_WARN << __func__ << " parsing error! valid=false";
    // TODO : error handling
    client_conn_->Abort();
    return;
  }

  if (backend->reply_complete() && backend->buffer()->unprocessed_bytes() == 0) {
    LOG_DEBUG << __func__ << " no new data to process, backend=" << backend.get()
                         << " replying_backend_=" << replying_backend_;
    // 新收的新数据，可能不需要转发，而且不止一遍！例如收到的刚好是"END\r\n"
    if (backend == replying_backend_) { // this check is necessary
      ++completed_backends_;
      RotateReplyingBackend();
    } else {
      PushWaitingReplyQueue(backend);
    }
    return;
  }

  // 判断是否最靠前的command, 是才可以转发
  if (client_conn_->IsFirstCommand(shared_from_this()) && TryActivateReplyingBackend(backend)) {
    TryForwardReply(backend);
  } else {
    PushWaitingReplyQueue(backend);
  }

  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}

// return : is the backend successfully activated
bool Command::TryActivateReplyingBackend(std::shared_ptr<BackendConn> backend) {
  if (replying_backend_ == nullptr) {
    replying_backend_ = backend;
    LOG_DEBUG << "TryActivateReplyingBackend ok, backend=" << backend << " replying_backend_=" << replying_backend_;
    return true;
  }
  return backend == replying_backend_;
}

void Command::OnForwardReplyFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_DEBUG << "Command::OnForwardReplyFinished error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  is_transfering_reply_ = false;
  backend->buffer()->dec_recycle_lock();

  if (backend->reply_complete() && backend->buffer()->unprocessed_bytes() == 0) {
    ++completed_backends_;
    RotateReplyingBackend();
    LOG_DEBUG << "WriteCommand::OnForwardReplyFinished backend no more reply, and all data forwarded to client,"
              << " backend=" << backend;
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardReplyFinished has more reply to forward, ready_to_transfer_bytes=" << backend->buffer()->unprocessed_bytes()
              << " reply_complete=" << backend->reply_complete()
              << " unprocessed_bytes=" << backend->buffer()->unprocessed_bytes()
              << " parsed_unreceived_bytes=" << backend->buffer()->parsed_unreceived_bytes()
              << " backend=" << backend;
    backend->TryReadMoreReply(); // 这里必须继续try
    TryForwardReply(backend); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

void Command::RotateReplyingBackend() {
  client_conn_->RotateReplyingCommand();
}

void Command::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_WARN << "Command::OnBackendConnectError endpoint=" << backend->remote_endpoint()
           << " backend=" << backend;
  if (client_conn_->IsFirstCommand(shared_from_this()) && TryActivateReplyingBackend(backend)) {
    static const char BACKEND_ERROR[] = "BACKEND_CONNECTION_REFUSED\r\n"; // TODO : 统一放置错误码, refining error message protocol
    backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
    backend->set_reply_complete(); // TODO : reply complete can't be the only standard to recycle
        backend->set_no_recycle();

    TryForwardReply(backend);
  } else {
    static const char BACKEND_ERROR[] = "wait_BACKEND_CONNECTION_REFUSED\r\n"; // TODO : 统一放置错误码
    backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
    backend->set_reply_complete();
        backend->set_no_recycle();

    PushWaitingReplyQueue(backend);
  }
}

void Command::TryForwardReply(std::shared_ptr<BackendConn> backend) {
  size_t unprocessed = backend->buffer()->unprocessed_bytes();
  if (!is_transfering_reply_ && unprocessed > 0) {
    is_transfering_reply_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
    backend->buffer()->inc_recycle_lock();
    client_conn_->ForwardReply(backend->buffer()->unprocessed_data(), unprocessed,
                                  WeakBind(&Command::OnForwardReplyFinished, backend));
    backend->buffer()->update_processed_bytes(unprocessed);
    LOG_DEBUG << __func__ << " to_process_bytes=" << unprocessed
              << " new_unprocessed=" << backend->buffer()->unprocessed_bytes()
              << " client=" << this << " backend=" << backend;
  } else {
    LOG_DEBUG << __func__ << " do nothing, unprocessed_bytes=" << unprocessed
              << " is_transfering_reply=" << is_transfering_reply_
              << " client=" << this << " backend=" << backend;
  }
}

}

