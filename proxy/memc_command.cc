#include "memc_command.h"

#include <vector>
#include <functional>

#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>

#include "base/thread_pool.h"
#include "base/logging.h"

#include "client_conn.h"
#include "upstream_conn.h"

namespace mcproxy {

ForwardResponseCallback WrapOnForwardResponseFinished(size_t to_transfer_bytes, std::weak_ptr<MemcCommand> cmd_wptr);

//存储命令 : <command name> <key> <flags> <exptime> <bytes>\r\n
MemcCommand::MemcCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
    std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len) 
  : cmd_line_(buf, cmd_len)
  , cmd_line_forwarded_(false)
  , forwarded_bytes_(0)
  , body_bytes_(0)
  , is_forwarding_response_(false)
  , missed_ready_(false)
  , missed_popped_bytes_(0)
  , missed_timer_(0)
  , upstream_endpoint_(ep)
  , upstream_conn_(nullptr)
  , client_conn_(owner)
  , io_service_(io_service)
  , loaded_(false)
  , upstream_nomore_data_(false)
{
  std::vector<std::string> strs;
  std::string cmd_line(buf, cmd_len); 
  boost::split(strs, cmd_line, boost::is_any_of(" \r\n"), boost::token_compress_on);
  method_ = strs[0];
  if (method_ == "set") {
    body_bytes_ += size_t(std::stoi(strs[4]));
    body_bytes_ += 2; // 数据后面, 要有一个 "\r\n" 串
  } else if (method_ == "get") {
    strs.erase(strs.begin());
    strs.pop_back();
    missed_keys_.swap(strs);
    // MCE_DEBUG("keys count : " << missed_keys_.size());
  }

  upstream_conn_ = owner->upconn_pool()->Pop(upstream_endpoint_);
};

void MemcCommand::OnUpstreamResponse(const boost::system::error_code& error) {
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

MemcCommand::~MemcCommand() {
  if (upstream_conn_) {
    delete upstream_conn_; // TODO : 需要连接池。暂时直接销毁
  }
  if (missed_timer_) {
    delete missed_timer_;
  }
}

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr);
UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr);

bool MemcCommand::IsFormostCommand() {
  return client_conn_->IsFirstCommand(shared_from_this());
}
/*
void MemcCommand::ForwardRequest(const char * buf, size_t bytes) {
  if (upstream_conn_ == nullptr) {
    // 需要一个上行的 memcache connection
    upstream_conn_ = new UpstreamConn(io_service_, upstream_endpoint_, WrapOnUpstreamResponse(shared_from_this()),
                                        WrapOnUpstreamRequestWritten(shared_from_this()));
    upstream_conn_->socket().async_connect(upstream_endpoint_, std::bind(&MemcCommand::HandleConnect, shared_from_this(), 
        buf, bytes, std::placeholders::_1));
    return;
  }
  
  //MCE_DEBUG(cmd_line_ << " write data to upstream. bytes : " << bytes);
  char * p = const_cast<char *>(buf);
  if (!cmd_line_forwarded()) {
    p -= cmd_line_.size(); // TODO : 检查逻辑, 确保这里确实不会越界
    memcpy(p, cmd_line_.c_str(), cmd_line_.size());
    bytes += cmd_line_.size();
  }

  //MCE_INFO("MemcCommand::ForwardData --> AsyncWrite cli:" << client_conn_.operator->() << " cmd:" << this);
  async_write(upstream_conn_->socket(),
      boost::asio::buffer(p, bytes),
      std::bind(&MemcCommand::HandleWrite, shared_from_this(), p, bytes,
          std::placeholders::_1, std::placeholders::_2));
}
*/

void MemcCommand::Abort() {
  if (upstream_conn_) {
    upstream_conn_->socket().close();
    // MCE_INFO("MemcCommand Abort OK.");
    delete upstream_conn_;
    upstream_conn_ = 0;
  } else {
    // MCE_WARN("MemcCommand Abort NULL upstream_conn_.");
  }
}

/*
void MemcCommand::AsyncRead() {
  //MCE_DEBUG(cmd_line_ << " read buffer size:" << UpstreamConn::BUFFER_SIZE - upstream_conn_->pushed_bytes_);
  upstream_conn_->socket().async_read_some(boost::asio::buffer(upstream_conn_->buf_ + upstream_conn_->pushed_bytes_,
                                          UpstreamConn::BUFFER_SIZE - upstream_conn_->pushed_bytes_),
                  std::bind(&MemcCommand::HandleRead, shared_from_this(),
                      std::placeholders::_1, std::placeholders::_2));
}
*/

/*
void MemcCommand::HandleWrite(const char * buf,
    const size_t bytes, // 命令的当前可写字节数(包含命令行和body)
    const boost::system::error_code& error, size_t bytes_transferred)
{
  if (error) {
    LOG_WARN << "MemcCommand::HandleWrite " << cmd_line_ << " err, upstream="
             << upstream_endpoint_ << " - "  << error << " " << error.message();
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }

  LOG_DEBUG << "MemcCommand::HandleWrite " << cmd_line_ << " bytes written to upstream : " << bytes;

  // forwarded_bytes_ , 是本命令总的已发送字节数
  // bytes , 是本命令本次已发送字节数, 其中"可能"(cmd_line_forwarded_标志)包括cmd_line的长度
  forwarded_bytes_ += bytes_transferred;

  if (bytes_transferred < bytes) {
    LOG_DEBUG << "MemcCommand::HandleWrite " << cmd_line_ << "向 upstream 没写完, 继续写.";
    boost::asio::async_write(upstream_conn_->socket(),
        boost::asio::buffer(buf + bytes_transferred, bytes - bytes_transferred),
        std::bind(&MemcCommand::HandleWrite, shared_from_this(), buf + bytes_transferred, bytes - bytes_transferred,
            std::placeholders::_1, std::placeholders::_2));
    return;
  }

  if (forwarded_bytes_ == total_bytes()) {
    // 转发了当前命令的所有数据, 需要 client_conn_ 调整相应的 buffer offset
    LOG_DEBUG << cmd_line_ << "转发了当前命令的所有数据, 等待 upstream 的响应.";
     
    if(!upstream_conn_){
      LOG_WARN << "MemcCommand::HandleWrite --> null upstream_conn cmd=" << this;
      return;
    }
    upstream_conn_->ResetBuffer();
    // upstream_conn_->pushed_bytes_ = upstream_conn_->popped_bytes_ = 0;
    //MCE_INFO("MemcCommand::HandleWrite --> AsyncRead cli:" << client_conn_.operator->() << " cmd:" << this);
    AsyncRead();
    //upstream_conn_->socket().async_read_some(boost::asio::buffer(upstream_conn_->buf_, UpstreamConn::BUFFER_SIZE),
    //                boost::bind(&MemcCommand::HandleRead, shared_from_this(),
    //                    boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    client_conn_->HandleForwardAllData(shared_from_this(), bytes);
  } else {
    // 转发了当前命令的部分数据, 需要client_conn_ 请求更多client数据
    //MCE_DEBUG( cmd_line_ << "转发了一部分, 继续处理.");
    client_conn_->HandleForwardMoreData(shared_from_this(), bytes);
  }
}
*/

/*
void MemcCommand::HandleConnect(const char * buf, size_t bytes, const boost::system::error_code& error)
{
  if (error) {
    // MCE_WARN("upstream connect error : " << upstream_endpoint_ << " - " << error << " " << error.message());
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }

  // TODO : socket option 定制
  ip::tcp::no_delay no_delay(true);
  upstream_conn_->socket().set_option(no_delay);

  socket_base::keep_alive keep_alive(true);
  upstream_conn_->socket().set_option(keep_alive);

  boost::asio::socket_base::linger linger(true, 0);
  upstream_conn_->socket().set_option(linger);

  //boost::asio::socket_base::receive_buffer_size recv_buf_size;
  //upstream_conn_->socket().get_option(recv_buf_size);

  //boost::asio::socket_base::send_buffer_size send_buf_size;
  //upstream_conn_->socket().get_option(send_buf_size);

  ForwardRequest(buf, bytes);
}
*/

bool MemcCommand::NeedLoadMissed() {
  if (missed_keys_.empty()) {
    return false;
  }

  return boost::starts_with(missed_keys_[0], "FEED#");
}

using namespace nbsdx::concurrent;
class TaskManager {
 static ThreadPool<5> thread_pool_;
public:
 static ThreadPool<5>& Instance() {
   return thread_pool_;
 }
};
ThreadPool<5> TaskManager::thread_pool_;

// TODO : 只是发起load, 应该改一下名字
void MemcCommand::LoadMissedKeys() {
  // MCE_INFO("MemcCommand::LoadMissedKeys --> cli:" << client_conn_.operator->() << " cmd:" << this);
  loaded_ = true;

  // TODO : lamda capture 成员变量的最佳实践?
  std::vector<std::string>& missed_keys = missed_keys_;
  std::shared_ptr<MemcCommand> this_cmd = shared_from_this();
  TaskManager::Instance().AddJob([&missed_keys, this_cmd]() {
        // new LoadMissedTask(missed_keys_, shared_from_this())
        for(std::string key : missed_keys) {
          LOG_INFO << "load missed key " << std::endl;
          // TODO : do real loading
        }
      });
}

void MemcCommand::RemoveMissedKey(const std::string & key) {
  for (size_t i = 0; i < missed_keys_.size(); ++ i) {
    if (missed_keys_[i] == key) {
      missed_keys_.erase(missed_keys_.begin() + i);
      break;
    }
  }
}

// 注意, 该函数会被其他线程调用!
void MemcCommand::DispatchMissedKeyData() {
//missed_ready_ = true; // 这里不需要同步

////MCE_DEBUG(cmd_line_ << " 从 loader 取数据完成");

//missed_timer_ = new boost::asio::deadline_timer(io_service_, boost::posix_time::microsec(1));
//missed_timer_->async_wait(std::bind(&MemcCommand::HandleMissedKeyReady, shared_from_this()));
}

void MemcCommand::HandleMissedKeyReady() {
//// MCE_INFO("MemcCommand::HandleMissedKeyReady --> cli:" << client_conn_.operator->() << " cmd:" << this);
//client_conn_->OnCommandReady(shared_from_this());
}

/*
void MemcCommand::HandleRead(const boost::system::error_code& error, size_t bytes_transferred)
{
  if (error) {
    // MCE_WARN(cmd_line_ << " upstream read error : " << upstream_endpoint_ << " - "  << error << " " << error.message());
    client_conn_->OnCommandError(shared_from_this(), error);
    return;
  }

  // MCE_DEBUG(cmd_line_ << " bytes read from up server : " << bytes_transferred);

  if (!upstream_conn_){
    LOG_WARN << "MemcCommand::HandleRead --> upstream_conn_=" << upstream_conn_ << " cmd=" << this;
    return;
  }
  upstream_conn_->pushed_bytes_ += bytes_transferred;

  client_conn_->OnCommandReady(shared_from_this());
}
*/

void MemcCommand::OnForwardResponseFinished(size_t bytes, const boost::system::error_code& error) {
  if (error) {
    // TODO
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished (" << cmd_line_.substr(0, cmd_line_.size() - 2) << ") error=" << error;
    return;
  }

  upstream_conn_->update_transfered_bytes(bytes);

  if (upstream_nomore_data() && upstream_conn_->to_transfer_bytes() == 0) {
    client_conn_->RotateFirstCommand();
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream_nomore_data, and all data forwarded to client";
  } else {
    LOG_DEBUG << "WriteCommand::OnForwardResponseFinished upstream transfered_bytes=" << bytes
              << " ready_to_transfer_bytes=" << upstream_conn_->to_transfer_bytes();
    is_forwarding_response_ = false;
    if (!upstream_nomore_data()) {
      upstream_conn_->TryReadMoreData(); // 这里必须继续try
    }

    OnForwardResponseReady(); // 可能已经有新读到的数据，因而要尝试转发更多
  }
}

}

