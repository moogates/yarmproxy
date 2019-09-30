#include "command.h"

#include <vector>
#include <functional>

#include "base/logging.h"

#include "error_code.h"
#include "worker_pool.h"
#include "client_conn.h"
// #include "backend_locator.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "read_buffer.h"

#include "error_command.h"

#include "memcached_get_command.h"
#include "memcached_set_command.h"
#include "mc_simple_command.h"

#include "redis_protocol.h"
#include "redis_set_command.h"
#include "redis_mset_command.h"
#include "redis_get_command.h"
#include "redis_mget_command.h"
#include "redis_del_command.h"

namespace yarmproxy {

std::atomic_int x_cmd_count;
//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
Command::Command(std::shared_ptr<ClientConnection> client)
    : client_conn_(client) {
};

Command::~Command() {
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
  const char * p = static_cast<const char *>(memchr(buf, '\n', size));
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand need more data";
    return 0;
  }

  if (strncmp(buf, "*", 1) == 0) {
    redis::BulkArray ba(buf, size);
    if (ba.total_bulks() == 0) {
      LOG_WARN << "CreateCommand data_size=" << size
               << " bad_data=[" << std::string(buf, size) << "]";
      return -1;
    }
    if (ba.present_bulks() == 0 || ba[0].absent_size() > 0) {
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
      // if (ba.present_bulks() < 2 || !ba[1].completed()) {
      if (ba.present_bulks() < 3) {
        return 0;
      }
      command->reset(new RedisMsetCommand(client, ba));
      if (ba.present_bulks() % 2 == 0) {
        return ba.parsed_size() - ba.back().total_size();    // TODO : 测试有k没v的情况, 是否会解析错误?
      } else {
        return ba.parsed_size();
      }
    } else if (ba[0].equals("del", sizeof("del") - 1)) {
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        return 0;
      }
      command->reset(new RedisDelCommand(client, ba));
      if (ba.back().completed()) {
        return ba.parsed_size();
      } else {
        return ba.parsed_size() - ba.back().total_size();
      }
    }

    command->reset(new ErrorCommand(client, std::string("-YarmProxy unknown redis directive:") + ba[0].to_string() + "\r\n"));
    return size;
  }

  {
    // TODO : memcached binary
  }

  // TODO : 支持 memcached noreply 字段, 顺带加上严格的语法检查
  size_t cmd_line_bytes = p - buf + 1; // 请求 命令行 长度
  if (strncmp(buf, "get ", sizeof("get ") - 1) == 0 ||
      strncmp(buf, "gets ", sizeof("gets ") - 1) == 0) {
    // TODO : strict protocol check
    command->reset(new MemcachedGetCommand(client, buf, cmd_line_bytes));
    return cmd_line_bytes;
  } else if (strncmp(buf, "set ", sizeof("set ") - 1) == 0 ||
             strncmp(buf, "add ", sizeof("add ") - 1) == 0 ||
             strncmp(buf, "replace ", sizeof("replace ") - 1) == 0 ||
             strncmp(buf, "append ", sizeof("append ") - 1) == 0 ||
             strncmp(buf, "prepend ", sizeof("prepend ") - 1) == 0 ||
             strncmp(buf, "cas", sizeof("cas ") - 1) == 0) {
    size_t body_bytes;
    command->reset(new MemcachedSetCommand(client, buf, cmd_line_bytes, &body_bytes));
    return cmd_line_bytes + body_bytes;
  } else if (strncmp(buf, "delete ", sizeof("delete ") - 1) == 0 ||
      strncmp(buf, "incr ", sizeof("incr ") - 1) == 0 ||
      strncmp(buf, "decr ", sizeof("decr ") - 1) == 0 ||
      strncmp(buf, "incr ", sizeof("incr ") - 1) == 0 ||
      strncmp(buf, "touch ", sizeof("touch ") - 1) == 0
      ) {
    // TODO : strict protocol check
    command->reset(new MemcachedSimpleCommand(client, buf, cmd_line_bytes));
    return cmd_line_bytes;
  }

  command->reset(new ErrorCommand(client, std::string("YarmProxy Unsupported Request\r\n")));
  LOG_WARN << "ErrorCommand(" << std::string(buf, cmd_line_bytes - 2)
           << ") len=" << cmd_line_bytes << " client_conn=" << client;
  return size;
}

std::shared_ptr<BackendConn> Command::AllocateBackend(const ip::tcp::endpoint& ep) {
  auto backend = backend_pool()->Allocate(ep);
  backend->SetReadWriteCallback(WeakBind(&Command::OnWriteQueryFinished, backend),
                             WeakBind(&Command::OnBackendReplyReceived, backend));
  return backend;
}

void Command::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
      LOG_WARN << "OnWriteQueryFinished connect error, backend=" << backend
               << " ep=" << backend->remote_endpoint();
      OnBackendConnectError(backend);

      // TODO : no duplicate code
      if (query_data_zero_copy()) {
        client_conn_->buffer()->dec_recycle_lock();
        if (!query_recv_complete()) {
          client_conn_->TryReadMoreQuery();
        }
      }
    } else {
      LOG_WARN << "OnWriteQueryFinished error, backend=" << backend
               << " ep=" << backend->remote_endpoint();
      client_conn_->Abort();
    }
    return;
  }

  LOG_DEBUG << "OnWriteQueryFinished ok, backend=" << backend;

  if (query_data_zero_copy()) {
    client_conn_->buffer()->dec_recycle_lock();
    if (!query_recv_complete()) {
      client_conn_->TryReadMoreQuery();
      return;
    }
  }

  if (query_recv_complete()) {
    LOG_DEBUG << "OnWriteQueryFinished ok, begin read reply, backend=" << backend;
    backend->ReadReply();
  } else {
    LOG_DEBUG << "OnWriteQueryFinished ok, need more query, backend=" << backend;
  }
}

void Command::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_WARN << "Command::OnWriteReplyFinished error, backend="
             << backend;
    client_conn_->Abort();
    return;
  }

  is_writing_reply_ = false;
  backend->buffer()->dec_recycle_lock();

  if (backend->finished()) {
    assert(!backend->buffer()->recycle_locked());
    LOG_DEBUG << "OnWriteReplyFinished backend=" << backend << " finished";
    RotateReplyingBackend(backend->recyclable());
  } else {
    LOG_DEBUG << "OnWriteReplyFinished backend=" << backend << " unfinished";
    backend->TryReadMoreReply(); // 这里必须继续try
    TryWriteReply(backend); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

void Command::OnBackendConnectError(std::shared_ptr<BackendConn> backend) {
  LOG_DEBUG << "Command::OnBackendConnectError endpoint="
            << backend->remote_endpoint() << " backend=" << backend;
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
  if (!is_writing_reply_ && unprocessed > 0) {
    is_writing_reply_ = true;

    backend->buffer()->inc_recycle_lock();

    client_conn_->WriteReply(backend->buffer()->unprocessed_data(), unprocessed,
                                  WeakBind(&Command::OnWriteReplyFinished, backend));

    LOG_DEBUG << "Command::TryWriteReply backend=" << backend
              << " unprocessed=" << unprocessed;
    backend->buffer()->update_processed_bytes(unprocessed);
  }
}

}

