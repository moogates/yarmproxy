#include "memc_command.h"

#include <vector>
#include <functional>

#include <boost/asio.hpp>

#include "base/logging.h"

#include "worker_pool.h"
#include "client_conn.h"
#include "memcached_locator.h"
#include "backend_conn.h"

#include "get_command.h"
#include "write_command.h"

namespace mcproxy {

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
  LOG_DEBUG << "GroupKeysByEndpoint enter, cmd=[" << std::string(cmd_line, cmd_line_size - 2) << "]";
  for(const char* p = cmd_line + 4/*strlen("get ")*/; p < cmd_line + cmd_line_size - 2/*strlen("\r\n")*/; ++p) {
    const char* q = p;
    while(*q != ' ' && *q != '\r') {
      ++q;
    }
    ip::tcp::endpoint ep = MemcachedLocator::Instance().GetEndpointByKey(p, q - p);

    auto it = endpoint_key_map->find(ep);
    if (it == endpoint_key_map->end()) {
      it = endpoint_key_map->insert(std::make_pair(ep, std::string("get"))).first;
    }

    it->second.append(p - 1, 1 + q - p);
    LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first << " key=[" << std::string(p-1, 1+q-p) << "]"
              << " current=" << it->second;

    p = q;
  }
  LOG_DEBUG << "GroupKeysByEndpoint exit, endpoint_key_map.size=" << endpoint_key_map->size();
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


//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
MemcCommand::MemcCommand(const ip::tcp::endpoint & ep, 
    std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len) 
  : is_transfering_response_(false)
  , backend_endpoint_(ep)
  , backend_conn_(nullptr)
  , client_conn_(owner)
  , context_(owner->context())
  , loaded_(false)
{
};

// 0 : ok, 数据不够解析
// >0 : ok, 解析成功，返回已解析的字节数
// <0 : error, 未知命令
int MemcCommand::CreateCommand(std::shared_ptr<ClientConnection> owner, const char* buf, size_t size,
          std::list<std::shared_ptr<MemcCommand>>* sub_commands) {
  const char * p = GetLineEnd(buf, size);
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand no complete cmd line found";
    return 0;
  }

  size_t cmd_line_bytes = p - buf + 1; // 请求 命令行 长度

  if (strncmp(buf, "get ", 4) == 0) {
#define DONT_USE_MAP 0
#if DONT_USE_MAP
    auto ep = MemcachedLocator::Instance().GetEndpointByKey("1");
    std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(context_.io_service_,
                     ep, owner, buf, cmd_line_bytes));
    sub_commands->push_back(cmd);
    LOG_DEBUG << "DONT_USE_MAP ep=" << ep << " keys=" << std::string(buf, cmd_line_bytes - 2);
    return cmd_line_bytes;
#endif
    std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
    GroupKeysByEndpoint(buf, cmd_line_bytes, &endpoint_key_map);
    for (auto it = endpoint_key_map.begin(); it != endpoint_key_map.end(); ++it) {
      LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first << " keys=" << it->second;
      it->second += "\r\n";
      std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(it->first, owner, it->second.c_str(), it->second.size()));
      // fetching_cmd_set_.insert(cmd);
      // subcommands.push_back(cmd);
      sub_commands->push_back(cmd);
    }
    return cmd_line_bytes;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0 || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    std::string key;
    size_t body_bytes;
    ParseWriteCommandLine(buf, cmd_line_bytes, &key, &body_bytes);

    //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
    std::shared_ptr<MemcCommand> cmd(new WriteCommand(MemcachedLocator::Instance().GetEndpointByKey(key),
              owner, buf, cmd_line_bytes, body_bytes));
    sub_commands->push_back(cmd);
    return cmd_line_bytes + body_bytes;
  } else {
    LOG_WARN << "CreateCommand unknown command(" << std::string(buf, cmd_line_bytes - 2)
             << ") len=" << cmd_line_bytes << " client_conn=" << owner.get();
    return -1;
  }
}

void MemcCommand::OnUpstreamResponseReceived(BackendConn* backend, const boost::system::error_code& error) {
  if (error) {
    LOG_WARN << "MemcCommand::OnUpstreamResponseReceived " << cmd_line_without_rn()
             << " backend read error : " << backend->remote_endpoint() << " - "  << error << " " << error.message();
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }

  bool valid = ParseUpstreamResponse(backend);
  if (!valid) {
    LOG_WARN << "MemcCommand parsing error! valid=false";
    // TODO : error handling
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }

  if (IsFormostCommand()) {
    TryForwardResponse(backend_conn_);
  } else {
    // TODO : do nothing, just wait
    LOG_DEBUG << "MemcCommand IsFormostCommand false!";
  }

  if (!backend_nomore_response()) {
    backend_conn_->TryReadMoreData(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
  }
}

MemcCommand::~MemcCommand() {
  if (backend_conn_) {
    context_.backend_conn_pool_->Release(backend_conn_);
  }
}

bool MemcCommand::backend_nomore_response() {
  return backend_conn_->read_buffer_.parsed_unreceived_bytes() == 0;
}

bool MemcCommand::IsFormostCommand() {
  return client_conn_->IsFirstCommand(shared_from_this());
}

void MemcCommand::ForwardRequest(const char * data, size_t bytes) {
  if (backend_conn_ == nullptr) {
    // LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn, worker_id=" << WorkerPool::CurrentWorkerId();
    LOG_DEBUG << "MemcCommand(" << cmd_line_without_rn() << ") create backend conn";
    backend_conn_ = context_.backend_conn_pool_->Allocate(backend_endpoint_);
    backend_conn_->SetReadWriteCallback(WeakBind(&MemcCommand::OnForwardMoreRequest),
                               WeakBind2(&MemcCommand::OnUpstreamResponseReceived, backend_conn_));
  }

  DoForwardRequest(data, bytes);
}

void MemcCommand::Abort() {
  if (backend_conn_) {
    backend_conn_->socket().close();
    // MCE_INFO("MemcCommand Abort OK.");
    delete backend_conn_;
    backend_conn_ = 0;
  } else {
    // MCE_WARN("MemcCommand Abort NULL backend_conn_.");
  }
}

void MemcCommand::OnForwardReplyFinished(const boost::system::error_code& error) {
  if (error) {
    // TODO
    LOG_DEBUG << "WriteCommand::OnForwardReplyFinished(" << cmd_line_without_rn() << ") error=" << error;
    return;
  }
  // backend_conn_->read_buffer_.update_processed_bytes(forwarded_bytes);
  backend_conn_->read_buffer_.dec_recycle_lock();

  if (backend_nomore_response() && backend_conn_->read_buffer_.unprocessed_bytes() == 0) {
    client_conn_->RotateFirstCommand();
    LOG_DEBUG << "WriteCommand::OnForwardReplyFinished backend_nomore_response, and all data forwarded to client";
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardReplyFinished ready_to_transfer_bytes=" << backend_conn_->read_buffer_.unprocessed_bytes();
    is_transfering_response_ = false;
    if (!backend_nomore_response()) {
      backend_conn_->TryReadMoreData(); // 这里必须继续try
    }
    TryForwardResponse(backend_conn_); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

void MemcCommand::TryForwardResponse(BackendConn* backend) {
  size_t unprocessed = backend->read_buffer_.unprocessed_bytes();
  if (!is_transfering_response_ && unprocessed > 0) {
    is_transfering_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
    client_conn_->ForwardResponse(backend->read_buffer_.unprocessed_data(), unprocessed,
                                  WeakBind(&MemcCommand::OnForwardReplyFinished));
    // backend->read_buffer_.lock_memmove(); // FIXME : lock begin at read-start, finishes at sent-done
    backend->read_buffer_.update_processed_bytes(unprocessed);
    LOG_WARN << "MemcCommand::TryForwardResponse to_process_bytes=" << unprocessed
              << " new_unprocessed=" << backend->read_buffer_.unprocessed_bytes()
              << " client=" << this << " backend=" << backend;
  } else {
    LOG_DEBUG << "MemcCommand::TryForwardResponse do nothing, unprocessed_bytes=" << unprocessed
              << " is_transfering_response=" << is_transfering_response_
              << " client=" << this << " backend=" << backend;
  }
}

}

