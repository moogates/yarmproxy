#include "backend_conn.h"

#include "logging.h"

#include "allocator.h"
#include "read_buffer.h"
#include "worker_pool.h"
#include "error_code.h"

const static size_t kMaxConnPerEndpoint = 64;

namespace yarmproxy {
// TODO: 更好的容错，以及错误返回信息, 例如
//   客户端格式错误时候，memcached返回错误信息: "CLIENT_ERROR bad data chunk\r\n"

std::atomic_int backend_conn_count;

BackendConn::BackendConn(WorkerContext& context,
                           const ip::tcp::endpoint& upendpoint)
  : context_(context)
  , read_buffer_(new ReadBuffer(context.allocator_->Alloc(), context.allocator_->slab_size()))
  , remote_endpoint_(upendpoint)
  , socket_(context.io_service_)
  , is_reading_more_(false)
  , reply_complete_(false)
  , no_recycle_(false) {
  LOG_DEBUG << "BackendConn ctor, backend_conn_count=" << ++backend_conn_count;
}

BackendConn::~BackendConn() {
  LOG_DEBUG << "BackendConn dtor, backend_conn_count=" << --backend_conn_count;
  socket_.close();

  context_.allocator_->Release(read_buffer_->data());
  delete read_buffer_;
}

void BackendConn::Close() {
  socket_.close();
}

void BackendConn::SetReplyData(const char* data, size_t bytes) {
  assert(is_reading_more_ == false);
  // reply_complete_  = true;
  buffer()->Reset();
  buffer()->push_reply_data(data, bytes);
}

void BackendConn::Reset() {
  is_reading_more_ = false;
  reply_complete_  = false;
  buffer()->Reset();
}

void BackendConn::ReadReply() {
  // TryReadMoreReply();
  // return;
  read_buffer_->inc_recycle_lock();
  socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
      std::bind(&BackendConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
}

void BackendConn::TryReadMoreReply() {
  if (reply_complete_) {
    LOG_DEBUG << "TryReadMoreReply reply_complete_=true, do nothing, backend=" << this;
    return;
  }
  if (!is_reading_more_  // not reading more
      && read_buffer_->has_much_free_space()) {
    is_reading_more_ = true; // memmove cause read data offset drift
    read_buffer_->inc_recycle_lock();

    socket_.async_read_some(boost::asio::buffer(read_buffer_->free_space_begin(), read_buffer_->free_space_size()),
        std::bind(&BackendConn::HandleRead, this, std::placeholders::_1, std::placeholders::_2));
    LOG_DEBUG << "TryReadMoreReply async_read_some, backend=" << this;
  } else {
    LOG_DEBUG << "TryReadMoreReply do nothing, is_reading_more_=" << is_reading_more_
             << " has_much_free_space=" << read_buffer_->has_much_free_space()
             << " backend=" << this;;
  }
}

void BackendConn::ForwardQuery(const char* data, size_t bytes, bool has_more_data) {
  if (!socket_.is_open()) {
    LOG_DEBUG << "BackendConn::ForwardQuery open socket, req="
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


void BackendConn::HandleWrite(const char * data, const size_t bytes, bool query_has_more_data,
    const boost::system::error_code& error, size_t bytes_transferred)
{
  if (error) {
    // TODO : 如何通知给command?
    LOG_DEBUG << "BackendConn::HandleWrite error, backend=" << this << " ep="
             << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    query_sent_callback_(ErrorCode::E_WRITE_QUERY); // TODO : check error
    return;
  }

  LOG_DEBUG << "BackendConn::HandleWrite ok, backend=" << this << " ep=" << remote_endpoint_
            << " " << bytes << " bytes transfered to backend";

  if (bytes_transferred < bytes) {
    LOG_DEBUG << "HandleWrite 向 backend 没写完, 继续写.";
    boost::asio::async_write(socket_,
        boost::asio::buffer(data + bytes_transferred, bytes - bytes_transferred),
        std::bind(&BackendConn::HandleWrite, this, data + bytes_transferred, query_has_more_data,
          bytes - bytes_transferred,
          std::placeholders::_1, std::placeholders::_2));
    return;
  }

  LOG_DEBUG << "HandleWrite 向 backend 写完, 触发回调.";
  // query_sent_callback_(error);

  query_sent_callback_(ErrorCode::E_SUCCESS); // TODO : check error
}

void BackendConn::HandleRead(const boost::system::error_code& error, size_t bytes_transferred) {
  if (error) {
    LOG_DEBUG << "BackendConn::HandleRead read error, backend=" << this
             << " ep=" << remote_endpoint_ << " err=" << error.message();
    socket_.close();
    // TODO : 如何通知给外界?
    reply_received_callback_(ErrorCode::E_READ_REPLY); // TODO : error type
  } else {
    LOG_DEBUG << "BackendConn::HandleRead read ok, bytes_transferred=" << bytes_transferred << " backend=" << this;
    is_reading_more_ = false;  // finish reading, you could memmove now

    read_buffer_->update_received_bytes(bytes_transferred);
    read_buffer_->dec_recycle_lock();

    reply_received_callback_(ErrorCode::E_SUCCESS);
  }
}

void BackendConn::HandleConnect(const char * data, size_t bytes, bool query_has_more_data, const boost::system::error_code& error) {
  if (error) {
    socket_.close();
    // TODO : 如何通知给外界?
    LOG_DEBUG << "BackendConn::HandleConnect error, backend=" << this;
    query_sent_callback_(ErrorCode::E_CONNECT);
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
      std::bind(&BackendConn::HandleWrite, this, data, bytes, query_has_more_data,
          std::placeholders::_1, std::placeholders::_2));
}

BackendConn* BackendConnPool::Allocate(const ip::tcp::endpoint & ep){
  {
  //BackendConn* backend = new BackendConn(io_service_, ep);
  //return backend;
  }
  BackendConn* backend;
  auto it = conn_map_.find(ep);
  if ((it != conn_map_.end()) && (!it->second.empty())) {
    backend = it->second.front();
    it->second.pop();
    LOG_DEBUG << "BackendConnPool::Allocate reuse, backend=" << backend << " ep=" << ep << ", idles=" << it->second.size();
  } else {
    backend = new BackendConn(context_, ep);
    LOG_DEBUG << "BackendConnPool::Allocate create, backend=" << backend << " ep=" << ep;
  }
  auto res = active_conns_.insert(std::make_pair(backend, ep));
  assert(res.second);
  return backend;
}

void BackendConnPool::Release(BackendConn * backend) {
  {
  //LOG_DEBUG << "BackendConnPool::Release delete dtor";
  //delete backend;
  //return;
  }

  const auto ep_it = active_conns_.find(backend);
  if (ep_it == active_conns_.end()) {
    LOG_WARN << "BackendConnPool::Release unacceptable, backend=" << backend;
    delete backend;
    return;
  }

  const ip::tcp::endpoint & ep = ep_it->second;
  LOG_DEBUG << "BackendConnPool::Release backend=" << backend << " ep=" << ep;
  active_conns_.erase(ep_it);

  if (backend->no_recycle() || !backend->reply_complete()) {
    LOG_WARN << "BackendConnPool::Release end_of_reply unreceived! backend=" << backend
             << " no_recycle=" << backend->no_recycle()
             << " reply_complete=" << backend->reply_complete();
    delete backend;
    return;
  }

  const auto it = conn_map_.find(ep);
  if (it == conn_map_.end()) {
    backend->Reset();
    // backend->buffer()->Reset();
    conn_map_[ep].push(backend);
    LOG_DEBUG << "BackendConnPool::Release ok, backend=" << backend << " ep=" << ep << " pool_size=1";
  } else {
    if (it->second.size() >= kMaxConnPerEndpoint){
      LOG_WARN << "BackendConnPool::Release overflow, backend=" << backend << " ep=" << ep << " destroyed, pool_size=" << it->second.size();
      delete backend;
    } else {
      backend->Reset();
      // backend->buffer()->Reset();
      it->second.push(backend);
      LOG_DEBUG << "BackendConnPool::Release ok, backend=" << backend << " ep=" << ep << " pool_size=" << it->second.size();
    }
  }
}

}

