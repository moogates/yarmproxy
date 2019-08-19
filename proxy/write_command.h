#ifndef _WRITE_COMMAND_H_
#define _WRITE_COMMAND_H_

#include "memc_command.h"

using namespace boost::asio;

namespace mcproxy {

class WriteCommand : public MemcCommand {
private:
  // bool is_forwarding_request_;
  // bool is_forwarding_response_;
  const char * request_cmd_line_;
  size_t request_cmd_len_;

  size_t request_forwarded_bytes_;
  size_t request_body_bytes_;
  size_t bytes_forwarding_;

public:
  WriteCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
          std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len, size_t body_bytes);

  virtual ~WriteCommand();

  virtual size_t request_body_upcoming_bytes() const;

  virtual void OnUpstreamRequestWritten(size_t, const boost::system::error_code& error);

  virtual size_t request_body_bytes() const {  // for debug info only
    return request_body_bytes_;
  }
private:
  virtual bool ParseUpstreamResponse();
  virtual void DoForwardRequest(const char * request_data, size_t client_buf_received_bytes);

  virtual std::string cmd_line_without_rn() const {
    return std::string(request_cmd_line_, request_cmd_len_ - 2);
  }
};

}

#endif // _WRITE_COMMAND_H_