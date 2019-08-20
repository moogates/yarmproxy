#include "get_command.h"

#include "base/logging.h"
#include "client_conn.h"
#include "backend_conn.h"

namespace mcproxy {

std::atomic_int single_get_cmd_count;

UpstreamReadCallback WrapOnUpstreamResponse(std::weak_ptr<MemcCommand> cmd_wptr);
UpstreamWriteCallback WrapOnUpstreamRequestWritten(std::weak_ptr<MemcCommand> cmd_wptr);
ForwardResponseCallback WrapOnForwardResponseFinished(size_t to_transfer_bytes, std::weak_ptr<MemcCommand> cmd_wptr);

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


SingleGetCommand::SingleGetCommand(boost::asio::io_service& io_service, const ip::tcp::endpoint & ep, 
        std::shared_ptr<ClientConnection> owner, const char * buf, size_t cmd_len)
    : MemcCommand(io_service, ep, owner, buf, cmd_len) 
    , cmd_line_(buf, cmd_len)
{
  LOG_DEBUG << "SingleGetCommand ctor " << ++single_get_cmd_count;
}

SingleGetCommand::~SingleGetCommand() {
  LOG_DEBUG << "SingleGetCommand dtor " << --single_get_cmd_count;
}

bool SingleGetCommand::ParseUpstreamResponse() {
  bool valid = true;
  while(upstream_conn_->read_buffer_.unparsed_bytes() > 0) {
    const char * entry = upstream_conn_->read_buffer_.unparsed_data();
    const char * p = GetLineEnd(entry, upstream_conn_->read_buffer_.unparsed_bytes());
    if (p == nullptr) {
      // TODO : no enough data for parsing, please read more
      LOG_DEBUG << "ParseUpstreamResponse no enough data for parsing, please read more"
                << " data=" << std::string(entry, upstream_conn_->read_buffer_.unparsed_bytes())
                << " bytes=" << upstream_conn_->read_buffer_.unparsed_bytes();
      return true;
    }

    if (entry[0] == 'V') {
      // "VALUE <key> <flag> <bytes>\r\n"
      size_t body_bytes = GetValueBytes(entry, p);
      size_t entry_bytes = p - entry + 1 + body_bytes + 2;

      upstream_conn_->read_buffer_.update_parsed_bytes(entry_bytes);
      break; // TODO : 每次转发一条，only for test
    } else {
      // "END\r\n"
      if (strncmp("END\r\n", entry, sizeof("END\r\n") - 1) == 0) {
        set_upstream_nomore_response();
        upstream_conn_->read_buffer_.update_parsed_bytes(sizeof("END\r\n") - 1);
        if (upstream_conn_->read_buffer_.unparsed_bytes() != 0) { // TODO : pipeline的情况呢?
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

void SingleGetCommand::OnForwardResponseReady() {
  if (upstream_conn_->read_buffer_.unprocessed_bytes() == 0) { // TODO : for test only, 正常这里不触发解析, 在收到数据时候触发的解析，会一次解析所有可解析的
     ParseUpstreamResponse();
  }

  if (!is_forwarding_response_ && upstream_conn_->read_buffer_.unprocessed_bytes() > 0) {
    is_forwarding_response_ = true; // TODO : 这个flag是否真的需要? 需要，防止重复的写回请求
    auto cb_wrap = WrapOnForwardResponseFinished(upstream_conn_->read_buffer_.unprocessed_bytes(), shared_from_this());
    client_conn_->ForwardResponse(upstream_conn_->read_buffer_.unprocessed_data(),
                                  upstream_conn_->read_buffer_.unprocessed_bytes(),
                                  cb_wrap);
    // LOG_DEBUG << "SingleGetCommand OnForwardResponseReady, data="
    //           << std::string(upstream_conn_->read_buffer_.unprocessed_data(), upstream_conn_->read_buffer_.unprocessed_bytes() - 2)
    //           << " unprocessed_bytes=" << upstream_conn_->read_buffer_.unprocessed_bytes();
  } else {
    LOG_DEBUG << "SingleGetCommand OnForwardResponseReady, upstream no data ready to_transfer, waiting to read more data then write down";
  }
}

void SingleGetCommand::DoForwardRequest(const char *, size_t) {
  upstream_conn_->ForwardRequest(cmd_line_.data(), cmd_line_.size(), false);
}

}


