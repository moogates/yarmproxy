/*
 * Test driver for YarmProxy, similar to 'nc' command in tcp client mode.
 * This simple tool is written to replace 'nc' because nc has bugs and might hang
 * during high-speed large-thoroughput client/server TCP data exchange.
 * Author: Mu Yuwei (moogates@163.com)
 */

#include <iostream>

#ifdef WINDOWS
#include <windows.h>
#pragma comment(lib,"ws2_32")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include <string>
#include <functional>

#include <sys/select.h>

#ifdef WINDOWS	
#include <winsock2.h>
#endif

#include "stopwatch.h"

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

  enum { kReadBufLength = 64 * 1024 };
  char read_buf_[kReadBufLength];

  int stdin_status_ = 0;
  enum { kWriteBufLength = 64 * 1024 };
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

YarmClient::YarmClient(const char* host, int port, const DataHandler& handler)
  : host_(host)
  , port_(port)
  , data_handler_(handler) {
}

YarmClient::~YarmClient() {
  if (sock_ > 0) {
#ifdef WINDOWS
    closesocket(sock_);
#else
    close(sock_);
#endif
  }

  std::cerr << "total_bytes_input = " << total_bytes_input_ << std::endl;
  std::cerr << "total_bytes_sent  = " << total_bytes_sent_ << std::endl;
  std::cerr << "total_bytes_recv  = " << total_bytes_recv_ << std::endl;
}

/** Returns true on success, or false if there was an error */
bool SetSocketBlocking(int fd, bool blocking)
{
   if (fd < 0) return false;
#ifdef _WIN32
   unsigned long mode = blocking ? 0 : 1;
   return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? true : false;
#else
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags == -1) return false;
   flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
   return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
#endif
}

int YarmClient::Connect() {
  //create socket if it is not already created
  if (sock_ == -1) {
    //Create socket
    sock_ = socket(AF_INET , SOCK_STREAM , 0);
    if (sock_ < 0) {
      std::cerr << "Could not create socket" << std::endl;
      return -1;
    }
    SetSocketBlocking(sock_, false);
  }

  struct sockaddr_in server;
  //setup address structure
  if (inet_addr(host_.c_str()) == INADDR_NONE) {
    struct hostent *he;
    struct in_addr **addr_list;

    //resolve the hostname, its not an ip address
    if ((he = gethostbyname(host_.c_str())) == NULL) {
        //gethostbyname failed
      std::cerr << "Failed to resolve hostname" << host_ << std::endl;
      return -2;
    }

    //Cast the h_addr_list to in_addr , since h_addr_list also has the ip address in long format only
    addr_list = (struct in_addr **) he->h_addr_list;
    for(int i = 0; addr_list[i] != NULL; i++) {
      //strcpy(ip , inet_ntoa(*addr_list[i]) );
      server.sin_addr = *addr_list[i];
      std::cerr << "Connect " << host_ << " resolved to " << inet_ntoa(*addr_list[i]) << std::endl;
      break;
    }
  } else { //plain ip address
    #ifdef WINDOWS
    server.sin_addr.S_un.S_addr = inet_addr(host_.c_str());
    #else
    server.sin_addr.s_addr = inet_addr(host_.c_str());
    #endif
  }

  server.sin_family = AF_INET;
  server.sin_port = htons(port_);

  //Connect to remote server
  if (connect(sock_, (sockaddr *)&server , sizeof(server)) < 0) {
    if (errno == EINPROGRESS  || errno == EAGAIN || // TODO : recheck it
        errno == EWOULDBLOCK) {
      std::cerr << "Connecting " << host_ << ":" << port_ << std::endl;
      return 1;
    }
    std::cerr << "Connect " << host_ << ":" << port_ << " failed" << std::endl;
    return -3;
  }

  return 0;
}

int YarmClient::NonblockingWrite() {
  while(write_buf_begin_ < write_buf_end_) {
    int ret = send(sock_, write_buf_ + write_buf_begin_,
                   write_buf_end_ - write_buf_begin_, 0);
    if (ret < 0) {
      if (errno == EINPROGRESS  || errno == EAGAIN || // TODO : recheck it
          errno == EWOULDBLOCK) {
        break;
      }
      std::cerr << "Send data failed" << std::endl;
      return -2;
    } else if (ret == 0) {
      std::cerr << "Send data server closed" << std::endl;
      return -1;
    }
    write_buf_begin_ += size_t(ret);
    total_bytes_sent_ += ret;
    // std::cerr << "Write ok, bytes=" << ret << " total=" << total_bytes_sent_ << std::endl;
  }
  return 0;
}

int YarmClient::LoadQueryData() {
  if (stdin_status_ != 0 || write_buf_begin_ < write_buf_end_) {
    return 0;
  }

  int ret = read(STDIN_FILENO, (void*)write_buf_, kWriteBufLength);
  if (ret > 0) {
    total_bytes_input_ += ret;
    write_buf_end_ = size_t(ret);
    write_buf_begin_ = 0;
  } else if (ret == 0) {
    stdin_status_ = -1; // EOF
    return -1;
  } else {
    stdin_status_ = -2;
    return -2;
  }
  return ret;
}

void YarmClient::PrepareSlect() {
    FD_ZERO(&read_set_);
    FD_ZERO(&write_set_);
    FD_ZERO(&error_set_);

    FD_SET(sock_, &read_set_);
    if (!write_finished()) {
      FD_SET(sock_, &write_set_);
    }
    FD_SET(sock_, &error_set_);

    timeval_.tv_sec = 1;
    timeval_.tv_usec = 0;
}

int YarmClient::Run() {
  stopwatch::Stopwatch sw;
  if (Connect() < 0) {
    std::cerr <<"Connect error" << std::endl;
    return 1;
  }
  std::uint64_t connected_tp = sw.elapsed<stopwatch::mus>();
  std::uint64_t write_complete_tp = 0;
  std::vector<std::uint64_t> writtalbe_tps;

  while(true) {
    int ret = LoadQueryData();
    PrepareSlect();

    ret = select(sock_ + 1, &read_set_, &write_set_, &error_set_, &timeval_);
    if (ret < 0) {
      std::cerr << "select error" << std::endl;
      break;
    } else if (ret == 0) {
      // "select timeout
      continue;
    }

    if (FD_ISSET(sock_, &read_set_)) {
      if (NonblockingRead() < 0) {
        break;
      }
    }
    if (FD_ISSET(sock_, &write_set_)) {
      writtalbe_tps.push_back(sw.elapsed<stopwatch::mus>());
      if (NonblockingWrite() < 0) {
        break;
      }
    }
    if (FD_ISSET(sock_, &error_set_)) {
      std::cerr << "select fd error" << std::endl;
      break;
    }

    if (write_finished() && !shutdown_write_) {
      write_complete_tp = sw.elapsed<stopwatch::mus>();
      std::cerr << "Send data finished." << std::endl;
      shutdown(sock_, SHUT_WR);
      shutdown_write_ = true;
    }
  }
  std::uint64_t all_complete_tp = sw.elapsed<stopwatch::mus>();
  // std::cerr << "Stopwatch connected=" << connected_tp
  //     << " write_complete=" << write_complete_tp
  //     << " all_complete=" << all_complete_tp
  //     << std::endl << "[";
  for(const auto& tp : writtalbe_tps) {
    std::cerr << " " << tp;
  }
  std::cerr << "]" << std::endl;
  return 0;
}

int YarmClient::NonblockingRead() {
  for(;;) {
    int ret = recv(sock_, read_buf_, kReadBufLength, 0);
    if (ret > 0) {
      total_bytes_recv_ += ret;
      data_handler_(read_buf_, ret);
    } else if (ret == 0) {
      std::cerr << "Recv data finished." << std::endl;
      return -1;
    } else {
      if (errno == EAGAIN || errno == EINPROGRESS ||
          errno == EWOULDBLOCK) {
        break;
      }
      std::cerr << "Recv data error." << std::endl;
      return -2;
    }
  }

  return 0;
}

int main(int argc, char *argv[]) {
  const char* host = "127.0.0.1";
  int port = 11311;

  if (argc >= 3) {
    host = argv[1];
    port = std::stoi(argv[2]);
  }
  YarmClient yc(host, port, [](const char* data, size_t bytes) {
        std::cout.write(data, bytes);
        std::cout.flush();
      });
  yc.Run();
  return 0;
}

