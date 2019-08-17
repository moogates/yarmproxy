#include "client_conn.h"

// #include <boost/algorithm/string.hpp>

#include <atomic>
#include <memory>

#include "base/logging.h"

#include "get_command.h"
#include "write_command.h"
#include "memcached_locator.h"
#include "upstream_conn.h"

using namespace boost::asio;

namespace mcproxy {

// TODO :
// 1. gracefully close connections
// 2. 

std::atomic_int g_cc_count;

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

ClientConnection::ClientConnection(boost::asio::io_service& io_service, UpstreamConnPool * pool)
  : io_service_(io_service)
  , socket_(io_service)
  , buf_lock_(0)
  , up_buf_begin_(0)
  , up_buf_end_(0)
  , parsed_bytes_(0)
  , upconn_pool_(pool)
  //, current_ready_cmd_(NULL)
  , timeout_(0) // TODO : timeout timer 
  , timer_(io_service)
{
  LOG_INFO << "ClientConnection destroyed." << ++g_cc_count;
}

ClientConnection::~ClientConnection() {
  if (socket_.is_open()) {
    LOG_INFO << "ClientConnection destroyed close socket."; 
    socket_.close();
  } else {
    LOG_INFO << "ClientConnection destroyed need not close socket.";
  }
  LOG_INFO << "ClientConnection destroyed." << --g_cc_count;
}

void ClientConnection::Start() {
  ip::tcp::no_delay nodelay(true);
  socket_.set_option(nodelay);

  boost::asio::socket_base::linger linger(true, 0);
  socket_.set_option(linger);

  boost::asio::socket_base::send_buffer_size send_buf_size(48 * 1024);
  socket_.set_option(send_buf_size);

  AsyncRead();
}

void ClientConnection::TryReadMoreRequest() {
  // TODO : add preconditions check
  AsyncRead();
}

void ClientConnection::AsyncRead()
{
  timer_.cancel();

  socket_.async_read_some(boost::asio::buffer(buf_ + up_buf_end_, kBufLength - up_buf_end_),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, // 占位符
          std::placeholders::_2));
}

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr) {
  return [cmd_wptr](const boost::system::error_code& error) {
           // if (std::shared_ptr<MemcCommand> cmd = cmd_wptr.lock()) {
           if (auto cmd_ptr = cmd_wptr.lock()) {
             LOG_DEBUG << "OnUpstreamResponse cmd ok";
             cmd_ptr->OnUpstreamResponse(error);
           } else {
             LOG_DEBUG << "OnUpstreamResponse cmd released";
           }
         };

  // TODO : 梳理资源管理和释放的时机
  auto cmd_ptr = cmd_wptr.lock();
  return [cmd_ptr](const boost::system::error_code& error) {
           cmd_ptr->OnUpstreamResponse(error);
         };
}

UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr) {
  return [cmd_wptr](size_t written_bytes, const boost::system::error_code& error) {
           // if (std::shared_ptr<MemcCommand> cmd = cmd_wptr.lock()) {
           if (auto cmd_ptr = cmd_wptr.lock()) {
             LOG_DEBUG << "OnUpstreamRequestWritten cmd ok";
             cmd_ptr->OnUpstreamRequestWritten(written_bytes, error);
           } else {
             LOG_DEBUG << "OnUpstreamRequestWritten cmd released";
           }
         };
}

ForwardResponseCallback WrapOnForwardResponseFinished(size_t to_transfer_bytes, std::weak_ptr<MemcCommand> cmd_wptr) { 
    // std::weak_ptr<MemcCommand> cmd_wptr = shared_from_this();
    // typedef std::function<void(size_t bytes, const boost::system::error_code& error)> ForwardResponseCallback;
    return [to_transfer_bytes, cmd_wptr](const boost::system::error_code& error) {
                          if (auto cmd_ptr = cmd_wptr.lock()) {
                            LOG_DEBUG << "OnForwardResponseFinished cmd weak_ptr valid";
                            cmd_ptr->OnForwardResponseFinished(to_transfer_bytes, error);
                          } else {
                            LOG_DEBUG << "OnForwardResponseFinished cmd weak_ptr released";
                          }
                        };
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

//for (auto it = cmd_line_map.begin(); it != cmd_line_map.end(); ++it) {
//  LOG_DEBUG << "GroupGetKeys " << it->first << " get_keys=" << it->second;
//  it->second += "\r\n";
//  std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(io_service_, it->first, client_conn_, it->second.c_str(), it->second.size()));
//  // fetching_cmd_set_.insert(cmd);
//  subcommands.push_back(cmd);
//}
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
// 0 : ok, 数据不够解析
// >0 : ok, 解析成功，返回已解析的字节数
// <0 : error, 未知命令
// lock_buffer : 是否锁定buffer, 如果锁定，则不可以memmove
int MemcCommand::CreateCommand(boost::asio::io_service& asio_service,
          std::shared_ptr<ClientConnection> owner, const char* buf, size_t size,
          size_t* cmd_line_bytes, size_t* body_bytes, bool* lock_buffer, std::list<std::shared_ptr<MemcCommand>>* sub_commands) {
  const char * p = GetLineEnd(buf, size);
  if (p == nullptr) {
    LOG_DEBUG << "CreateCommand no complete cmd line found";
    return 0;
  }

  *cmd_line_bytes = p - buf + 1; // 请求 命令行 长度

  if (strncmp(buf, "get ", 4) == 0) {
#define DONT_USE_MAP 0
#if DONT_USE_MAP
    auto ep = MemcachedLocator::Instance().GetEndpointByKey("1");
    std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(asio_service,
                     ep, owner, buf, *cmd_line_bytes));
    sub_commands->push_back(cmd);
    LOG_DEBUG << "DONT_USE_MAP ep=" << ep << " keys=" << std::string(buf, *cmd_line_bytes - 2);
    *lock_buffer = false;
    return *cmd_line_bytes;
#endif
    std::map<ip::tcp::endpoint, std::string> endpoint_key_map;
    GroupKeysByEndpoint(buf, *cmd_line_bytes, &endpoint_key_map);
    for (auto it = endpoint_key_map.begin(); it != endpoint_key_map.end(); ++it) {
      LOG_DEBUG << "GroupKeysByEndpoint ep=" << it->first << " keys=" << it->second;
      it->second += "\r\n";
      std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(asio_service, it->first, owner, it->second.c_str(), it->second.size()));
      // fetching_cmd_set_.insert(cmd);
      // subcommands.push_back(cmd);
      sub_commands->push_back(cmd);
    }
    *lock_buffer = false;
    return *cmd_line_bytes;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0 || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    std::string key;
    ParseWriteCommandLine(buf, *cmd_line_bytes, &key, body_bytes);

    //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
    const char * q = buf, * r = buf; 
    while(q < p) {
      if(*(++q) == ' ') {
        if (*r == ' ') {
          break;
        } else {
          r = q;
        }
      }
    }

    std::shared_ptr<MemcCommand> cmd(new WriteCommand(asio_service,
              MemcachedLocator::Instance().GetEndpointByKey(r + 1, q - r - 1),
              owner, buf, *cmd_line_bytes, *body_bytes));
    sub_commands->push_back(cmd);
    *lock_buffer = true;
    return *cmd_line_bytes + *body_bytes;
  } else {
    LOG_WARN << "CreateCommand unknown command(" << std::string(buf, *cmd_line_bytes - 2)
             << ") len=" << *cmd_line_bytes << " client_conn=" << owner.get();
    return -1;
  }
}

void ClientConnection::RotateFirstCommand() {
  poly_cmd_queue_.pop_front();
  if (!poly_cmd_queue_.empty()) {
    poly_cmd_queue_.front()->OnForwardResponseReady();
  }
}

void ClientConnection::ForwardResponse(const char* data, size_t bytes, const ForwardResponseCallback& cb) {
  forward_resp_callback_ = cb;

  std::weak_ptr<ClientConnection> wptr = shared_from_this();
  auto cb_wrap = [wptr, data, bytes, cb](const boost::system::error_code& error, size_t bytes_transferred) {
    LOG_DEBUG << "ClientConnection::ForwardResponse callback begin, bytes_transferred=" << bytes_transferred;
    if (!error && bytes_transferred < bytes) {
      if (auto ptr = wptr.lock()) {
        LOG_DEBUG << "ClientConnection::ForwardResponse try write more, bytes_transferred=" << bytes_transferred
                 << " left_bytes=" << bytes - bytes_transferred << " conn=" << ptr.get();
        ptr->ForwardResponse(data + bytes_transferred, bytes - bytes_transferred, cb);
      } else {
        LOG_DEBUG << "ClientConnection::ForwardResponse try write more, but conn released";
      }
    } else {
      LOG_DEBUG << "ClientConnection::ForwardResponse callback, bytes_transferred=" << bytes_transferred
               << " total_bytes=" << bytes << " error=" << error << "-" << error.message();
      cb(error);  // 发完了，或出错了，才告知MemcCommand
    }

    return;
  };

  boost::asio::async_write(socket_, boost::asio::buffer(data, bytes), cb_wrap);
}

void ClientConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    if (error == boost::asio::error::eof) {
      LOG_INFO << "ClientConnection::HandleRead : client connection closed. conn=" << this;
    } else {
      LOG_WARN << "ClientConnection::HandleRead error=" << error.message() << " conn=" << this;
    }
    return;
  }

  // TODO : bytes_transferred == 0, 如何处理? 此时会eof，前面已经处理

  up_buf_end_ += bytes_transferred;

  if (cmd_need_more_data_) { // 当前已经解析但还需要更多数据的command
    LOG_INFO << "ClientConnection::HandleRead.cmd_need_more_data_ : up_buf_begin_=" << up_buf_begin_
               << " bytes=" << (up_buf_end_ - up_buf_begin_)
               << " bytes_transferred=" << bytes_transferred
               << ". conn=" << this;

    recursive_lock_buffer();
    if (cmd_need_more_data_->request_body_upcoming_bytes() > (up_buf_end_ - up_buf_begin_)) {
      update_processed_bytes(up_buf_end_ - up_buf_begin_);
      LOG_INFO << "ClientConnection::HandleRead.ForwardRequest : up_buf_begin_=" << up_buf_begin_
               << " bytes=" << (up_buf_end_ - up_buf_begin_) << ". HAS MORE DATA. conn=" << this;
      cmd_need_more_data_->ForwardRequest(buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);  
      // 尚未转发cmd_need_more_data_的全部数据
      return;
    } else {
      LOG_INFO << "ClientConnection::HandleRead.ForwardRequest : up_buf_begin_=" << up_buf_begin_
               << " bytes=" << cmd_need_more_data_->request_body_upcoming_bytes() << ". NO MORE DATA. conn=" << this;
      cmd_need_more_data_->ForwardRequest(buf_ + up_buf_begin_, cmd_need_more_data_->request_body_upcoming_bytes());  
      update_processed_bytes(cmd_need_more_data_->request_body_upcoming_bytes());
      cmd_need_more_data_.reset();
      // 终于转发cmd_need_more_data_的全部数据,  继续处理其他命令
    }
  }

  size_t cmd_line_bytes = 0;

  while(parsed_bytes_ < up_buf_end_) { // TODO : 提取buffer对象
    size_t body_bytes = 0;
    bool lock_buffer = true;

    std::list<std::shared_ptr<MemcCommand>> sub_commands;
    int parsed_bytes = MemcCommand::CreateCommand(io_service_,
          shared_from_this(), (const char*)(buf_ + up_buf_begin_), up_buf_end_ - up_buf_begin_,
          &cmd_line_bytes, &body_bytes, &lock_buffer, &sub_commands);
    if (parsed_bytes < 0) {
      // TODO : error handling
      socket_.close();
      return;
    }  else if (parsed_bytes == 0) {
      AsyncRead(); // read more data
      return;
    } else {
      LOG_DEBUG << "ClientConnection::HandleRead CreateCommand ok, cmd_line_size=" << cmd_line_bytes
                << " body_bytes=" << body_bytes
                << " total_bytes=" << (cmd_line_bytes + body_bytes)
                << " parsed_bytes=" << parsed_bytes
                << " received_bytes=" << (up_buf_end_ - up_buf_begin_)
                << " sub_commands.size=" << sub_commands.size();
      for(auto entry : sub_commands) { // TODO : 要控制单client的并发command数
        LOG_DEBUG << "new version command created";
        if (lock_buffer) {
          assert(sub_commands.size() == 1);
          if (parsed_bytes > up_buf_end_ - up_buf_begin_) {
            entry->ForwardRequest(buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);  
            cmd_need_more_data_ = entry;
          } else {
            entry->ForwardRequest(buf_ + up_buf_begin_, (size_t)parsed_bytes);  
          }
        } else {
          entry->ForwardRequest(nullptr, 0);  
        }
      }
      poly_cmd_queue_.splice(poly_cmd_queue_.end(), sub_commands);
      parsed_bytes_ += parsed_bytes;
      up_buf_begin_ += std::min((size_t)parsed_bytes, up_buf_end_ - up_buf_begin_);
      if (lock_buffer) {
        recursive_lock_buffer();
      }
    }
  }

  // 一次只处理一组命令! 这个条件非常关键. 
  // TODO : 上一行注释是老代码的逻辑，新代码要支持pipeline
//LOG_DEBUG << "ClientConnection::HandleRead fetching_cmd_set_.size() : " << fetching_cmd_set_.size();
//if (!is_new_version && fetching_cmd_set_.empty()) {
//  cmd_line_bytes = MapMemcCommand(buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
//}

//if (cmd_line_bytes < 0) {
//  LOG_WARN << "ClientConnection::HandleRead bad request";
//  socket_.close();
//  return;
//}
//LOG_DEBUG << "ClientConnection::HandleRead cmd_line_bytes : " << cmd_line_bytes;
//if (poly_cmd_queue_.empty() &&      // new style.
//    fetching_cmd_set_.empty()) {    // old style
//  AsyncRead(); // read more data
//  return;
//}

//if (!poly_cmd_queue_.empty()) {
//  for(auto entry : poly_cmd_queue_) {
//    LOG_DEBUG << "new version command created";
//    entry->ForwardRequest(nullptr, up_buf_end_ - up_buf_begin_);   // TODO : 1. 可能重复调用ForwardResponse, 2. 要控制单client的并发数
//  }
//} else {
//  //LOG(VERBOSE) << "fetching_cmd_set_.size() : " << fetching_cmd_set_.size();
//  up_buf_begin_ += cmd_line_bytes;

//  for(auto cmd : fetching_cmd_set_) {
//    // 向 upstream conn 异步写数据
//    //LOG(VERBOSE) << "HandleRead : forward data to upstream conn.";
//    size_t to_write_body_bytes = 0;
//    if (up_buf_begin_ + cmd->body_bytes() <= up_buf_end_) { // 该条数据全部read完毕
//      to_write_body_bytes = cmd->body_bytes();
//    } else {
//      // 该条数据没有read完毕, buf_先全部写出去, 剩下的部分读完再写
//      to_write_body_bytes = up_buf_end_ - up_buf_begin_;
//    }
//  
//    cmd->ForwardRequest(buf_ + up_buf_begin_, to_write_body_bytes);
//  }
//}

  if (timeout_ > 0) {
    // TODO : 改为每个command有一个timer
    timer_.expires_from_now(boost::posix_time::millisec(timeout_));
    // timer_.expires_from_now(std::chrono::milliseconds(timeout_));
    timer_.async_wait(std::bind(&ClientConnection::HandleMemcCommandTimeout, shared_from_this(), std::placeholders::_1));
  }
}

void ClientConnection::HandleTimeoutWrite(const boost::system::error_code& error) {
  if (error) {
    socket_.close();
  }
}

void ClientConnection::HandleMemcCommandTimeout(const boost::system::error_code& error) {
  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      LOG_WARN << "ClientConnection::HandleMemcCommandTimeout timer error : " << error;
    }
    return;
  }
#if 0
  if (current_ready_cmd_) {
    MCE_WARN("error MemcCommandTimeout : 正在返回数据, 不予强行终止" << current_ready_cmd_->cmd_line());
    timer_.expires_from_now(boost::posix_time::millisec(timeout_ / 2));
    timer_.async_wait(std::bind(&ClientConnection::HandleMemcCommandTimeout, shared_from_this(), std::placeholders::_1));
    return;
  }

  if (fetching_cmd_set_.empty()) {
    MCE_WARN("error MemcCommandTimeout but all cmd has finished.");
    return;
  }

  assert(ready_cmd_queue_.empty());

  if (fetching_cmd_set_.size() < mapped_cmd_count_) {
    MCE_WARN("error MemcCommandTimeout : 部分数据返回, 部分没有返回(只可能是get命令)");

    boost::asio::async_write(socket_,
      boost::asio::buffer("END\r\n", 5),
        std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
    return;
  }
#endif 

  std::set<std::shared_ptr<MemcCommand>>::iterator it = fetching_cmd_set_.begin();
  while(it != fetching_cmd_set_.end()) {
    auto cmd = *it;
    fetching_cmd_set_.erase(it++);
    // MCE_WARN << "终止请求 : " << cmd->cmd_line();
    cmd->Abort();
    // LOG_S(WARN) << "ClientConnection::HandleMemcCommandTimeout --> Abort set_upstream_conn, cli:" 
    //            << this << " cmd:" << cmd.operator->() << " upconn:0");
    //delete cmd->upstream_conn();
    //cmd.reset();
  }

  try {
    // static char s[] = "SERVER_ERROR timeout\r\n";
    static char s[] = "xEND\r\n"; // TODO : 标准协议是怎样的呢?
    boost::asio::write(socket_, boost::asio::buffer(s, sizeof(s) - 1));
  } catch(std::exception& e) {
    LOG_WARN << "ClientConnection::HandleMemcCommandTimeout write END err, " << e.what();
  }

  fetching_cmd_set_.clear();
  while(!ready_cmd_queue_.empty()) {
    ready_cmd_queue_.pop();
  }
  current_ready_cmd_.reset();

  socket_.close();
  return;
  char s[] = "SERVER_ERROR timeout\r\n";
  boost::asio::async_write(socket_,
    boost::asio::buffer(s, sizeof(s) - 1),
      std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
}

void ClientConnection::try_free_buffer_space() {
  // TODO : locking ?
  if (buf_lock_ == 0) {
    LOG_INFO << "in try_free_buffer_space(), buf is unlocked, try moving offset, PRE: begin=" << up_buf_begin_
             << " end=" << up_buf_end_ << " parsed=" << parsed_bytes_;
    if (up_buf_begin_ == up_buf_end_) {
      parsed_bytes_ -= up_buf_begin_;
      up_buf_begin_ = up_buf_end_ = 0;
    } else if (up_buf_begin_ > (kBufLength - up_buf_end_)) {
      // TODO : memmove
      memmove(buf_, buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
      parsed_bytes_ -= up_buf_begin_;
      up_buf_end_ -= up_buf_begin_;
      up_buf_begin_ = 0;
    }
    LOG_DEBUG << "in try_free_buffer_space(), buf is unlocked, try moving offset, POST: begin=" << up_buf_begin_
             << " end=" << up_buf_end_ << " parsed=" << parsed_bytes_;
  } else {
    LOG_DEBUG << "in try_free_buffer_space, buf is locked, do nothing";
  }
}
void ClientConnection::update_processed_bytes(size_t processed) {
  up_buf_begin_ += processed;
  try_free_buffer_space();
}

void ClientConnection::recursive_lock_buffer() {
  ++buf_lock_;
}
void ClientConnection::recursive_unlock_buffer() {
  --buf_lock_;
  try_free_buffer_space();
}

void ClientConnection::OnCommandError(std::shared_ptr<MemcCommand> memc_cmd, const boost::system::error_code& error) {
  timer_.cancel();

  // TODO : 如果是最后一个error, 要负责client的收尾工作

  auto it = fetching_cmd_set_.find(memc_cmd);
  if (it != fetching_cmd_set_.end()) {
    fetching_cmd_set_.erase(it);
  }

  if (fetching_cmd_set_.empty()) {
  }

  if (memc_cmd == current_ready_cmd_) {
    current_ready_cmd_.reset();
  }

  UpstreamConn * upstream_conn = memc_cmd->upstream_conn();
  if (!upstream_conn) {
    // LOG_S(WARN) << "OnCommandError error : upstream_conn NULL";
    return;
  }
  upstream_conn->socket().close();
  delete upstream_conn; //socket有出错/关闭, 不回收
  memc_cmd->set_upstream_conn(nullptr);
  // LOG_S(WARN) << "ClientConnection::OnCommandError --> set_upstream_conn " << memc_cmd.operator->()<< " upconn:0";
}

}


