#ifndef _QUOTE_MQTTD_CASCADE_CONN_MGR_
#define _QUOTE_MQTTD_CASCADE_CONN_MGR_

#include <set>
#include <string>

#include <boost/asio.hpp>

namespace yarmproxy {

class RedisConnection;

class ConnectionKeeper {
public:
  ConnectionKeeper(boost::asio::io_service& io_service, 
         const std::string& host, int port, size_t concurrency)
     : io_service_(io_service)
     , host_(host)
     , port_(port)
     , concurrency_(concurrency)
     , check_timer_(io_service) {
  }

  void Start();

 private:
  void OnCheckTimer(const boost::system::error_code& error);
  void CheckNextConnection();

  boost::asio::io_service& io_service_;
  const std::string host_;
  int port_;

  size_t concurrency_;
  size_t next_check_ = 0;

  boost::asio::steady_timer check_timer_;
  std::vector<std::shared_ptr<RedisConnection>> topic_conn_;

 private:
  ConnectionKeeper& operator=(const ConnectionKeeper&);
};

}

#endif // _QUOTE_MQTTD_CASCADE_CONN_MGR_
