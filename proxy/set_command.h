#ifndef _YARMPROXY_SET_COMMAND_H_
#define _YARMPROXY_SET_COMMAND_H_

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class SetCommand : public Command {
private:
  size_t query_header_bytes_;

  size_t query_written_bytes_;
  size_t query_body_bytes_;
  size_t query_writing_bytes_;

  ip::tcp::endpoint backend_endpoint_;
  std::shared_ptr<BackendConn> backend_conn_;
public:
  SetCommand(const ip::tcp::endpoint & ep,
          std::shared_ptr<ClientConnection> client,
          const char * buf, size_t cmd_len, size_t body_bytes);

  virtual ~SetCommand();

private:
  size_t query_body_upcoming_bytes() const;
  void OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;
  void OnWriteReplyEnabled() override;

  void WriteQuery(const char * data, size_t bytes) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;
  void DoWriteQuery(const char * query_data, size_t received_bytes) override;
};

}

#endif // _YARMPROXY_SET_COMMAND_H_
