#ifndef _YARMPROXY_PARALLEL_GET_COMMAND_H_
#define _YARMPROXY_PARALLEL_GET_COMMAND_H_

#include <set>

#include "command.h"

using namespace boost::asio;

namespace yarmproxy {

class ParallelGetCommand : public Command {
public:
  ParallelGetCommand(std::shared_ptr<ClientConnection> client,
                     std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map);

  virtual ~ParallelGetCommand();

  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override;

private:
  void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) override;
  void HookOnUpstreamReplyReceived(BackendConn* backend) override;
  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(BackendConn* backend) override;

  void PushWaitingReplyQueue(BackendConn* backend) override;
  bool HasMoreBackend() const {
    return completed_backends_ < query_set_.size(); // NOTE: 注意这里要
  }
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
    BackendConn* backend_conn_;
  };

  std::vector<std::unique_ptr<BackendQuery>> query_set_;
  std::list<BackendConn*> waiting_reply_queue_;
  BackendConn* last_backend_;
  std::set<BackendConn*> received_reply_backends_;
};

}

#endif  // _YARMPROXY_PARALLEL_GET_COMMAND_H_

