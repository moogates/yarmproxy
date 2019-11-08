#include "conn_keeper.h"

#include <functional>
// #include <boost/algorithm/string.hpp>

#include "../proxy/logging.h"

#include "redis_conn.h"

namespace yarmproxy {

void ConnectionKeeper::Start() {
  for(size_t i = 0; i < concurrency_; ++i) {
    topic_conn_.push_back(RedisConnection::Create(io_service_, host_, port_));
    usleep(200);
  }

  check_timer_.expires_after(std::chrono::seconds(3));
  check_timer_.async_wait(std::bind(&ConnectionKeeper::OnCheckTimer, this,
                     std::placeholders::_1));
}

void ConnectionKeeper::OnCheckTimer(const boost::system::error_code& error) {
  LOG_DEBUG << "ConnectionKeeper OnCheckTimer";
  size_t round = std::min(size_t(100), concurrency_ / 20);
  if (round <= 0) {
    round = 1;
  }
  for(size_t i = 0; i < round; ++i) {
    CheckNextConnection();
    usleep(10000);
  }

  check_timer_.expires_after(std::chrono::milliseconds(500));
  check_timer_.async_wait(std::bind(&ConnectionKeeper::OnCheckTimer, this,
                     std::placeholders::_1));
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
    LOG_DEBUG << "CheckNextConnection ok, index=" << next_check_ << " conn=" << conn.get();
  }
  ++next_check_;
  next_check_ %= topic_conn_.size();
}

}

