#include "client_conn.h"

#include <boost/algorithm/string.hpp>

#include <atomic>
#include <memory>

#include "base/logging.h"

#include "memc_command.h"
#include "memcached_locator.h"
#include "upstream_conn.h"

using namespace boost::asio;

namespace mcproxy {

std::atomic_int g_cc_count;

// 指向行位的'\n'字符
static const char * GetLineEnd(const char * buf, size_t len) {
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
  , up_buf_begin_(0)
  , up_buf_end_(0)
  , upconn_pool_(pool)
  //, current_ready_cmd_(NULL)
  , timeout_(60)
  , timer_(io_service)
{
  LOG_INFO << "ClientConnection destroyed." << ++g_cc_count;
}

ClientConnection::~ClientConnection() {
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

void ClientConnection::AsyncRead()
{
  timer_.cancel();

  socket_.async_read_some(boost::asio::buffer(up_buf_ + up_buf_end_, kBufLength - up_buf_end_),
      std::bind(&ClientConnection::HandleRead, shared_from_this(),
          std::placeholders::_1, // 占位符
          std::placeholders::_2));
}

class SingleGetCommand : public MemcCommand {
public:
  SingleGetCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len)
      : MemcCommand(io_service, ep, owner, buf, cmd_len) 
  {
  }

  virtual void ForwardData(const char *, size_t) {
    if (upstream_conn_ == nullptr) {
      LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") create upstream conn";
      // 需要一个上行的 memcache connection
      upstream_conn_ = new UpstreamConn(io_service_, upstream_endpoint_);
      upstream_conn_->socket().async_connect(upstream_endpoint_, std::bind(&MemcCommand::HandleConnect, shared_from_this(), 
          cmd_line_.c_str(), cmd_line_.size(), std::placeholders::_1));
      return;
    }

    LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") write data to upstream";
    
    async_write(upstream_conn_->socket(),
        // boost::asio::buffer(buf, bytes),
        // std::bind(&MemcCommand::HandleWrite, shared_from_this(), buf, bytes,
        boost::asio::buffer(cmd_line_.c_str(), cmd_line_.size()),
        std::bind(&MemcCommand::HandleWrite, shared_from_this(), cmd_line_.c_str(), cmd_line_.size(),
            std::placeholders::_1, std::placeholders::_2));
  }

  virtual void ForwardData2(const char *, size_t) {
    if (upstream_conn_ == nullptr) {
      LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") create upstream conn";
      // 需要一个上行的 memcache connection
      upstream_conn_ = new UpstreamConn(io_service_, upstream_endpoint_);
      return;
    }
    upstream_conn_->ForwardRequest(cmd_line_.c_str(), cmd_line_.size());


    LOG_DEBUG << "SingleGetCommand (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") write data to upstream";
    
    async_write(upstream_conn_->socket(),
        // boost::asio::buffer(buf, bytes),
        // std::bind(&MemcCommand::HandleWrite, shared_from_this(), buf, bytes,
        boost::asio::buffer(cmd_line_.c_str(), cmd_line_.size()),
        std::bind(&MemcCommand::HandleWrite, shared_from_this(), cmd_line_.c_str(), cmd_line_.size(),
            std::placeholders::_1, std::placeholders::_2));
  }
};

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(boost::asio::io_service& io_service, std::shared_ptr<ClientConnection> owner, const char* buf, size_t size)
    : MemcCommand(io_service,
        MemcachedLocator::Instance().GetEndpointByKey("1"), // FIXME
        owner, buf, size) {
  }
  std::vector<std::shared_ptr<MemcCommand>> single_get_commands_;

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
      single_get_commands_.push_back(cmd);
    }
    return true;
  }

  virtual void ForwardData(const char *, size_t) {
    for(auto cmd : single_get_commands_) {
      cmd->ForwardData(nullptr, 0);
    }
  }
};

int MemcCommand::CreateCommand(boost::asio::io_service& asio_service,
          std::shared_ptr<ClientConnection> owner, const char* buf, size_t size, std::shared_ptr<MemcCommand>* cmd) {

  const char * p = GetLineEnd(buf, size);
  if (p == nullptr) {
    *cmd = nullptr;
    return 0;
  }

  size_t cmd_len = p - buf + 1; // 请求 命令行 长度
  if(cmd_len < 5) {
    LOG_WARN << "MapMemcCommand unknown command(" << std::string(buf, p - buf -1)
             << ") len=" << cmd_len << " client_conn=" << owner.get();
    return -1;
  }

#define DONT_USE_MAP 1
  if(strncmp(buf, "get ", 4) == 0) {
#if DONT_USE_MAP
    (*cmd).reset(new SingleGetCommand(asio_service,
                     MemcachedLocator::Instance().GetEndpointByKey("1"),
                     owner, buf, cmd_len));
    return cmd_len;
#endif

    ParallelGetCommand* parallel_cmd = new ParallelGetCommand(asio_service, owner, buf, cmd_len);
    parallel_cmd ->GroupGetKeys();
    (*cmd).reset(parallel_cmd);
    return cmd_len;
  }
  return 0;
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
    //LOG(WARNING) << "退出命令.";
    return -1;
  } else if (strncmp(buf, "set ", 4) == 0 || strncmp(buf, "add ", 4) == 0 || strncmp(buf, "replace ", 4) == 0) {
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
    //LOG(WARNING) << "收到不支持的命令" << fetching_cmd_set_.size();
    return -1;
  }

  mapped_cmd_count_ = fetching_cmd_set_.size();
  return cmd_len;
}

void ClientConnection::HandleRead(const boost::system::error_code& error, size_t bytes_transferred)
{
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

  // 一次只处理一组命令! 这个条件非常关键.
  int cmd_line_bytes = 0;
  std::shared_ptr<MemcCommand> child_cmd;
  LOG_DEBUG << "ClientConnection::HandleRead fetching_cmd_set_.size() : " << fetching_cmd_set_.size();
  if (fetching_cmd_set_.empty()) {
    cmd_line_bytes = MemcCommand::CreateCommand(io_service_,
          shared_from_this(), (const char*)(up_buf_ + up_buf_begin_), up_buf_end_ - up_buf_begin_, &child_cmd);
    if (cmd_line_bytes == 0) {
      cmd_line_bytes = MapMemcCommand(up_buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
    }
  }

  if (cmd_line_bytes < 0) {
    LOG_WARN << "ClientConnection::HandleRead bad request";
    socket_.close();
    return;
  }
  LOG_DEBUG << "ClientConnection::HandleRead cmd_line_bytes : " << cmd_line_bytes;
  if (child_cmd == nullptr &&               // new style
      fetching_cmd_set_.empty()) {    // old style
    AsyncRead(); // read more data
    return;
  }

  if (child_cmd != nullptr) {
    LOG_DEBUG << "child command created";
    child_cmd->ForwardData(nullptr, 0);
  } else {
    //LOG(VERBOSE) << "fetching_cmd_set_.size() : " << fetching_cmd_set_.size();
    up_buf_begin_ += cmd_line_bytes;
    
    for(auto cmd : fetching_cmd_set_) {
      // 向 upstream conn 异步写数据
      //LOG(VERBOSE) << "HandleRead : forward data to upstream conn.";
      size_t to_write_body_bytes = 0;
      if (up_buf_begin_ + cmd->body_bytes() <= up_buf_end_) { // 该条数据全部read完毕
        to_write_body_bytes = cmd->body_bytes();
      } else {
        // 该条数据没有read完毕, up_buf_先全部写出去, 剩下的部分读完再写
        to_write_body_bytes = up_buf_end_ - up_buf_begin_;
      }
    
      cmd->ForwardData(up_buf_ + up_buf_begin_, to_write_body_bytes);
    }
  }

  if (timeout_ > 0) {
    timer_.expires_from_now(boost::posix_time::millisec(timeout_));
    //timer_.expires_from_now(boost::posix_time::microsec(timeout_));
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
      // LOG_S(WARN) << "timer error : " << error;
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

  // LOG_S(WARN) << "MemcCommandTimeout, 终止未返回的请求 : " << timeout_lines;

  try{
    boost::asio::write(socket_, boost::asio::buffer("END\r\n", 5));
  }catch(std::exception& e){
    // LOG_S(WARN) << "ClientConnection::HandleMemcCommandTimeout --> write end err, " << e.what();
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
    boost::asio::buffer(s, strlen(s)),
      std::bind(&ClientConnection::HandleTimeoutWrite, shared_from_this(), std::placeholders::_1));
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
    memmove(up_buf_, up_buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
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
    memmove(up_buf_, up_buf_ + up_buf_begin_, up_buf_end_ - up_buf_begin_);
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


