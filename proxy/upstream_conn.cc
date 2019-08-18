#include "upstream_conn.h"

#include "base/logging.h"

const static size_t kMaxConnPerEndpoint = 64;

namespace mcproxy {
// TODO: 更好的容错，以及错误返回信息, 例如
//   客户端格式错误时候，memcached返回错误信息: "CLIENT_ERROR bad data chunk\r\n"

std::atomic_int upstream_conn_count;

UpstreamConn::UpstreamConn(boost::asio::io_service& io_service,
                           const ip::tcp::endpoint& upendpoint,
                           const UpstreamReadCallback& uptream_read_callback,
                           const UpstreamWriteCallback& uptream_write_callback)
//: popped_bytes_(0)
//, pushed_bytes_(0)
//, parsed_bytes_(0)
  : upstream_endpoint_(upendpoint)
  , socket_(io_service) 
  , upstream_read_callback_(uptream_read_callback)
  , uptream_write_callback_(uptream_write_callback)
  , is_reading_more_(false) {
  LOG_DEBUG << "UpstreamConn ctor, upstream_conn_count=" << ++upstream_conn_count;
}

UpstreamConn::~UpstreamConn() {
  LOG_DEBUG << "UpstreamConn dtor, upstream_conn_count=" << --upstream_conn_count;
}

//size_t UpstreamConn::unparsed_bytes() const {
//  LOG_DEBUG << "UpstreamConn::unparsed_bytes pushed="
//              << pushed_bytes_ << " parsed=" << parsed_bytes_;
//  if (pushed_bytes_ > parsed_bytes_) {
//    return pushed_bytes_ - parsed_bytes_;
//  }
//  return 0;
//}

//void UpstreamConn::update_transfered_bytes(size_t transfered) {
//  popped_bytes_ += transfered;
//  if (!is_reading_more_) {
//    // TODO : error checking
//    if (popped_bytes_ == pushed_bytes_) {
//    //LOG_DEBUG << "UpstreamConn::update_transfered_bytes, all data pushed, "
//    //        << " popped_bytes_=" << popped_bytes_ << " parsed=" << parsed_bytes_
//    //        << " parsed-popped=" << parsed_bytes_ - popped_bytes_;
//      parsed_bytes_ -= popped_bytes_;
//      popped_bytes_ = pushed_bytes_ = 0;
//    } else if (popped_bytes_ > (BUFFER_SIZE - pushed_bytes_)) {
//      // TODO : memmove
//      memmove(buf_, buf_ + popped_bytes_, pushed_bytes_ - popped_bytes_);
//      parsed_bytes_ -= popped_bytes_;
//      pushed_bytes_ -= popped_bytes_;
//      popped_bytes_ = 0;
//    }
//  }
//}

  void UpstreamConn::ReadResponse() {
    // socket_.async_read_some(boost::asio::buffer(buf_ + pushed_bytes_, BUFFER_SIZE - pushed_bytes_),
    socket_.async_read_some(boost::asio::buffer(read_buffer_.free_space_begin(), read_buffer_.free_space_size()),
        std::bind(&UpstreamConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
  }

  void UpstreamConn::TryReadMoreData() {
    if (!is_reading_more_  // not reading more
        // && pushed_bytes_ * 3 <  BUFFER_SIZE * 2) {// there is still more than 1/3 buffer space free
        && read_buffer_.has_much_free_space()) {
      is_reading_more_ = true; // memmove cause read data offset drift
      read_buffer_.lock_memmove();

      // socket_.async_read_some(boost::asio::buffer(buf_ + pushed_bytes_, BUFFER_SIZE - pushed_bytes_),
      socket_.async_read_some(boost::asio::buffer(read_buffer_.free_space_begin(), read_buffer_.free_space_size()),
          std::bind(&UpstreamConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
      LOG_DEBUG << "TryReadMoreData";
    } else {
      LOG_DEBUG << "No TryReadMoreData, is_reading_more_=" << is_reading_more_
               << " has_much_free_space=" << read_buffer_.has_much_free_space();
    }
  }

  void UpstreamConn::ForwardRequest(const char* data, size_t bytes, bool has_more_data) {
    if (!socket_.is_open()) {
      LOG_DEBUG << "UpstreamConn::ForwardRequest open socket, req="
                << std::string(data, bytes - 2) << " size=" << bytes
                << " has_more_data=" << has_more_data << " conn=" << this;
      socket_.async_connect(upstream_endpoint_, std::bind(&UpstreamConn::HandleConnect, this, 
          data, bytes, has_more_data, std::placeholders::_1));
      return;
    }

    LOG_DEBUG << "UpstreamConn::ForwardRequest write data,  bytes=" << bytes
              << " has_more_data=" << has_more_data
              << " req_ptr=" << (void*)data
              << " req_data=" << std::string(data, bytes - 2)
              << " conn=" << this;
    async_write(socket_, boost::asio::buffer(data, bytes),
        std::bind(&UpstreamConn::HandleWrite, this, data, bytes, has_more_data,
            std::placeholders::_1, std::placeholders::_2));
  }


  void UpstreamConn::HandleWrite(const char * data, const size_t bytes, bool request_has_more_data,
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
          boost::asio::buffer(data + bytes_transferred, bytes - bytes_transferred),
          std::bind(&UpstreamConn::HandleWrite, this, data + bytes_transferred, request_has_more_data,
            bytes - bytes_transferred,
            std::placeholders::_1, std::placeholders::_2));
      return;
    }

    {
      if (request_has_more_data) {
        LOG_WARN << "UpstreamConn::HandleWrite 转发了当前所有可转发数据, 但还要转发更多来自client的数据.";
        uptream_write_callback_(bytes, error);
      } else {
        LOG_DEBUG << "UpstreamConn::HandleWrite 转发了当前命令的所有数据, 等待 upstream 的响应.";
        // pushed_bytes_ = popped_bytes_ = parsed_bytes_ = 0; // TODO : 这里需要吗？
        read_buffer_.Reset();  // TODO : 这里还需要吗？
      
        // socket_.async_read_some(boost::asio::buffer(buf_, BUFFER_SIZE),
        socket_.async_read_some(boost::asio::buffer(read_buffer_.free_space_begin(), read_buffer_.free_space_size()),
            std::bind(&UpstreamConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
      }
    }
  }

  void UpstreamConn::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
      LOG_WARN << "UpstreamConn::HandleRead upstream read error, upconn=" << this
               << " ep=" << upstream_endpoint_ << " err=" << error.message();
      // socket_.close();
      // TODO : 如何通知给外界?
    } else {
      LOG_DEBUG << "UpstreamConn::HandleRead upstream read ok, bytes_transferred=" << bytes_transferred << " upconn=" << this;
      if (is_reading_more_) {
        is_reading_more_ = false;  // finish reading, you could memmove now
        read_buffer_.unlock_memmove();
      }
      // pushed_bytes_ += bytes_transferred;
      read_buffer_.update_received_bytes(bytes_transferred);
      upstream_read_callback_(error); // TODO : error总是false，所以这个参数应当去掉
    }
    return;

  //if (error) {
  //  LOG_WARN << "HandleRead upstream read error, upconn=" << this
  //           << " ep=" << upstream_endpoint_ << " err=" << error.message();
  //  socket_.close();
  //  // TODO : 如何通知给外界?
  //  return;
  //}

  //LOG_DEBUG << "HandleRead read from upstream ok, bytes=" << bytes_transferred;

  //pushed_bytes_ += bytes_transferred;
    // upstream_read_callback_(buf_, pushed_bytes_, error);
  }

  void UpstreamConn::HandleConnect(const char * data, size_t bytes, bool request_has_more_data, const boost::system::error_code& error) {
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
    
    // ForwardData(data, bytes);
    async_write(socket_, boost::asio::buffer(data, bytes),
        std::bind(&UpstreamConn::HandleWrite, this, data, bytes, request_has_more_data,
            std::placeholders::_1, std::placeholders::_2));
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
      // conn->ResetBuffer();
      conn->read_buffer_.Reset();
      conn_map_[ep].push_back(conn);
      // MCE_DEBUG("conn_pool " << ep << " push. size=" << conn_map_[ep].size());
    }
  }
}

}

