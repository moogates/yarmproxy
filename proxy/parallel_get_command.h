#ifndef _YARMPROXY_PARALLEL_GET_COMMAND_H_
#define _YARMPROXY_PARALLEL_GET_COMMAND_H_

#include <map>
#include <set>

#include <boost/asio.hpp>

#include "command.h"

namespace yarmproxy {

using namespace boost::asio;

class ParallelGetCommand : public Command {
public:
  ParallelGetCommand(std::shared_ptr<ClientConnection> client, const std::string& original_header, 
                     std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map);

  virtual ~ParallelGetCommand();

  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override;

private:
  void OnForwardQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) override;

  void HookOnUpstreamReplyReceived(std::shared_ptr<BackendConn> backend) override;
  void OnBackendConnectError(std::shared_ptr<BackendConn> backend) override;

  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(std::shared_ptr<BackendConn> backend) override;

  void PushWaitingReplyQueue(std::shared_ptr<BackendConn> backend) override;
  bool HasMoreBackend() const override;// rename -> HasUnfinishedBanckends()
  void RotateReplyingBackend() override;

  size_t query_body_upcoming_bytes() const override {
    return 0;
  }

  struct BackendQuery {
    BackendQuery(const ip::tcp::endpoint& ep, std::string&& query_line)
        : query_line_(query_line)
        , backend_addr_(ep)
        , backend_conn_(nullptr) {
    }
    ~BackendQuery();
    std::string query_line_;
    ip::tcp::endpoint backend_addr_;
    std::shared_ptr<BackendConn> backend_conn_;
  };

  std::vector<std::unique_ptr<BackendQuery>> query_set_;
  std::list<std::shared_ptr<BackendConn>> waiting_reply_queue_;
  std::shared_ptr<BackendConn> last_backend_;
  std::set<std::shared_ptr<BackendConn>> received_reply_backends_;
};

}

#endif  // _YARMPROXY_PARALLEL_GET_COMMAND_H_

