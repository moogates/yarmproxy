#include "command.h"

#include <vector>
#include <functional>

#include "base/logging.h"

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
#include "redis_set_command.h"
#include "redis_mset_command.h"
#include "redis_get_command.h"
#include "redis_mget_command.h"

namespace yarmproxy {

std::atomic_int cmd_count;
//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
Command::Command(std::shared_ptr<ClientConnection> client)
    : client_conn_(client)
    , is_transfering_reply_(false) {
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
        return 0;
      }
      command->reset(new RedisGetCommand(client, ba));
      return ba.total_size();
    } else if (ba[0].equals("mget", sizeof("mget") - 1)) {
      if (!ba.completed()) { // TODO : allow incomplete mget bulk_array
        return 0;
      }
      command->reset(new RedisMgetCommand(client, ba));
      return ba.total_size();
    } else if (ba[0].equals("set", sizeof("set") - 1)) {
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        return 0;
      }
      command->reset(new RedisSetCommand(client, ba));
      return ba.parsed_size();
    } else if (ba[0].equals("mset", sizeof("mset") - 1)) {
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        LOG_DEBUG << "CreateCommand RedisMsetCommand need more data, present_bulks=" << ba.present_bulks()
                  << "ba[1].completed=" << ba[1].completed();
        return 0;
      }
      command->reset(new RedisMsetCommand(client, ba));
      if (ba.present_bulks() % 2 == 0) {
        return ba.parsed_size() - ba.back().total_size();    // TODO : 测试有k没v的情况, 是否会解析错误?
      } else {
        return ba.parsed_size();
      }
    }

    LOG_DEBUG << "CreateCommand unknown redis command=" << ba[0].to_string();
    return -1;
  }

  {
    // TODO : memcached binary
  }

  const char * p = static_cast<const char *>(memchr(buf, '\n', size));
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand need more data";
    return 0;
  }

  size_t cmd_line_bytes = p - buf + 1; // 请求 命令行 长度
  if (strncmp(buf, "get ", 4) == 0) {
    command->reset(new ParallelGetCommand(client, buf, cmd_line_bytes));
    return cmd_line_bytes;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0
             || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    size_t body_bytes;
    command->reset(new SetCommand(client, buf, cmd_line_bytes, &body_bytes));
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

  if (backend->finished()) {
    RotateReplyingBackend(backend->recyclable());
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

