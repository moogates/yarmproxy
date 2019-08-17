#include "client_conn.h"

#include <boost/algorithm/string.hpp>

#include <atomic>
#include <memory>
// #include <chrono>

#include "base/logging.h"

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

size_t GetValueBytes(const char * data, const char * end) {
  // "VALUE <key> <flag> <bytes>\r\n"
  const char * p = data + sizeof("VALUE ");
  int count = 0;
  while(p != end) {
    if (*p == ' ') {
      if (++count == 2) {
        return std::stoi(p + 1);
      }
    }
    ++p;
  }
  return 0;
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

std::atomic_int single_get_cmd_count;
class SingleGetCommand : public MemcCommand {
private:
  bool is_forwarding_response_;
public:
  SingleGetCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len)
      : MemcCommand(io_service, ep, owner, buf, cmd_len) 
      , is_forwarding_response_(false)
  {
    LOG_DEBUG << "SingleGetCommand ctor " << ++single_get_cmd_count;
  }

  virtual ~SingleGetCommand() {
    LOG_DEBUG << "SingleGetCommand dtor " << --single_get_cmd_count;
  }

  bool ParseUpstreamResponse() {
    bool valid = true;
    while(upstream_conn_->unparsed_bytes() > 0) {
      const char * entry = upstream_conn_->unparsed_data();
      const char * p = GetLineEnd(entry, upstream_conn_->unparsed_bytes());
      if (p == nullptr) {
        // TODO : no enough data for parsing, please read more
        LOG_DEBUG << "ParseUpstreamResponse no enough data for parsing, please read more"
                  << " data=" << std::string(entry, upstream_conn_->unparsed_bytes())
                  << " bytes=" << upstream_conn_->unparsed_bytes();
        return true;
      }

      if (entry[0] == 'V') {
        // "VALUE <key> <flag> <bytes>\r\n"
        size_t body_bytes = GetValueBytes(entry, p);
        size_t entry_bytes = p - entry + 1 + body_bytes + 2;

        upstream_conn_->update_parsed_bytes(entry_bytes);
        break; // TODO : 每次转发一条，only for test
      } else {
        // "END\r\n"
        if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
          set_upstream_nomore_data();
          upstream_conn_->update_parsed_bytes(sizeof("END\r\n") - 1);
          if (upstream_conn_->unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
            valid = false;
            LOG_WARN << "ParseUpstreamResponse END not really end!";
          } else {
            LOG_INFO << "ParseUpstreamResponse END is really end!";
          }
          break;
        } else {
          LOG_WARN << "ParseUpstreamResponse BAD DATA";
          // TODO : ERROR
          valid = false;
          break;
        }
      }
    }
    return valid;
  }

  virtual bool IsFormostCommand() {
    // SubCommand 检察是否是第一个 command
    return client_conn_->IsFirstCommand(shared_from_this());
  }

  virtual void OnUpstreamResponse(const boost::system::error_code& error) {
    if (error) {
      // MCE_WARN(cmd_line_ << " upstream read error : " << upstream_endpoint_ << " - "  << error << " " << error.message());
      LOG_WARN << "SingleGetCommand OnUpstreamResponse error";
      client_conn_->OnCommandError(shared_from_this(), error);
      return;
    }
    LOG_DEBUG << "SingleGetCommand OnUpstreamResponse data";

    bool valid = ParseUpstreamResponse();
    if (!valid) {
      LOG_WARN << "SingleGetCommand parsing error! valid=false";
      // TODO : error handling
    }
    if (IsFormostCommand()) {
      if (!is_forwarding_response_) {
        is_forwarding_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
        auto cb_wrap = WrapOnForwardResponseFinished(upstream_conn_->to_transfer_bytes(), shared_from_this());
        client_conn_->ForwardResponse(upstream_conn_->to_transfer_data(),
                    upstream_conn_->to_transfer_bytes(), cb_wrap);
        LOG_DEBUG << "SingleGetCommand IsFirstCommand, call ForwardResponse, to_transfer_bytes="
                  << upstream_conn_->to_transfer_bytes();
      } else {
        LOG_WARN << "SingleGetCommand IsFirstCommand, but is forwarding response, don't call ForwardResponse";
      }
    } else {
      // TODO : do nothing, just wait
      LOG_WARN << "SingleGetCommand IsFirstCommand false! to_transfer_bytes="
               << upstream_conn_->to_transfer_bytes();
    }

    if (!upstream_nomore_data()) {
      upstream_conn_->TryReadMoreData(); // upstream 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
    }
  }

  virtual void OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error) {
    if (error) {
      // TODO
      LOG_DEBUG << "OnForwardResponseFinished (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") error=" << error;
      return;
    }

    upstream_conn_->update_transfered_bytes(bytes);

    if (upstream_nomore_data() && upstream_conn_->to_transfer_bytes() == 0) {
      client_conn_->RotateFirstCommand();
      LOG_DEBUG << "OnForwardResponseFinished upstream_nomore_data, and all data forwarded to client";
    } else {
      LOG_DEBUG << "OnForwardResponseFinished upstream transfered_bytes=" << bytes
                << " ready_to_transfer_bytes=" << upstream_conn_->to_transfer_bytes();
      is_forwarding_response_ = false;
      if (!upstream_nomore_data()) {
        upstream_conn_->TryReadMoreData(); // 这里必须继续try
      }

      OnForwardResponseReady(); // 可能已经有新读到的数据，因而要尝试转发更多
    }
  }

  virtual void OnForwardResponseReady() {
    if (upstream_conn_->to_transfer_bytes() == 0) { // TODO : for test only, 正常这里不触发解析, 在收到数据时候触发的解析，会一次解析所有可解析的
       ParseUpstreamResponse();
    }
 
    if (!is_forwarding_response_ && upstream_conn_->to_transfer_bytes() > 0) {
      is_forwarding_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
      auto cb_wrap = WrapOnForwardResponseFinished(upstream_conn_->to_transfer_bytes(), shared_from_this());
      client_conn_->ForwardResponse(upstream_conn_->to_transfer_data(),
                                    upstream_conn_->to_transfer_bytes(),
                                    cb_wrap);
      // LOG_DEBUG << "SingleGetCommand OnForwardResponseReady, data=" << std::string(upstream_conn_->to_transfer_data(), upstream_conn_->to_transfer_bytes() - 2)
      //           << " to_transfer_bytes=" << upstream_conn_->to_transfer_bytes();
    } else {
      LOG_DEBUG << "SingleGetCommand OnForwardResponseReady, upstream no data ready to_transfer, waiting to read more data then write down";
    }
  }

  virtual void ForwardRequest(const char *, size_t) {
    if (upstream_conn_ == nullptr) {
      LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") create upstream conn";
      upstream_conn_ = new UpstreamConn(io_service_, upstream_endpoint_,
                                        WrapOnUpstreamResponse(shared_from_this()),
                                        WrapOnUpstreamRequestWritten(shared_from_this()));
    } else {
      LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") reuse upstream conn";
      upstream_conn_->set_upstream_read_callback(WrapOnUpstreamResponse(shared_from_this()),
                                                 WrapOnUpstreamRequestWritten(shared_from_this()));
    }
    upstream_conn_->ForwardRequest(cmd_line_.data(), cmd_line_.size(), false);
  }

  virtual void OnUpstreamRequestWritten(size_t bytes, const boost::system::error_code& error) {
    // 不需要再通知Client Conn
  }
};

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(boost::asio::io_service& io_service, std::shared_ptr<ClientConnection> owner, const char* cmd_line_buf, size_t cmd_line_size)
    : MemcCommand(io_service,
        MemcachedLocator::Instance().GetEndpointByKey("1"), // FIXME
        owner, cmd_line_buf, cmd_line_size) {
  }
  std::vector<std::shared_ptr<MemcCommand>> subcommands_;

  // TODO : refinement
  bool GroupGetKeys() {
    std::vector<std::string> keys;
    std::string key_list = cmd_line().substr(4, cmd_line().size() - 6);
    boost::split(keys, key_list, boost::is_any_of(" "), boost::token_compress_on);

    std::map<ip::tcp::endpoint, std::string> cmd_line_map;
    
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i].empty()) {
        continue;
      }
      ip::tcp::endpoint ep = MemcachedLocator::Instance().GetEndpointByKey(keys[i].c_str(), keys[i].size());

      auto it = cmd_line_map.find(ep);
      if (it == cmd_line_map.end()) {
        it = cmd_line_map.insert(make_pair(ep, std::string("get"))).first;
      }
      it->second += ' ';
      it->second += keys[i];
    }

    for (auto it = cmd_line_map.begin(); it != cmd_line_map.end(); ++it) {
      LOG_DEBUG << "GroupGetKeys " << it->first << " get_keys=" << it->second;
      it->second += "\r\n";
      std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(io_service_, it->first, client_conn_, it->second.c_str(), it->second.size()));
      // fetching_cmd_set_.insert(cmd);
      subcommands_.push_back(cmd);
    }
    return true;
  }
};

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

int ClientConnection::MapMemcCommand(char * buf, size_t len) {
  const char * p = GetLineEnd(buf, len);
  if (p == nullptr) {
    return 0;
  }

  size_t cmd_len = p - buf + 1; // 请求的命令行长度
  if(cmd_len < 5) {
    LOG_WARN << "unknown command(" << std::string(buf, p - buf)
             << ") len=" << cmd_len << " conn=" << this;
    return -1;
  }

  if(strncmp(buf, "get ", 4) == 0) {
#ifdef DONT_USE_MAP
    std::shared_ptr<MemcCommand> cmd(new MemcCommand(io_service_,
            MemcachedLocator::Instance().GetEndpointByKey("1"),
            shared_from_this(), buf, cmd_len));
    fetching_cmd_set_.insert(cmd);
    return cmd_len;
#endif

    std::vector<std::string> keys;
    std::string key_list(buf + 4, cmd_len - 6);
    boost::split(keys, key_list, boost::is_any_of(" "), boost::token_compress_on);

    // memcached address -> cmd line 

    std::map<ip::tcp::endpoint, std::string> cmd_line_map;
    
    // CmdLineMap::iterator it;
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i].empty()) {
        continue;
      }
      ip::tcp::endpoint ep = MemcachedLocator::Instance().GetEndpointByKey(keys[i].c_str(), keys[i].size());

      auto it = cmd_line_map.find(ep);
      if (it == cmd_line_map.end()) {
        it = cmd_line_map.insert(make_pair(ep, std::string("get"))).first;
      }
      it->second += ' ';
      it->second += keys[i];
    }

    for (auto it = cmd_line_map.begin(); it != cmd_line_map.end(); ++it) {
      //MCE_DEBUG(it->first << " cmd_line : " << it->second);
      it->second += "\r\n";
      std::shared_ptr<MemcCommand> cmd(new MemcCommand(io_service_, it->first, shared_from_this(), it->second.c_str(), it->second.size()));
      fetching_cmd_set_.insert(cmd);
    }
  } else if(strncmp(buf, "quit\r\n", 6) == 0) {
    // LOG_WARN << "退出命令.";
    return -1;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0 || strncmp(buf, "replace ", sizeof("replace ") - 1) == 0) {
    //存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
    char * q = buf, * r = buf; 
    while(q < p) {
      if(*(++q) == ' ') {
        if (*r == ' ') {
          break;
        } else {
          r = q;
        }
      }
    }

    std::shared_ptr<MemcCommand> cmd(new MemcCommand(io_service_,
              MemcachedLocator::Instance().GetEndpointByKey(r + 1, q - r - 1),
              shared_from_this(), buf, cmd_len));
    fetching_cmd_set_.insert(cmd);
  } else {
    LOG_WARN << "收到不支持的命令" << fetching_cmd_set_.size();
    return -1;
  }

  mapped_cmd_count_ = fetching_cmd_set_.size();
  return cmd_len;
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
    if (cmd_need_more_data_->upcoming_bytes() > (up_buf_end_ - up_buf_begin_)) {
      update_processed_bytes(up_buf_end_ - up_buf_begin_);
      LOG_INFO << "ClientConnection::HandleRead.ForwardRequest : up_buf_begin_=" << up_buf_begin_
               << " bytes=" << (up_buf_end_ - up_buf_begin_) << ". HAS MORE DATA. conn=" << this;
      cmd_need_more_data_->ForwardRequest(buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);  
      // 尚未转发cmd_need_more_data_的全部数据
      return;
    } else {
      LOG_INFO << "ClientConnection::HandleRead.ForwardRequest : up_buf_begin_=" << up_buf_begin_
               << " bytes=" << cmd_need_more_data_->upcoming_bytes() << ". NO MORE DATA. conn=" << this;
      cmd_need_more_data_->ForwardRequest(buf_ + up_buf_begin_, cmd_need_more_data_->upcoming_bytes());  
      update_processed_bytes(cmd_need_more_data_->upcoming_bytes());
      cmd_need_more_data_.reset();
      // 终于转发cmd_need_more_data_的全部数据,  继续处理其他命令
    }
  }

  size_t cmd_line_bytes = 0;
  bool is_new_version = false;

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

  std::string timeout_lines;
  std::set<std::shared_ptr<MemcCommand>>::iterator it = fetching_cmd_set_.begin();
  while(it != fetching_cmd_set_.end()) {
    auto cmd = *it;
    fetching_cmd_set_.erase(it++);

    timeout_lines += cmd->cmd_line();
    // MCE_WARN << "终止请求 : " << cmd->cmd_line();
    cmd->Abort();
    // LOG_S(WARN) << "ClientConnection::HandleMemcCommandTimeout --> Abort set_upstream_conn, cli:" 
    //            << this << " cmd:" << cmd.operator->() << " upconn:0");
    //delete cmd->upstream_conn();
    //cmd.reset();
  }

  LOG_WARN << "MemcCommandTimeout, 终止未返回的请求 : " << timeout_lines;

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
    LOG_INFO << "in try_free_buffer_space(), buf is unlocked, try moving offset, POST: begin=" << up_buf_begin_
             << " end=" << up_buf_end_ << " parsed=" << parsed_bytes_;
  } else {
    LOG_WARN << "in try_free_buffer_space, buf is locked, do nothing";
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

// 这两个函数, 可以合二为一的
void ClientConnection::HandleForwardAllData(std::shared_ptr<MemcCommand> cmd, size_t bytes) {
  // 当前命令所有数据都转发
  up_buf_begin_ += bytes;

  if (!cmd->cmd_line_forwarded()) {
    up_buf_begin_ -= cmd->cmd_line_bytes();
    cmd->set_cmd_line_forwarded(true);
  }

  if (up_buf_begin_ > up_buf_end_) {
    // LOG_S(WARN) << "转发越界!";
  } else if (up_buf_begin_ == up_buf_end_) {
    up_buf_begin_ = up_buf_end_ = 0;
  } else {
  //LOG_S(WARN) << "处理完当前命令, client buffer 中仍然有命令 : " << up_buf_begin_ 
  //     << ":" << up_buf_end_; // 处理完当前命令, client buffer 中仍然有命令. 暂时不支持此种情况

    //up_buf_begin_ = up_buf_end_ = 0;
    memmove(buf_, buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
    up_buf_end_ = up_buf_end_ - up_buf_begin_;
    up_buf_begin_ = 0;
  }
}

void ClientConnection::HandleForwardMoreData(std::shared_ptr<MemcCommand> cmd, size_t bytes) {
  up_buf_begin_ += bytes;

  if (!cmd->cmd_line_forwarded()) {
    up_buf_begin_ -= cmd->cmd_line_bytes();
    cmd->set_cmd_line_forwarded(true);
  }

  if (up_buf_begin_ > up_buf_end_) {
    //LOG(WARNING) << "转发越界!";
  } else if (up_buf_begin_ == up_buf_end_) {
    //LOG(VERBOSE) << "转发所有数据!";
    up_buf_begin_ = up_buf_end_ = 0;

    if (cmd->forwarded_bytes() < cmd->total_bytes()) {
      //LOG(VERBOSE) << "  半条数据";
    }
  } else {
    // 未解析并写到 upstream conn 的内容, 挪到 buffer 开头
    //LOG(VERBOSE) << " --------- 未解析并写出到 upstream 的内容. size = " << up_buf_end_ - up_buf_begin_;
    memmove(buf_, buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
    up_buf_end_ = up_buf_end_ - up_buf_begin_;
    up_buf_begin_ = 0;
  }

  AsyncRead();
}

void ClientConnection::AsyncWrite() {
  UpstreamConn * upstream_conn = current_ready_cmd_->upstream_conn();
  if (!upstream_conn) {
    LOG_WARN << "AsyncWrite error : upstream_conn NULL";
    return;
  }
  boost::asio::async_write(socket_,
    boost::asio::buffer(upstream_conn->buf_ + upstream_conn->popped_bytes_,
                       upstream_conn->pushed_bytes_ - upstream_conn->popped_bytes_ - response_status_.unparsed_bytes),
    std::bind(&ClientConnection::HandleWrite, shared_from_this(),
      std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::AsyncWriteMissed() {
  std::shared_ptr<MemcCommand> p = current_ready_cmd_;

  //MCE_DEBUG("开始写回 missed key data");

  if (p->missed_buf().empty()) {
    //MCE_DEBUG("missed key data 为空");
  }

  boost::asio::async_write(socket_,
    boost::asio::buffer(p->missed_buf().c_str() + p->missed_popped_bytes(),
                        p->missed_buf().size() - p->missed_popped_bytes()),
    std::bind(&ClientConnection::HandleWriteMissed, shared_from_this(),
      std::placeholders::_1, std::placeholders::_2));
}

void ClientConnection::HandleWriteMissed(const boost::system::error_code& error, size_t bytes_transferred)
{
  if (!current_ready_cmd_) {
    // LOG_S(WARN) << "HandleWriteMissed error current_ready_cmd_ NULL";
    return;
  }

  std::shared_ptr<MemcCommand> cmd = current_ready_cmd_;

  if (error) {
    // LOG_S(WARN) << "HandleWriteMissed error : " << cmd->cmd_line() << " " << error.message();
    socket_.close();
    current_ready_cmd_.reset();
    return;
  }

  //MCE_DEBUG("完成写回 missed key data bytes : " << bytes_transferred);
  cmd->set_missed_popped_bytes(cmd->missed_popped_bytes() + bytes_transferred);
  if (cmd->missed_popped_bytes() < cmd->missed_buf().size()) {
    AsyncWriteMissed();
  } else {
    // TODO : 清理
    fetching_cmd_set_.erase(current_ready_cmd_);
    current_ready_cmd_.reset();

    // 处理下一个 ready command
    if (!ready_cmd_queue_.empty()) {
      auto cmd = ready_cmd_queue_.front();
      ready_cmd_queue_.pop();
      OnCommandReady(cmd);
    } else {
      if (fetching_cmd_set_.empty()) {
        // 处理完所有ready command, 开始处理 client 的下一个请求
        // LOG_S(INFO) "HandleWriteMissed get next cmd: ";
        AsyncRead();
      } else {
        // 需要继续等其他未结束的command
      }
    }
  }
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

void ClientConnection::OnCommandReady(std::shared_ptr<MemcCommand> memc_cmd) {
  //MCE_INFO("ClientConnection::OnCommandReady --> " << memc_cmd->cmd_line());
  if (current_ready_cmd_ && current_ready_cmd_ != memc_cmd) {
    ready_cmd_queue_.push(memc_cmd); // 等处理完 current_ready_cmd_, 再处理
    return;
  }

  current_ready_cmd_ = memc_cmd;
  
  if (memc_cmd->method() == "get") { // 需要 Reduce 的命令
    // 从 Loader 加载数据完毕
    if (memc_cmd->missed_ready()) {
      if (fetching_cmd_set_.size() == 1) { // 最后一个, 要加 "END\r\n"
        //MCE_DEBUG(memc_cmd->cmd_line() << "最后一个missed 写回");
        memc_cmd->missed_buf() += "END\r\n";
      }
      //MCE_INFO("ClientConnection::OnCommandReady --> " << memc_cmd->cmd_line() << " write end");
      AsyncWriteMissed();
      return;
    }
    
    UpstreamConn * conn = memc_cmd->upstream_conn();
    if (!conn) {
      // LOG_S(WARN) << "OnCommandReady error : upstream_conn NULL";
      return;
    }

    // 有数据读过来, ReduceMemcGetCommand(memc_cmd)
    size_t start_offset = conn->popped_bytes_ + response_status_.left_bytes;

    if (start_offset > conn->pushed_bytes_) {
      //MCE_DEBUG("当前value仍然没完");
      response_status_.unparsed_bytes = 0;
      response_status_.left_bytes -= conn->pushed_bytes_ - conn->popped_bytes_;
      AsyncWrite();
      return;
    }

    for(;;) {
      const char * p = GetLineEnd(conn->buf_ + start_offset, conn->pushed_bytes_ - start_offset);

      if (!p) {
        //LOG(VERBOSE) <<  "buffer 结尾数据暂时无法解析 : ";
        //LOG(VERBOSE).write(conn->buf_ + start_offset, conn->pushed_bytes_ - start_offset);
        //LOG(VERBOSE) << conn->pushed_bytes_ - start_offset;

        // TODO : read for more data
        response_status_.complete = false;
        response_status_.unparsed_bytes = conn->pushed_bytes_ - start_offset;
        response_status_.left_bytes = 0;
        AsyncWrite();
        return;
      }

      std::string status_line(conn->buf_ + start_offset, p - (conn->buf_ + start_offset) - 1);
      std::vector<std::string> strs;
      boost::split(strs, status_line, boost::is_any_of(" "), boost::token_compress_on);
      status_line += "\r\n";

      size_t body_bytes = 0;
      if (strs.size() == 4) { // VALUE <key> <flag> <bytes>
        body_bytes = std::stoi(strs[3]);
        //MCE_DEBUG(memc_cmd->cmd_line() << " 成功从memc 获取key " << strs[1] << " bytes=" << body_bytes);
        //MCE_INFO("ClientConnection::OnCommandReady --> " << memc_cmd->cmd_line() << " get " << strs[1]);
        memc_cmd->RemoveMissedKey(strs[1]); // 该key已经获取
      } else { // "END \r\n", or error
        //MCE_DEBUG("memcached get 数据完毕 : " << status_line << " missed key count : " 
        //    << memc_cmd->NeedLoadMissed());
        response_status_.complete = true;
        if ((fetching_cmd_set_.size() > 1) || memc_cmd->NeedLoadMissed()) {
          response_status_.unparsed_bytes = status_line.size(); // 最后一行, 不直接写回
        } else {
          response_status_.unparsed_bytes = 0; //最后一个get命令的最后一行, 直接写回
        }
        response_status_.left_bytes = 0;
        //MCE_INFO("ClientConnection::OnCommandReady --> " << memc_cmd->cmd_line() << " get and write end");
        AsyncWrite();
        return;
      }

      size_t end_offset = start_offset + status_line.size() + body_bytes + 2;
      if (end_offset  >= conn->pushed_bytes_) {
        //LOG(VERBOSE) <<  "处理buffer 中所有数据. 读取更多.";
        response_status_.complete = false;
        response_status_.unparsed_bytes = 0;
        response_status_.left_bytes = end_offset - conn->pushed_bytes_;
        AsyncWrite();
        return;
      }
      start_offset = end_offset;

      ++ p;
    }
  } else if (memc_cmd->method() == "set" || memc_cmd->method() == "add" || memc_cmd->method() == "replace") {
    UpstreamConn * conn = memc_cmd->upstream_conn();
    if (!conn) {
      // LOG_S(WARN) << "OnCommandReady error : upstream_conn NULL";
      return;
    }
    if(conn->buf_[conn->pushed_bytes_ - 1] == '\n') {
      response_status_.complete = true;
    }
    AsyncWrite();
  }
}


void ClientConnection::HandleWrite(const boost::system::error_code& error, size_t bytes_transferred)
{
  if (error) {
    // LOG_S(WARN) << "HandleWrite error : " << error << " " << error.message();
    timer_.cancel();
    socket_.close();
    return;
  }

  if (!current_ready_cmd_) {
    // MCE_WARN("HandleWrite error : current_ready_cmd_ NULL");
    return;
  }
  //MCE_DEBUG("写回到client. bytes=" << bytes_transferred);
  UpstreamConn * conn = current_ready_cmd_->upstream_conn();
  if (!conn) {
    // LOG_S(WARN) << "HandleWrite error : upstream_conn NULL";
    return;
  }

  conn->popped_bytes_ += bytes_transferred;

  if (conn->popped_bytes_ == conn->pushed_bytes_ - response_status_.unparsed_bytes) {
    //MCE_DEBUG("全部写回到client. complete = " << response_status_.complete);

    if (!response_status_.complete) {
      //LOG(VERBOSE) << "需要继续读upstream conn";
      memmove(conn->buf_, conn->buf_ + conn->pushed_bytes_ - response_status_.unparsed_bytes, response_status_.unparsed_bytes);
      conn->popped_bytes_ = 0;
      conn->pushed_bytes_ = response_status_.unparsed_bytes;
      //MCE_INFO("ClientConnection::HandleWrite --> AsyncRead cli:" << this << " cmd:" << current_ready_cmd_.operator->());
      current_ready_cmd_->AsyncRead();
      return; 
    }
    // current_ready_cmd_所有数据处理完毕, 清理/回收 
    response_status_.Reset();

    upconn_pool_->Push(current_ready_cmd_->upstream_endpoint(), conn);
    current_ready_cmd_->set_upstream_conn(0);
    //MCE_INFO("ClientConnection::HandleWrite --> set_upstream_conn cli:"
    //<< this << " cmd:" << current_ready_cmd_.operator->() << " upconn:0");
    //MCE_INFO("ClientConnection::HandleWrite --> " << current_ready_cmd_->cmd_line() << " clear upstream");

    if (current_ready_cmd_->NeedLoadMissed()) {
      //MCE_INFO("ClientConnection::HandleWrite --> " << current_ready_cmd_->cmd_line() << " load from db");
      current_ready_cmd_->LoadMissedKeys();
    } else {
      fetching_cmd_set_.erase(current_ready_cmd_);
    }
    current_ready_cmd_.reset();

    // 处理下一个 ready command
    if (!ready_cmd_queue_.empty()) {
      auto cmd = ready_cmd_queue_.front();
      ready_cmd_queue_.pop();
      OnCommandReady(cmd);
    } else {
      if (fetching_cmd_set_.empty()) {
        // 处理完所有ready command, 开始处理 client 的下一个请求
        AsyncRead();
      } else {
        // 需要继续等其他未结束的command
      }
    }
  } else {
    //LOG(VERBOSE) << "未全部写回到client";
    AsyncWrite(); // 当前command还没写完，继续写
  }
}

}


