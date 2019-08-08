#include "upstream_conn.h"

const static size_t kMaxConnPerEndpoint = 64;

namespace mcproxy {

UpstreamConn::UpstreamConn(boost::asio::io_service& io_service, const ip::tcp::endpoint& upendpoint) 
  : popped_bytes_(0)
  , pushed_bytes_(0)
  , upstream_endpoint_(upendpoint)
  , socket_(io_service) 
{
}

  void UpstreamConn::ForwardData(const char* buf, size_t bytes) {
    if (!socket_.is_open()) {
      socket_.async_connect(upstream_endpoint_, std::bind(&UpstreamConn::HandleConnect, this, 
          buf, bytes, std::placeholders::_1));
      return;
    }

    //MCE_INFO("MemcCommand::ForwardData --> AsyncWrite cli:" << client_conn_.operator->() << " cmd:" << this);
    async_write(socket_, boost::asio::buffer(buf, bytes),
        std::bind(&UpstreamConn::HandleWrite, this, buf, bytes,
            std::placeholders::_1, std::placeholders::_2));
  }


  void UpstreamConn::HandleWrite(const char * buf, const size_t bytes,
      const boost::system::error_code& error, size_t bytes_transferred)
  {
    if (error) {
      // TODO : 如何通知给command?
      LOG_WARN << "HandleWrite error, upconn=" << this << " ep="
               << upstream_endpoint_ << " err=" << error.message();
      socket_.close();
      return;
    }

    LOG_DEBUG << "HandleWrite ok, upconn=" << this << " ep=" << upstream_endpoint_
              << " " << bytes << " bytes transfered to upstream";

    if (bytes_transferred < bytes) {
      LOG_DEBUG << "HandleWrite 向 upstream 没写完, 继续写.";
      boost::asio::async_write(socket_,
          boost::asio::buffer(buf + bytes_transferred, bytes - bytes_transferred),
          std::bind(&UpstreamConn::HandleWrite, this, buf + bytes_transferred,
            bytes - bytes_transferred,
            std::placeholders::_1, std::placeholders::_2));
    } else {
      LOG_DEBUG << "HandleWrite 转发了当前命令的所有数据, 等待 upstream 的响应.";
      pushed_bytes_ = popped_bytes_ = 0;

      socket_.async_read_some(boost::asio::buffer(buf_, BUFFER_SIZE),
          std::bind(&UpstreamConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
    }
  }

  void UpstreamConn::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
      LOG_WARN << "HandleRead upstream read error, upconn=" << this
               << " ep=" << upstream_endpoint_ << " err=" << error.message();
      socket_.close();
      // TODO : 如何通知给外界?
      return;
    }

    LOG_DEBUG << "HandleRead read from upstream ok, bytes=" << bytes_transferred;

    pushed_bytes_ += bytes_transferred;
  }

  void UpstreamConn::HandleConnect(const char * buf, size_t bytes, const boost::system::error_code& error) {
    if (error) {
      socket_.close();
      // TODO : 如何通知给外界?
      return;
    }
    
    // TODO : socket option 定制
    ip::tcp::no_delay no_delay(true);
    socket_.set_option(no_delay);
    
    socket_base::keep_alive keep_alive(true);
    socket_.set_option(keep_alive);
    
    boost::asio::socket_base::linger linger(true, 0);
    socket_.set_option(linger);
    
    ForwardData(buf, bytes);
  }

UpstreamConn * UpstreamConnPool::Pop(const ip::tcp::endpoint & ep){
  UpstreamConn * conn = 0;

  //std::mutex::scoped_lock(mutex_);
  ConnMap::iterator i = conn_map_.find(ep);
  if ((i != conn_map_.end()) && (!i->second.empty())) {
    conn = i->second.back();
    i->second.pop_back();
    // MCE_DEBUG("conn_pool " << ep << " pop. size=" << conn_map_[ep].size());
  }
  return conn;
}

void UpstreamConnPool::Push(const ip::tcp::endpoint & ep, UpstreamConn * conn) {
  if (conn) {
    //std::mutex::scoped_lock(mutex_);

    ConnMap::iterator i = conn_map_.find(ep);
    if (i != conn_map_.end() && conn_map_.size() >= kMaxConnPerEndpoint){
      conn->socket().close();
      delete conn;
    } else {
      conn->Reset();
      conn_map_[ep].push_back(conn);
      // MCE_DEBUG("conn_pool " << ep << " push. size=" << conn_map_[ep].size());
    }
  }
}

}

