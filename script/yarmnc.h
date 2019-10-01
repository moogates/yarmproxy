#ifndef _YARMPROCY_YARM_CLIENT_H_
#define _YARMPROCY_YARM_CLIENT_H_

#include <string>
#include <functional>

#include <sys/select.h>

#ifdef WINDOWS	
#include <winsock2.h>
#endif

namespace base {

using DataHandler = std::function<void(const char* data, size_t bytes)>;

class YarmClient {
public:
  YarmClient(const char* host, int port, const DataHandler&);

  virtual ~YarmClient();
  int Connect();
  int Run();

private:
  int NonblockingWrite();
  int NonblockingRead();
  int LoadQueryData();
  void PrepareSlect();

  bool write_finished() const {
    return (write_buf_begin_ == write_buf_end_) &&
             stdin_status_ != 0;
  }

private:
  std::string host_;
  int port_;

#ifdef WINDOWS
  SOCKET sock_ = -1;
#else
  int sock_ = -1;
#endif // WINDOWS

  enum { kReadBufLength = 16 * 1024 };
  char read_buf_[kReadBufLength];

  int stdin_status_ = 0;
  enum { kWriteBufLength = 4 * 1024 };
  char write_buf_[kWriteBufLength];
  size_t write_buf_begin_ = 0, write_buf_end_ = 0;
  bool shutdown_write_ = false;

  std::function<void(const char* data, size_t bytes)> data_handler_;

  fd_set read_set_, write_set_, error_set_;
  struct timeval timeval_;

  size_t total_bytes_input_ = 0;
  size_t total_bytes_sent_ = 0;
  size_t total_bytes_recv_ = 0;
};
 
}

#endif // _YARMPROCY_YARM_CLIENT_H_
