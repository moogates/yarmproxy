#include "backend_conn.h"

#include "base/logging.h"
#include "allocator.h"

const static size_t kMaxConnPerEndpoint = 64;

namespace mcproxy {
// TODO: 更好的容错，以及错误返回信息, 例如
//   客户端格式错误时候，memcached返回错误信息: "CLIENT_ERROR bad data chunk\r\n"

std::atomic_int backend_conn_count;

BackendConn::BackendConn(WorkerContext& context,
                           const ip::tcp::endpoint& upendpoint)
  : context_(context)
  , read_buffer_(new ReadBuffer(context.allocator_->Alloc(), context.allocator_->slab_size()))
  , remote_endpoint_(upendpoint)
  , socket_(context.io_service_) 
  , is_reading_more_(false) {
  LOG_DEBUG << "BackendConn ctor, backend_conn_count=" << ++backend_conn_count;
}

BackendConn::~BackendConn() {
  LOG_DEBUG << "BackendConn dtor, backend_conn_count=" << --backend_conn_count;
  socket_.close();

  context_.allocator_->Release(read_buffer_->data());
  delete read_buffer_;
}

void BackendConn::ReadResponse() {
  // TryReadMoreData();
  // return;
  read_buffer_->inc_recycle_lock();
  socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
      std::bind(&BackendConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
}

void BackendConn::TryReadMoreData() {
  if (!is_reading_more_  // not reading more
      && read_buffer_->has_much_free_space()) {
    is_reading_more_ = true; // memmove cause read data offset drift
    read_buffer_->inc_recycle_lock();

    socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
        std::bind(&BackendConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
    LOG_DEBUG << "TryReadMoreData";
  } else {
    LOG_DEBUG << "No TryReadMoreData, is_reading_more_=" << is_reading_more_
             << " has_much_free_space=" << read_buffer_->has_much_free_space();
  }
}

void BackendConn::ForwardQuery(const char* data, size_t bytes, bool has_more_data) {
  if (!socket_.is_open()) {
    LOG_DEBUG << "ParallelGetCommand BackendConn::ForwardQuery open socket, req="
              << std::string(data, bytes - 2) << " size=" << bytes
              << " has_more_data=" << has_more_data << " backend=" << this;
    socket_.async_connect(remote_endpoint_, std::bind(&BackendConn::HandleConnect, this, 
        data, bytes, has_more_data, std::placeholders::_1));
    return;
  }

  LOG_DEBUG << "ParallelGetCommand BackendConn::ForwardQuery write data, bytes=" << bytes
            << " has_more_data=" << has_more_data
            // << " req_ptr=" << (void*)data
            // << " req_data=" << std::string(data, bytes - 2)
            << " backend=" << this;
  async_write(socket_, boost::asio::buffer(data, bytes),
      std::bind(&BackendConn::HandleWrite, this, data, bytes, has_more_data,
          std::placeholders::_1, std::placeholders::_2));
}


void BackendConn::HandleWrite(const char * data, const size_t bytes, bool request_has_more_data,
    const boost::system::error_code& error, size_t bytes_transferred)
{
  if (error) {
    // TODO : 如何通知给command?
    LOG_DEBUG << "HandleWrite error, upconn=" << this << " ep="
             << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    return;
  }

  LOG_DEBUG << "ParallelGetCommand HandleWrite ok, upconn=" << this << " ep=" << remote_endpoint_
            << " " << bytes << " bytes transfered to backend";

  if (bytes_transferred < bytes) {
    LOG_DEBUG << "HandleWrite 向 backend 没写完, 继续写.";
    boost::asio::async_write(socket_,
        boost::asio::buffer(data + bytes_transferred, bytes - bytes_transferred),
        std::bind(&BackendConn::HandleWrite, this, data + bytes_transferred, request_has_more_data,
          bytes - bytes_transferred,
          std::placeholders::_1, std::placeholders::_2));
    return;
  }

  LOG_DEBUG << "HandleWrite 向 backend 写完, 触发回调.";
  request_sent_callback_(error);
}

void BackendConn::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    LOG_DEBUG << "BackendConn::HandleRead backend read error, upconn=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    // TODO : 如何通知给外界?
  } else {
    LOG_DEBUG << "ParallelGetCommand BackendConn::HandleRead backend read ok, bytes_transferred=" << bytes_transferred << " upconn=" << this;
    is_reading_more_ = false;  // finish reading, you could memmove now

    read_buffer_->update_received_bytes(bytes_transferred);
    read_buffer_->dec_recycle_lock();

    response_received_callback_(error); // TODO : error总是false，所以这个参数应当去掉
  }
}

void BackendConn::HandleConnect(const char * data, size_t bytes, bool request_has_more_data, const boost::system::error_code& error) {
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
  
  async_write(socket_, boost::asio::buffer(data, bytes),
      std::bind(&BackendConn::HandleWrite, this, data, bytes, request_has_more_data,
          std::placeholders::_1, std::placeholders::_2));
}

BackendConn* BackendConnPool::Allocate(const ip::tcp::endpoint & ep){
  {
  //BackendConn* conn = new BackendConn(io_service_, ep);
  //return conn;
  }
  BackendConn* conn;
  auto it = conn_map_.find(ep);
  if ((it != conn_map_.end()) && (!it->second.empty())) {
    conn = it->second.front();
    it->second.pop();
    LOG_DEBUG << "BackendConnPool::Allocate reuse conn, thread=" << std::this_thread::get_id() << " ep=" << ep << ", idles=" << it->second.size();
  } else {
    conn = new BackendConn(context_, ep);
    LOG_DEBUG << "BackendConnPool::Allocate create conn, thread=" << std::this_thread::get_id() << " ep=" << ep;
  }
  active_conns_.insert(std::make_pair(conn, ep));
  return conn;
}

void BackendConnPool::Release(BackendConn * conn) {
  {
  //LOG_DEBUG << "BackendConnPool::Release delete dtor";
  //delete conn;
  //return;
  }

  if (conn == nullptr) {
    return;
  }
  auto ep_it = active_conns_.find(conn);
  if (ep_it == active_conns_.end()) {
    LOG_DEBUG << "BackendConnPool::Release destroyed, thread=" << std::this_thread::get_id() << " conn " << conn << "  not found";
    delete conn;
    return;
  }
  const ip::tcp::endpoint & ep = ep_it->second;

  auto it = conn_map_.find(ep);
  if (it == conn_map_.end()) {
    conn->buffer()->Reset();
    conn_map_[ep].push(conn);
    LOG_DEBUG << "BackendConnPool::Release thread=" << std::this_thread::get_id() << " ep=" << ep << " released, size=1";
  } else {
    if (it->second.size() >= kMaxConnPerEndpoint){
      LOG_DEBUG << "BackendConnPool::Release thread=" << std::this_thread::get_id() << " ep=" << ep << " destroyed, size=" << it->second.size();
      conn->socket().close();
      delete conn;
    } else {
      conn->buffer()->Reset();
      it->second.push(conn);
      LOG_DEBUG << "BackendConnPool::Release thread=" << std::this_thread::get_id() << " ep=" << ep << " released, size=" << it->second.size();
    }
  }
}

}

