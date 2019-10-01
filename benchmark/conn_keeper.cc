#include "conn_keeper.h"

#include <boost/bind.hpp>
// #include <boost/algorithm/string.hpp>

#include "base/logging.h"

#include "redis_conn.h"

namespace yarmproxy {

void ConnectionKeeper::Start() {
  for(size_t i = 0; i < concurrency_; ++i) {
    topic_conn_.push_back(RedisConnection::Create(io_service_, host_, port_));
    usleep(200);
  }

  check_timer_.expires_from_now(boost::posix_time::seconds(300));
  check_timer_.async_wait(boost::bind(&ConnectionKeeper::OnCheckTimer, this,
                     boost::asio::placeholders::error));
}

void ConnectionKeeper::OnCheckTimer(const boost::system::error_code& error) {
  LOG_DEBUG << "ConnectionKeeper OnCheckTimer";
  size_t round = std::min(size_t(30), concurrency_ / 100);
  if (round <= 0) {
    round = 1;
  }
  for(size_t i = 0; i < round; ++i) {
    CheckNextConnection();
    usleep(10000);
  }

  check_timer_.expires_from_now(boost::posix_time::seconds(300));
  check_timer_.async_wait(boost::bind(&ConnectionKeeper::OnCheckTimer, this,
                     boost::asio::placeholders::error));
}

void ConnectionKeeper::CheckNextConnection() {
  std::shared_ptr<RedisConnection> conn = topic_conn_[next_check_];

  if (!conn || conn->IsClosed()) {
    std::shared_ptr<RedisConnection> new_conn = RedisConnection::Create(io_service_, host_, port_);
    topic_conn_[next_check_] = new_conn;
    // TODO : 处理 conn 为null的情况
    LOG_WARN << "CheckNextConnection reconnect, index=" << next_check_ << " old_conn=" << conn.get()
              << " new_conn=" << new_conn.get();
  } else {
    LOG_DEBUG << "CheckNextConnection ok, index=" << next_check_ << " topic=" << conn->topic()  << " conn=" << conn.get();
  }
  ++next_check_;
  next_check_ %= topic_conn_.size();
}

}

