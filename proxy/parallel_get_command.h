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

  void ForwardQuery(const char * data, size_t bytes) override;
  void OnForwardReplyEnabled() override;

private:
  void OnForwardQueryFinished(BackendConn* backend, const boost::system::error_code& error) override;
  void DoForwardQuery(const char *, size_t) override;
  bool ParseReply(BackendConn* backend) override;

  void PushReadyQueue(BackendConn* backend) override; 
  bool HasMoreBackend() const override {
    // FIXME
    bool ret = (finished_count_ + 1) < query_set_.size(); // NOTE: 注意这里要+1
    LOG_DEBUG << "ParallelGetCommand HasMoreBackend ret=" << ret
              << " finished_count_=" << finished_count_
              << " query_set_.size=" << query_set_.size();
    return ret;
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
  // std::vector<BackendQuery*> query_set_;
  std::queue<BackendConn*> ready_queue_;
  std::set<BackendConn*> ready_set_;
  size_t finished_count_;
};

}

#endif  // _PARALLEL_GET_COMMAND_H_

