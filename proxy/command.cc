#include "command.h"

#include <vector>
#include <functional>

#include "logging.h"
#include "error_code.h"
#include "worker_pool.h"
#include "client_conn.h"
#include "backend_locator.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "read_buffer.h"

#include "get_command.h"
#include "set_command.h"

#include "redis_protocol.h"
#include "redis_get_command.h"
#include "redis_gets_command.h"

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

bool RedisGroupKeysByEndpoint(const char* cmd_line, size_t cmd_line_size,
        std::map<ip::tcp::endpoint, std::string>* endpoint_key_map) {
  auto ep = ip::tcp::endpoint(ip::address::from_string("127.0.0.1"), 6379);
  LOG_DEBUG << "RedisGroupKeysByEndpoint begin, cmd=[" << std::string(cmd_line, cmd_line_size - 2) << "]";
  endpoint_key_map->insert(std::make_pair(ep, std::string(cmd_line, cmd_line_size)));
  return true;
}

bool GroupKeysByEndpoint(const char* cmd_line, size_t cmd_line_size,
        std::map<ip::tcp::endpoint, std::string>* endpoint_key_map) {
  LOG_DEBUG << "GroupKeysByEndpoint begin, cmd=[" << std::string(cmd_line, cmd_line_size - 2) << "]";
  for(const char* p = cmd_line + 4/*strlen("get ")*/
      ; p < cmd_line + cmd_line_size - 2/*strlen("\r\n")*/; ++p) {
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
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first
              << " key=[" << std::string(p-1, 1+q-p) << "]"
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

int ParseSetCommandLine(const char* cmd_line, size_t cmd_len, std::string* key, size_t* bytes) {
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
  LOG_DEBUG << "ParseSetCommandLine cmd=" << std::string(cmd_line, cmd_len - 2)
            << " key=[" << *key << "]" << " body_bytes=" << *bytes;

  return 0;
}


std::atomic_int cmd_count;

//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
Command::Command(std::shared_ptr<ClientConnection> client, const std::string& original_header)
    : client_conn_(client)
    , is_transfering_reply_(false)
    , original_header_(original_header) {
  LOG_DEBUG << "Command ctor " << ++cmd_count;
};

Command::~Command() {
  LOG_DEBUG << "Command dtor " << --cmd_count;
}

BackendConnPool* Command::backend_pool() {
  return client_conn_->context().backend_conn_pool();
}

// 0 : ok, 数据不够解析
// >0 : ok, 解析成功，返回已解析的字节数
// <0 : error, 未知命令
int Command::CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* command) {
  if (strncmp(buf, "*", 1) == 0) {
    redis::BulkArray ba(buf, size);
    if (ba.total_bulks() == 0) {
      LOG_DEBUG << "CreateCommand redis command empty bulk array";
      return -1;
    }
    if (ba.present_bulks() == 0) {
      LOG_DEBUG << "CreateCommand redis command bulk array need more data";
      return 0;
    }
    if (ba[0].equals("get", sizeof("get") - 1)) {
      if (!ba.completed()) {
        LOG_DEBUG << "CreateCommand RedisGetCommand need more data";
        return 0;
      }
      size_t cmd_line_bytes = ba.total_size();
      std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
      RedisGroupKeysByEndpoint(buf, cmd_line_bytes, &endpoint_key_map);
      command->reset(new RedisGetCommand(client, std::string(buf, cmd_line_bytes), ba, std::move(endpoint_key_map)));
      LOG_DEBUG << "CreateCommand ok, redis_command=" << ba[0].to_string();
      return cmd_line_bytes;
    } else if (ba[0].equals("mget", sizeof("mget") - 1)) {
      if (!ba.completed()) {
        LOG_DEBUG << "CreateCommand RedisGetsCommand need more data";
        return 0;
      }
      size_t cmd_line_bytes = ba.total_size();
      std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
      RedisGroupKeysByEndpoint(buf, cmd_line_bytes, &endpoint_key_map);
      command->reset(new RedisGetsCommand(client, std::string(buf, cmd_line_bytes), ba, std::move(endpoint_key_map)));
      LOG_DEBUG << "CreateCommand ok, redis_command=" << ba[0].to_string();
      return cmd_line_bytes;
    } 

    LOG_DEBUG << "CreateCommand unknown redis command=" << ba[0].to_string();
    return -1;
  }

  const char * p = GetLineEnd(buf, size);
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand no complete cmd line found";
    return 0;
  }

  size_t cmd_line_bytes = p - buf + 1; // 请求 命令行 长度
  if (strncmp(buf, "get ", 4) == 0) {
    std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
    GroupKeysByEndpoint(buf, cmd_line_bytes, &endpoint_key_map);
    command->reset(new ParallelGetCommand(client, std::string(buf, cmd_line_bytes), std::move(endpoint_key_map)));
    return cmd_line_bytes;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0
             || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    std::string key;
    size_t body_bytes;
    ParseSetCommandLine(buf, cmd_line_bytes, &key, &body_bytes);

    //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
    std::shared_ptr<Command> cmd(new SetCommand(BackendLoactor::Instance().GetEndpointByKey(key),
              client, buf, cmd_line_bytes, body_bytes));
    *command = cmd;
    return cmd_line_bytes + body_bytes;
  } else {
    LOG_WARN << "CreateCommand unknown command(" << std::string(buf, cmd_line_bytes - 2)
             << ") len=" << cmd_line_bytes << " client_conn=" << client;
    return -1;
  }
}

std::shared_ptr<BackendConn> Command::AllocateBackend(const ip::tcp::endpoint& ep) {
  auto backend = backend_pool()->Allocate(ep);
  backend->SetReadWriteCallback(WeakBind(&Command::OnWriteQueryFinished, backend),
                             WeakBind(&Command::OnBackendReplyReceived, backend));
  LOG_DEBUG << "SetCommand::WriteQuery allocated backend=" << backend;
  return backend;
}

void Command::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
    //LOG_WARN << "OnWriteQueryFinished conn_refused, endpoint=" << backend->remote_endpoint()
    //         << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_WARN << "OnWriteQueryFinished error";
    }
    return;
  }
  if (query_data_zero_copy()) {
    client_conn_->buffer()->dec_recycle_lock();
    // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
    if (client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
      client_conn_->TryReadMoreQuery();
      return;
    }
  }
  backend->ReadReply();
}

void Command::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_WARN << "Command::OnWriteReplyFinished error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  LOG_DEBUG << "Command::OnWriteReplyFinished ok, backend=" << backend;
  is_transfering_reply_ = false;
  backend->buffer()->dec_recycle_lock();

  if (backend->Completed()) {
    RotateReplyingBackend(true);
  } else {
    backend->TryReadMoreReply(); // 这里必须继续try
    TryWriteReply(backend); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

void Command::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_DEBUG << "Command::OnBackendConnectError endpoint=" << backend->remote_endpoint()
           << " backend=" << backend;
  static const char BACKEND_ERROR[] = "BACKEND_CONNECT_ERROR\r\n"; // TODO :refining error message
  backend->SetReplyData(BACKEND_ERROR, sizeof(BACKEND_ERROR) - 1);
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
}

void Command::TryWriteReply(std::shared_ptr<BackendConn> backend) {
  size_t unprocessed = backend->buffer()->unprocessed_bytes();
  if (!is_transfering_reply_ && unprocessed > 0) {
    is_transfering_reply_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
    backend->buffer()->inc_recycle_lock();
    client_conn_->WriteReply(backend->buffer()->unprocessed_data(), unprocessed,
                                  WeakBind(&Command::OnWriteReplyFinished, backend));

    LOG_DEBUG << "Command::TryWriteReply backend=" << backend
              << " data=(" << std::string(backend->buffer()->unprocessed_data(), unprocessed) << ")";
    backend->buffer()->update_processed_bytes(unprocessed);
  }
}

}

