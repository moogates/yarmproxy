#ifndef _PARALLEL_GET_COMMAND_H_
#define _PARALLEL_GET_COMMAND_H_

#include "command.h"

using namespace boost::asio;

namespace mcproxy {

class ParallelGetCommand : public Command {
public:
  ParallelGetCommand(std::shared_ptr<ClientConnection> owner,
                     std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map);

  virtual ~ParallelGetCommand();

  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override;

private:
  void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) override;
  void HookOnUpstreamReplyReceived(BackendConn* backend) override;
  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(BackendConn* backend) override;

  void PushReadyQueue(BackendConn* backend) override; 
  bool HasMoreBackend() const override {
    return completed_backends_ < query_set_.size(); // NOTE: 注意这里要
  }
  void RotateFirstBackend() override;

  std::string cmd_line_without_rn() const override {
    return "PARALLEL GET";
  }
  size_t request_body_upcoming_bytes() const override {
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

#endif  // _PARALLEL_GET_COMMAND_H_

