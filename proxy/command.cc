#include "command.h"

#include <algorithm>
#include <vector>
#include <functional>

#include "logging.h"

#include "error_code.h"
#include "protocol_type.h"

#include "backend_conn.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "config.h"
#include "key_locator.h"
#include "read_buffer.h"
#include "worker_pool.h"

#include "error_command.h"
#include "stats_command.h"

#include "memc_basic_command.h"
#include "memc_get_command.h"
#include "memc_set_command.h"

#include "redis_protocol.h"
#include "redis_basic_command.h"
#include "redis_del_command.h"
#include "redis_mset_command.h"
#include "redis_mget_command.h"
#include "redis_set_command.h"

namespace yarmproxy {

//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
Command::Command(std::shared_ptr<ClientConnection> client, ProtocolType protocol)
    : client_conn_(client), protocol_(protocol) {
};

Command::~Command() {
  // TODO : backend_pool()->Release(replying_backend_);
}

BackendConnPool* Command::backend_pool() {
  return client_conn_->context().backend_conn_pool();
}

std::shared_ptr<KeyLocator> Command::key_locator() {
  return client_conn_->context().key_locator_;
}

enum class RedisCommandType {
  RCT_BASIC,
  RCT_SET,
  RCT_MSET,
  RCT_MGET,
  RCT_DEL,
  RCT_YARMSTATS,
  RCT_UNSUPPORTED,
};

static RedisCommandType GetRedisCommandType(const redis::Bulk& bulk) {
  static const std::map<std::string, RedisCommandType> kCommandNameType = {
      {"get",         RedisCommandType::RCT_BASIC},
      {"getset",      RedisCommandType::RCT_BASIC},
      {"getrange",    RedisCommandType::RCT_BASIC},
      {"ttl",         RedisCommandType::RCT_BASIC},
      {"incr",        RedisCommandType::RCT_BASIC},
      {"incrby",      RedisCommandType::RCT_BASIC},
      {"incrbyfloat", RedisCommandType::RCT_BASIC},
      {"decr",        RedisCommandType::RCT_BASIC},
      {"decrby",      RedisCommandType::RCT_BASIC},
      {"strlen",      RedisCommandType::RCT_BASIC},

      {"set",      RedisCommandType::RCT_SET},
      {"append",   RedisCommandType::RCT_SET},
      {"setrange", RedisCommandType::RCT_SET},
      {"setnx",    RedisCommandType::RCT_SET},
      {"psetex",   RedisCommandType::RCT_SET},
      {"setex",    RedisCommandType::RCT_SET},

      {"mset",   RedisCommandType::RCT_MSET},

      {"mget",   RedisCommandType::RCT_MGET},

      {"del",      RedisCommandType::RCT_DEL},
      {"exists",   RedisCommandType::RCT_DEL},
      {"touch",    RedisCommandType::RCT_DEL},

      {"yarmstats", RedisCommandType::RCT_YARMSTATS},
    };

  std::string cmd_name(bulk.payload_data(), bulk.payload_size());
  std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), tolower);
  auto it = kCommandNameType.find(cmd_name);
  if (it != kCommandNameType.cend()) {
    return it->second;
  }
  return RedisCommandType::RCT_UNSUPPORTED;
}

enum class MemcCommandType {
  MCT_GET,
  MCT_SET,
  MCT_BASIC,
  MCT_YARMSTATS,
  MCT_UNSUPPORTED,
};

static MemcCommandType GetMemcCommandType(const char* cmd_line, size_t size) {
  static const std::map<std::string, MemcCommandType> kCommandNameType = {
      {"get",     MemcCommandType::MCT_GET},
      {"gets",    MemcCommandType::MCT_GET},

      {"set",     MemcCommandType::MCT_SET},
      {"add",     MemcCommandType::MCT_SET},
      {"replace", MemcCommandType::MCT_SET},
      {"append",  MemcCommandType::MCT_SET},
      {"prepend", MemcCommandType::MCT_SET},
      {"cas",     MemcCommandType::MCT_SET},

      {"delete",  MemcCommandType::MCT_BASIC},
      {"incr",    MemcCommandType::MCT_BASIC},
      {"decr",    MemcCommandType::MCT_BASIC},
      {"touch",   MemcCommandType::MCT_BASIC},

      {"yarmstats", MemcCommandType::MCT_YARMSTATS},
    };

  const char * p = static_cast<const char *>(memchr(cmd_line, ' ', size));
  if (p == nullptr) {
    p = cmd_line + size;
  }
  auto it = kCommandNameType.find(std::string(cmd_line, p - cmd_line));
  if (it != kCommandNameType.cend()) {
    return it->second;
  }
  return MemcCommandType::MCT_UNSUPPORTED;
}

// return : bytes parsed, 0 if no adquate data to parse
size_t Command::CreateCommand(std::shared_ptr<ClientConnection> client,
                           const char* buf, size_t size,
                           std::shared_ptr<Command>* command) {
  const char * p = static_cast<const char *>(memchr(buf, '\n', size));
  if (p == nullptr) {
    if (size > Config::Instance().buffer_size() / 2) {
      std::string err_desc(*buf == '*' ? "-" : "");
      err_desc.append("ERR Too long unparsable data:[")
              .append(std::string(buf, size))
              .append("]\r\n");
      command->reset(new ErrorCommand(client, std::move(err_desc)));
      return size;
    } else {
      LOG_DEBUG << "CreateCommand need more data";
      return 0;
    }
  }

  if (strncmp(buf, "*", 1) == 0) {
    redis::BulkArray ba(buf, size);
    if (ba.total_bulks() == 0) {
      LOG_WARN << "CreateCommand data_size=" << size
               << " bad_data=[" << std::string(buf, size) << "]";

      command->reset(new ErrorCommand(client,
          std::string("-ERR Bulk Array Parse Error:[") +
              std::string(buf, size) + "]\r\n"));
      return size;
    }
    if (ba.present_bulks() == 0 || ba[0].absent_size() > 0) {
      return 0;
    }

    switch(GetRedisCommandType(ba[0])) {
    case RedisCommandType::RCT_BASIC:
      if (!ba.completed()) {
        return 0;
      }
      command->reset(new RedisBasicCommand(client, ba));
      return ba.total_size();
    case RedisCommandType::RCT_SET:
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        return 0;
      }
      command->reset(new RedisSetCommand(client, ba));
      return ba.parsed_size();
    case RedisCommandType::RCT_MSET:
      if (ba.present_bulks() < 3) {
        return 0;
      }
      command->reset(new RedisMsetCommand(client, ba));
      if (ba.present_bulks() % 2 == 0) {
        return ba.parsed_size() - ba.back().total_size();
      } else {
        return ba.parsed_size();
      }
    case RedisCommandType::RCT_MGET:
      if (!ba.completed()) { // TODO : support incomplete mget bulk_array
        return 0;
      }
      command->reset(new RedisMgetCommand(client, ba));
      return ba.total_size();
    case RedisCommandType::RCT_DEL:
      if (ba.present_bulks() < 2 || !ba[1].completed()) {
        return 0;
      }
      command->reset(new RedisDelCommand(client, ba));
      if (ba.back().completed()) {
        return ba.parsed_size();
      } else {
        return ba.parsed_size() - ba.back().total_size();
      }
    case RedisCommandType::RCT_YARMSTATS:
      if (!ba.completed()) {
        return 0;
      }
      command->reset(new StatsCommand(client, ProtocolType::REDIS));
      return ba.total_size();
    default:
      command->reset(new ErrorCommand(client,
            std::string("-ERR YarmProxy unsupported redis command:[") +
                ba[0].to_string() + "]\r\n"));
      return size;
    }
  }

  {
    // TODO : support memcached binary
  }

  // TODO : support memcached noreply mode
  size_t cmd_line_bytes = p - buf + 1;
  size_t body_bytes = 0;
  switch(GetMemcCommandType(buf, cmd_line_bytes)) {
  case MemcCommandType::MCT_GET:
    command->reset(new MemcGetCommand(client, buf, cmd_line_bytes));
    return cmd_line_bytes;
  case MemcCommandType::MCT_SET:
    command->reset(new MemcSetCommand(client, buf, cmd_line_bytes,
                   &body_bytes));
    if (body_bytes <= 2) {
      command->reset(new ErrorCommand(client,
          std::string("ERR Protocol Error:[") +
              std::string(buf, cmd_line_bytes) + "]\r\n"));
      return cmd_line_bytes;
    }
    return cmd_line_bytes + body_bytes;
  case MemcCommandType::MCT_BASIC:
    command->reset(new MemcBasicCommand(client, buf));
    return cmd_line_bytes;
  case MemcCommandType::MCT_YARMSTATS:
    command->reset(new StatsCommand(client, ProtocolType::MEMCACHED));
    return cmd_line_bytes;
  default:
    command->reset(new ErrorCommand(client,
          std::string("YarmProxy Unsupported Request [") +
            std::string(buf,cmd_line_bytes) + "]\r\n"));
    LOG_WARN << "ErrorCommand(" << std::string(buf, cmd_line_bytes) << ") len="
             << cmd_line_bytes << " client_conn=" << client;
    return size;
  }
}

const std::string& Command::MemcErrorReply(ErrorCode ec) {
  static const std::string kErrorConnect("ERROR Backend Connect Error\r\n");
  static const std::string kErrorWriteQuery("ERROR Backend Write Error\r\n");
  static const std::string kErrorReadReply("ERROR Backend Read Error\r\n");
  static const std::string kErrorProtocol("ERROR Backend Protocol Error\r\n");
  // static const std::string kErrorTimeout("ERROR Backend Timeout\r\n");
  static const std::string kErrorConnectTimeout("ERROR Backend Connect Timeout\r\n");
  static const std::string kErrorWriteTimeout("ERROR Backend Write Timeout\r\n");
  static const std::string kErrorReadTimeout("ERROR Backend Read Timeout\r\n");
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
//case ErrorCode::E_TIMEOUT:
//  return kErrorTimeout;
  case ErrorCode::E_BACKEND_CONNECT_TIMEOUT:
    return kErrorConnectTimeout;
  case ErrorCode::E_BACKEND_WRITE_TIMEOUT:
    return kErrorWriteTimeout;
  case ErrorCode::E_BACKEND_READ_TIMEOUT:
    return kErrorReadTimeout;
  default:
    return kErrorDefault;
  }
}

const std::string& Command::RedisErrorReply(ErrorCode ec) {
  static const std::string kErrorConnect("-Backend Connect Error\r\n");
  static const std::string kErrorWriteQuery("-Backend Write Error\r\n");
  static const std::string kErrorReadReply("-Backend Read Error\r\n");
  static const std::string kErrorProtocol("-Backend Protocol Error\r\n");
  static const std::string kErrorConnectTimeout("-Backend Connect Timeout\r\n");
  static const std::string kErrorWriteTimeout("-Backend Write Timeout\r\n");
  static const std::string kErrorReadTimeout("-Backend Read Timeout\r\n");
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
//case ErrorCode::E_TIMEOUT:
//  return kErrorTimeout;
  case ErrorCode::E_BACKEND_CONNECT_TIMEOUT:
    return kErrorConnectTimeout;
  case ErrorCode::E_BACKEND_WRITE_TIMEOUT:
    return kErrorWriteTimeout;
  case ErrorCode::E_BACKEND_READ_TIMEOUT:
    return kErrorReadTimeout;
  default:
    return kErrorDefault;
  }
}

bool Command::BackendErrorRecoverable(std::shared_ptr<BackendConn>, ErrorCode) {
  return !has_written_some_reply_;
}

bool Command::StartWriteQuery() {
  assert(replying_backend_);
  check_query_recv_complete();

  replying_backend_->SetReadWriteCallback(
      WeakBind(&Command::OnWriteQueryFinished, replying_backend_),
      WeakBind(&Command::OnBackendReplyReceived, replying_backend_));

  LOG_DEBUG << "Command " << this << " StartWriteQuery backend=" << replying_backend_
           << " ep=" << replying_backend_->remote_endpoint();
  client_conn_->buffer()->inc_recycle_lock();
  replying_backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
                            client_conn_->buffer()->unprocessed_bytes());
  return false;
}

bool Command::ContinueWriteQuery() {
  check_query_recv_complete();

  if (replying_backend_->error()) {
    if (!query_recv_complete()) {
      return true; // no callback, try read more query directly
    }
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(replying_backend_);
    } else {
      // wait to write reply
    }
    return false;
  }

  client_conn_->buffer()->inc_recycle_lock();
  replying_backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
                                client_conn_->buffer()->unprocessed_bytes());
  return false;
}


void Command::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend,
                                   ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    LOG_DEBUG << "OnWriteQueryFinished err " << ErrorCodeString(ec)
             << " backend=" << backend
             << " client_buffer=" << client_conn_->buffer()
             << " has_written_some_reply_=" << has_written_some_reply_
             << " ep=" << backend->remote_endpoint();
    if (!BackendErrorRecoverable(backend, ec)) {
      client_conn_->Abort();
    } else {
      client_conn_->buffer()->dec_recycle_lock();
      OnBackendRecoverableError(backend, ec);
    }
    return;
  }

  LOG_DEBUG << "OnWriteQueryFinished ok, backend=" << backend
             << " client_buffer=" << client_conn_->buffer()
             << " has_written_some_reply_=" << has_written_some_reply_
             << " ep=" << backend->remote_endpoint();

  client_conn_->buffer()->dec_recycle_lock();

  if (query_recv_complete()) {
    // begin to read reply
    backend->ReadReply();
  } else {
    // need more query
    if (!client_conn_->buffer()->recycle_locked()) {
      // LOG_ERROR << "TryReadMoreQuery command_4";
      client_conn_->TryReadMoreQuery("command_4");
    }
  }
}

void Command::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend,
                                     ErrorCode ec) {
  if (ec == ErrorCode::E_SUCCESS && !ParseReply(backend)) {
    ec = ErrorCode::E_PROTOCOL;
  }
  LOG_DEBUG << "Command " << this << " OnBackendReplyReceived, backend=" << backend
            << " ec=" << ErrorCodeString(ec)
            << " recoveralbe=" << BackendErrorRecoverable(backend, ec);
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

 LOG_DEBUG << "Command " << this << " OnWriteReplyFinished ok, backend="
          << backend << " backend->finished=" << backend->finished()
          << " backend->unprocessed=" << backend->buffer()->unprocessed_bytes()
          << " backend_buf=" << backend->buffer();
  assert(is_writing_reply_);
  is_writing_reply_ = false;
  backend->buffer()->dec_recycle_lock();

  if (backend->finished()) {
    assert(!backend->buffer()->recycle_locked());
    // write_reply开始时须recv_query结束, 包括connect error时也要遵守这一约定
    assert(query_recv_complete());
    RotateReplyingBackend();
  } else {
    backend->TryReadMoreReply(); // 这里必须继续try
    TryWriteReply(backend); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

const std::string& Command::ErrorReply(ErrorCode ec) {
  switch(protocol_) {
  case ProtocolType::REDIS:
    return RedisErrorReply(ec);
  case ProtocolType::MEMCACHED:
  default:
    return MemcErrorReply(ec);
  }
}
void Command::OnBackendRecoverableError(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  assert(BackendErrorRecoverable(backend, ec));
  LOG_DEBUG << "OnBackendRecoverableError ec=" << ErrorCodeString(ec)
            << " endpoint=" << backend->remote_endpoint()
            << " backend=" << backend;
  auto& err_reply(ErrorReply(ec));
  backend->SetReplyData(err_reply.data(), err_reply.size());
  backend->set_reply_recv_complete();
  backend->set_no_recycle();

  if (query_recv_complete()) {
    if (client_conn_->IsFirstCommand(shared_from_this())) {
      // write reply
      TryWriteReply(backend);
    } else {
      // waiting to write reply
    }
  } else {
    // wait for more query data
    // assert(client_conn_->buffer()->recycle_locked());
    LOG_DEBUG << "TryReadMoreQuery command_3 ec=" << ErrorCodeString(ec)
              << " unparsed=" << client_conn_->buffer()->unparsed_bytes()
              << " unprocessed=" << client_conn_->buffer()->unprocessed_bytes()
              << " free_space_size=" << client_conn_->buffer()->free_space_size()
              << " has_much_space=" << client_conn_->buffer()->has_much_free_space();
    if (!client_conn_->buffer()->recycle_locked()) {
      client_conn_->TryReadMoreQuery("command_3");
    }
  }
}

bool Command::ParseRedisSimpleReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed_bytes = backend->buffer()->unparsed_bytes();
  if (unparsed_bytes == 0) { // bottom-half of a bulk string
    if (backend->buffer()->parsed_unreceived_bytes() == 0) {
      backend->set_reply_recv_complete();
    }
    return true;
  }

  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != ':' && entry[0] != '+' &&
      entry[0] != '-' && entry[0] != '$') {
    LOG_WARN << "RedisBasicCommand ParseReply error ["
             << std::string(entry, unparsed_bytes) << "]";
    return false;
  }

  const char * p = static_cast<const char *>(
                       memchr(entry, '\n', unparsed_bytes));
  if (p == nullptr) {
    return true;
  }

  if (entry[0] == '$') {
    redis::Bulk bulk(entry, unparsed_bytes);
    if (bulk.present_size() < 0) {
      return false;
    }
    if (bulk.present_size() == 0) {
      return true;
    }
    if (bulk.completed()) {
      LOG_DEBUG << "ParseReply bulk completed";
      backend->set_reply_recv_complete();
    }
    backend->buffer()->update_parsed_bytes(bulk.total_size());
  } else {
    backend->set_reply_recv_complete();
    backend->buffer()->update_parsed_bytes(p - entry + 1);
  }

  LOG_DEBUG << "Command ParseReply ok, resp.size=" << p - entry + 1
            << " backend=" << backend;
  return true;
}

bool Command::ParseMemcSimpleReply(std::shared_ptr<BackendConn> backend) {
  const char * entry = backend->buffer()->unparsed_data();
  const char * p = static_cast<const char *>(memchr(entry, '\n',
                       backend->buffer()->unparsed_bytes()));
  if (p != nullptr) {
    backend->buffer()->update_parsed_bytes(p - entry + 1);
    backend->set_reply_recv_complete();
  }
  return true;
}

bool Command::ParseReply(std::shared_ptr<BackendConn> backend) {
  if (protocol_ == ProtocolType::REDIS) {
    return ParseRedisSimpleReply(backend);
  } else {
    return ParseMemcSimpleReply(backend);
  }
}

void Command::StartWriteReply() {
  if (query_recv_complete() && replying_backend_) {
    TryWriteReply(replying_backend_);
  }
}

void Command::TryWriteReply(std::shared_ptr<BackendConn> backend) {
  size_t unprocessed = backend->buffer()->unprocessed_bytes();
  LOG_DEBUG << "Command::TryWriteReply backend=" << backend
              << " ep=" << backend->remote_endpoint()
              << " is_writing_reply_=" << is_writing_reply_
              << " reply_recv_complete=" << backend->reply_recv_complete()
              << " backend_buf=" << backend->buffer()
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

void Command::RotateReplyingBackend() {
  assert(query_recv_complete());
  client_conn_->RotateReplyingCommand();
}

}

