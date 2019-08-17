#ifndef _GET_COMMAND_H_
#define _GET_COMMAND_H_

#include "memc_command.h"

using namespace boost::asio;

namespace mcproxy {

class SingleGetCommand : public MemcCommand {
public:
  SingleGetCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len);

  virtual ~SingleGetCommand();

  virtual void OnForwardResponseReady();

  virtual void ForwardRequest(const char *, size_t);

  virtual void OnUpstreamRequestWritten(size_t bytes, const boost::system::error_code& error) {
    // 不需要再通知Client Conn
  }
private:
  virtual bool ParseUpstreamResponse();
};

}

#endif  // _GET_COMMAND_H_

