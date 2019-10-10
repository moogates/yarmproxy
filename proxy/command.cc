#include "command.h"

#include <vector>
#include <functional>

#include "logging.h"

#include "error_code.h"
#include "backend_locator.h"

#include "worker_pool.h"
#include "client_conn.h"
#include "backend_locator.h"
#include "backend_conn.h"
#include "backend_pool.h"
#include "read_buffer.h"

#include "error_command.h"
#include "stats_command.h"

#include "mc_basic_command.h"
#include "mc_get_command.h"
#include "mc_set_command.h"

#include "redis_protocol.h"
#include "redis_basic_command.h"
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

std::shared_ptr<BackendLocator> Command::backend_locator() {
  return client_conn_->context().backend_locator_;
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
    if (ba[0].equals("get", sizeof("get") - 1) ||
        ba[0].equals("getset", sizeof("getset") - 1) ||
        ba[0].equals("getrange", sizeof("getrange") - 1)) {
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
    } else if (ba[0].equals("set", sizeof("set") - 1) ||
        ba[0].equals("append", sizeof("append") - 1) ||
        ba[0].equals("setrange", sizeof("setrange") - 1) ||
        ba[0].equals("setnx", sizeof("setnx") - 1) ||
        ba[0].equals("psetex", sizeof("psetex") - 1) ||
        ba[0].equals("setex", sizeof("setex") - 1)) {
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
        return ba.parsed_size() - ba.back().total_size();
      } else {
        return ba.parsed_size();
      }
    } else if (ba[0].equals("del", sizeof("del") - 1) ||
               ba[0].iequals("exists", sizeof("exists") - 1) ||
               ba[0].iequals("touch", sizeof("touch") - 1)) {
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        return 0;
      }
      command->reset(new RedisDelCommand(client, ba));
      if (ba.back().completed()) {
        return ba.parsed_size();
      } else {
        return ba.parsed_size() - ba.back().total_size();
      }
    } else if (ba[0].equals("ttl", sizeof("ttl") - 1) ||
        ba[0].equals("incr", sizeof("incr") - 1) ||
        ba[0].equals("incrby", sizeof("incrby") - 1) ||
        ba[0].equals("incrbyfloat", sizeof("incrbyfloat") - 1) ||
        ba[0].equals("decr", sizeof("decr") - 1) ||
        ba[0].equals("decrby", sizeof("decrby") - 1) ||
        ba[0].equals("strlen", sizeof("strlen") - 1) ||
        false) {
      if (!ba.completed()) {
        return 0;
      }
      command->reset(new RedisBasicCommand(client, ba));
      return ba.total_size();
    } else if (ba[0].equals("yarmstats", sizeof("yarmstats") - 1)) {
      if (!ba.completed()) {
        return 0;
      }
      command->reset(new StatsCommand(client, ProtocolType::REDIS));
      return ba.total_size();
    }

    command->reset(new ErrorCommand(client,
          std::string("-ERR YarmProxy unsupported redis directive:[") +
              ba[0].to_string() + "]\r\n"));
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
    size_t body_bytes = 0;
    LOG_WARN << "MemcachedSetCommand created(" << std::string(buf, cmd_line_bytes) << ")";
    command->reset(new MemcachedSetCommand(client, buf, cmd_line_bytes,
                                           &body_bytes));
    if (body_bytes <= 2) {
      return -1;
    }
    return cmd_line_bytes + body_bytes;
  } else if (strncmp(buf, "delete ", sizeof("delete ") - 1) == 0 ||
      strncmp(buf, "incr ", sizeof("incr ") - 1) == 0 ||
      strncmp(buf, "decr ", sizeof("decr ") - 1) == 0 ||
      strncmp(buf, "incr ", sizeof("incr ") - 1) == 0 ||
      strncmp(buf, "touch ", sizeof("touch ") - 1) == 0) {
    // TODO : strict protocol check
    command->reset(new MemcachedBasicCommand(client, buf, cmd_line_bytes));
    return cmd_line_bytes;
  }

  command->reset(new ErrorCommand(client,
        std::string("YarmProxy Unsupported Request [") +
          std::string(buf,cmd_line_bytes) + "]\r\n"));
  LOG_WARN << "ErrorCommand(" << std::string(buf, cmd_line_bytes) << ") len="
           << cmd_line_bytes << " client_conn=" << client;
  return size;
}

std::shared_ptr<BackendConn> Command::AllocateBackend(const Endpoint& ep) {
  auto backend = backend_pool()->Allocate(ep);
  backend->SetReadWriteCallback(
      WeakBind(&Command::OnWriteQueryFinished, backend),
      WeakBind(&Command::OnBackendReplyReceived, backend));
  return backend;
}

const std::string& Command::MemcachedErrorReply(ErrorCode ec) {
  static const std::string kErrorConnect("ERROR Backend Connect Error\r\n");
  static const std::string kErrorWriteQuery("ERROR Backend Write Error\r\n");
  static const std::string kErrorReadReply("ERROR Backend Read Error\r\n");
  static const std::string kErrorProtocol("ERROR Backend Protocol Error\r\n");
  static const std::string kErrorTimeout("ERROR Backend Timeout\r\n");
  static const std::string kErrorDefault("ERROR Backend Unknown Error\r\n");
  switch(ec) {
  case ErrorCode::E_CONNECT:
    return kErrorConnect;
  case ErrorCode::E_WRITE_QUERY:
    return kErrorWriteQuery;
  case ErrorCode::E_READ_REPLY:
    return kErrorReadReply;
  case ErrorCode::E_PROTOCOL:
    return kErrorProtocol;
  case ErrorCode::E_TIMEOUT:
    return kErrorTimeout;
  default:
    return kErrorDefault;
  }
}

const std::string& Command::RedisErrorReply(ErrorCode ec) {
  static const std::string kErrorConnect("-Backend Connect Error\r\n");
  static const std::string kErrorWriteQuery("-Backend Write Error\r\n");
  static const std::string kErrorReadReply("-Backend Read Error\r\n");
  static const std::string kErrorProtocol("-Backend Protocol Error\r\n");
  static const std::string kErrorTimeout("-Backend Timeout\r\n");
  static const std::string kErrorDefault("-Backend Unknown Error\r\n");
  switch(ec) {
  case ErrorCode::E_CONNECT:
    return kErrorConnect;
  case ErrorCode::E_WRITE_QUERY:
    return kErrorWriteQuery;
  case ErrorCode::E_READ_REPLY:
    return kErrorReadReply;
  case ErrorCode::E_PROTOCOL:
    return kErrorProtocol;
  case ErrorCode::E_TIMEOUT:
    return kErrorTimeout;
  default:
    return kErrorDefault;
  }
}

bool Command::BackendErrorRecoverable(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  return !has_written_some_reply_;
}

void Command::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_ERROR << "OnWriteQueryFinished err " << ErrorCodeMessage(ec)
             << " backend=" << backend
             << " has_written_some_reply_=" << has_written_some_reply_
             << " ep=" << backend->remote_endpoint();
    if (!BackendErrorRecoverable(backend, ec)) {
      client_conn_->Abort();
    } else {
      OnBackendRecoverableError(backend, ec);
      // TODO : no duplicate code
      if (query_data_zero_copy()) {
        client_conn_->buffer()->dec_recycle_lock();
        if (!query_recv_complete()) {
          client_conn_->TryReadMoreQuery("command_1");
        }
      }
    }
    return;
  }

  LOG_DEBUG << "OnWriteQueryFinished ok, backend=" << backend;

  if (query_data_zero_copy()) {
    client_conn_->buffer()->dec_recycle_lock();
    if (!query_recv_complete()) {
      client_conn_->TryReadMoreQuery("command_2");
      return;
    }
  }

  if (query_recv_complete()) {
    // begin to read reply
    backend->ReadReply();
  } else {
    // need more query
  }
}

void Command::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                        ErrorCode ec) {
  // assert(backend == backend_conn_);
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

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    // write reply
    TryWriteReply(backend);
  } else {
    // wait to write reply
  }
  backend->TryReadMoreReply();
}

void Command::OnWriteReplyFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_WARN << "Command::OnWriteReplyFinished error, backend="
             << backend;
    client_conn_->Abort();
    return;
  }

  assert(is_writing_reply_);
  is_writing_reply_ = false;
  backend->buffer()->dec_recycle_lock();

  if (backend->finished()) {
    assert(!backend->buffer()->recycle_locked());
    // write_reply开始时须recv_query结束, 包括connect error时也要遵守这一约定
    assert(query_recv_complete());
    RotateReplyingBackend(backend->recyclable());
  } else {
    backend->TryReadMoreReply(); // 这里必须继续try
    TryWriteReply(backend); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

/*
void Command::OnBackendError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (backend->has_read_some_reply()) {
    client_conn_->Abort();
    return;
  }
  auto& err_reply(RedisErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    LOG_WARN << "RedisGetCommand::OnBackendError TryWriteReply, backend=" << backend;
    TryWriteReply(backend);
  }
}
*/

void Command::OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(BackendErrorRecoverable(backend, ec));
  LOG_DEBUG << "OnBackendRecoverableError ec=" << int(ec)
            << "endpoint=" << backend->remote_endpoint()
            << " backend=" << backend;
  auto& err_reply(RedisErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
}

void Command::TryWriteReply(std::shared_ptr<BackendConn> backend) {
  size_t unprocessed = backend->buffer()->unprocessed_bytes();
  LOG_DEBUG << "Command::TryWriteReply backend=" << backend
              << " is_writing_reply_=" << is_writing_reply_
              << " unprocessed=" << unprocessed;
  if (!is_writing_reply_ && unprocessed > 0) {
    is_writing_reply_ = true;
    has_written_some_reply_ = true;
    backend->buffer()->inc_recycle_lock();
    client_conn_->WriteReply(backend->buffer()->unprocessed_data(),
        unprocessed, WeakBind(&Command::OnWriteReplyFinished, backend));
    backend->buffer()->update_processed_bytes(unprocessed);
  }
}

}

