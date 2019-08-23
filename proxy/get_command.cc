#include "get_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "backend_conn.h"

namespace mcproxy {

std::atomic_int single_get_cmd_count;

const char * GetLineEnd(const char * buf, size_t len);

size_t GetValueBytes(const char * data, const char * end) {
  // "VALUE <key> <flag> <bytes>\r\n"
  const char * p = data + sizeof("VALUE ");
  int count = 0;
  while(p != end) {
    if (*p == ' ') {
      if (++count == 2) {
        return std::stoi(p + 1);
      }
    }
    ++p;
  }
  return 0;
}


SingleGetCommand::SingleGetCommand(const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len)
    : MemcCommand(ep, owner, buf, cmd_len) 
    , cmd_line_(buf, cmd_len)
{
  LOG_DEBUG << "SingleGetCommand ctor " << ++single_get_cmd_count;
}

SingleGetCommand::~SingleGetCommand() {
  LOG_DEBUG << "SingleGetCommand dtor " << --single_get_cmd_count;
}

bool SingleGetCommand::ParseUpstreamResponse(BackendConn* backend) {
  bool valid = true;
  assert(backend_conn_ == backend);
  while(backend_conn_->read_buffer_.unparsed_bytes() > 0) {
    const char * entry = backend_conn_->read_buffer_.unparsed_data();
    const char * p = GetLineEnd(entry, backend_conn_->read_buffer_.unparsed_bytes());
    if (p == nullptr) {
      // TODO : no enough data for parsing, please read more
      LOG_DEBUG << "ParseUpstreamResponse no enough data for parsing, please read more"
                << " data=" << std::string(entry, backend_conn_->read_buffer_.unparsed_bytes())
                << " bytes=" << backend_conn_->read_buffer_.unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;

      backend_conn_->read_buffer_.update_parsed_bytes(entry_bytes);
      // break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        backend_conn_->read_buffer_.update_parsed_bytes(sizeof("END\r\n") - 1);
        if (backend_conn_->read_buffer_.unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
          valid = false;
          LOG_WARN << "ParseUpstreamResponse END not really end!";
        } else {
          LOG_INFO << "ParseUpstreamResponse END is really end!";
        }
        break;
      } else {
        LOG_WARN << "ParseUpstreamResponse BAD DATA";
        // TODO : ERROR
        valid = false;
        break;
      }
    }
  }
  return valid;
}

void SingleGetCommand::DoForwardRequest(const char *, size_t) {
  backend_conn_->ForwardRequest(cmd_line_.data(), cmd_line_.size(), false);
}

}


