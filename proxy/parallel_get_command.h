#ifndef _PARALLEL_GET_COMMAND_H_
#define _PARALLEL_GET_COMMAND_H_

#include "memc_command.h"
#include "base/logging.h"

using namespace boost::asio;

namespace mcproxy {

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(std::shared_ptr<ClientConnection> owner,
                     std::map<ip::tcp::endpoint, std::string>&& endpoint_query_map);

  virtual ~ParallelGetCommand();

  void ForwardRequest(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override {
    // TryForwardResponse(backend_conn_);
  }

private:
  void DoForwardRequest(const char *, size_t) override;
  bool ParseUpstreamResponse(BackendConn* backend) override;

  void PushReadyQueue(BackendConn* backend) override; 
  bool HasMoreBackend() const override {
    // FIXME
    bool ret = (finished_count_ + 1) < query_set_.size(); // NOTE: 注意这里要+1
    LOG_DEBUG << "ParallelGetCommand HasMoreBackend ret=" << ret
              << " finished_count_=" << finished_count_
              << " query_set_.size=" << query_set_.size();
    return ret;
  }
  void RotateFirstBackend() override {
    ++finished_count_;
    LOG_DEBUG << "ParallelGetCommand RotateFirstBackend finished_count_=" << finished_count_;
    if (ready_queue_.size() > 0) {
      replying_backend_ = ready_queue_.front();
      ready_queue_.pop();
      LOG_DEBUG << "ParallelGetCommand RotateFirstBackend, activate ready backend";
      TryForwardResponse(replying_backend_);
      return;
    }
    LOG_DEBUG << "ParallelGetCommand RotateFirstBackend, no ready backend, wait";
    replying_backend_ = nullptr;
  }

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
  // std::vector<BackendQuery*> query_set_;
  std::queue<BackendConn*> ready_queue_;
  size_t finished_count_;
};

}

#endif  // _PARALLEL_GET_COMMAND_H_

