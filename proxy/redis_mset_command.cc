#include "redis_mset_command.h"

#include "logging.h"
#include "backend_conn.h"
#include "backend_locator.h"
#include "backend_pool.h"
#include "client_conn.h"
#include "error_code.h"
#include "read_buffer.h"
#include "worker_pool.h"

#include "redis_protocol.h"

namespace yarmproxy {

const char * GetLineEnd(const char * buf, size_t len);

std::atomic_int redis_mset_cmd_count;

RedisMsetCommand::RedisMsetCommand(/*const ip::tcp::endpoint & ep, */std::shared_ptr<ClientConnection> client, const redis::BulkArray& ba)
    : Command(client, std::string(ba.raw_data(), ba.parsed_size()))
    // , backend_endpoint_(ep)
    , unparsed_bulks_(ba.absent_bulks())
{
  unparsed_bulks_ -= unparsed_bulks_ % 2;  // don't parse the 'key' now if 'value' not present
  for(size_t i = 1; (i + 1) < ba.present_bulks(); i += 2) { // only 'key' is inadequate, 'value' field must be present
    ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(ba[i].payload_data(), ba[i].payload_size(), "REDIS_bj");
    LOG_DEBUG << "RedisMsetCommand ctor, key[" << (i - 1) / 2 << "/" << (ba.present_bulks() - 1) / 2
              << "]=" << ba[i].to_string() << " ep=" << ep;
    // command->reset(new RedisMsetCommand(ep, client, ba));
    subqueries_.emplace_back(ep, 2, ba[i].raw_data(), ba[i].present_size() + ba[i+1].present_size(), ba[i+1].absent_size());
  }
  LOG_DEBUG << "RedisMsetCommand ctor " << ++redis_mset_cmd_count;
}

RedisMsetCommand::~RedisMsetCommand() {
  if (backend_conn_) {
    backend_pool()->Release(backend_conn_);
  }
  LOG_DEBUG << "RedisMsetCommand dtor " << --redis_mset_cmd_count;
}

void RedisMsetCommand::WriteQuery() {
  for(size_t i = 0; i < subqueries_.size(); ++i) {
    auto& query = subqueries_[i];
    if (!query.backend_) {
      query.backend_ = AllocateBackend(query.backend_endpoint_);
      backend_index_[query.backend_] = i;
    }

    client_conn_->buffer()->inc_recycle_lock();
    LOG_DEBUG << "RedisMsetCommand WriteQuery cmd=" << this
              << " backend=" << query.backend_ << ", key=("
              << redis::Bulk(query.data_, query.present_bytes_).to_string() << ")";

    const char MSET_PREFIX[] = "*3\r\n$4\r\nmset\r\n";
    query.backend_->WriteQuery(MSET_PREFIX, sizeof(MSET_PREFIX) - 1, true);
    // TODO : merge adjcent shared-endpoint queries
    // TODO : only one async_read() allowed per stream
    query.backend_->WriteQuery(query.data_, query.present_bytes_, query.absent_bytes_ > 0);
  }
  return;
}

void RedisMsetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  // 判断是否最靠前的command, 是才可以转发
  if (client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
  }
  backend->TryReadMoreReply(); // backend 正在read more的时候，不能memmove，不然写回的数据位置会相对漂移
}

void RedisMsetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply backend_conn_=" << backend_conn_;
  // TODO : if connection refused, should report error & rotate
  TryWriteReply(backend_conn_);
}

void RedisMsetCommand::RotateReplyingBackend(bool) {
  client_conn_->RotateReplyingCommand();
}

// try to keep pace with parent class impl
void RedisMsetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
    //LOG_WARN << "OnWriteQueryFinished conn_refused, endpoint=" << backend->remote_endpoint()
    //         << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_WARN << "OnWriteQueryFinished error";
    }
    return;
  }

  bool has_artificial_query_prefix = true;
  if (has_artificial_query_prefix) {
    auto& query = subqueries[backend_index_[backend_]];
    if (query.phase_ == 0) {
      query.phase_ = 1;
      query.backend_->WriteQuery(query.data_, query.present_bytes_, query.absent_bytes_ > 0);
      LOG_WARN << "OnWriteQueryFinished phase 1";
      return;
    } else {
      LOG_WARN << "OnWriteQueryFinished phase 2";
    }
  }

  // TODO : ================ continue from HERE : phase 2 ==========
  if (query_data_zero_copy()) {
    client_conn_->buffer()->dec_recycle_lock();
    // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
    if (client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
      client_conn_->TryReadMoreQuery();
      return;
    }
  }
  backend->ReadReply();
}

bool RedisMsetCommand::QueryParsingComplete() {
  LOG_WARN << "RedisMsetCommand::QueryParsingComplete unparsed_bulks_=" << unparsed_bulks_;
  // assert(unparsed_bulks_ == 0);
  return unparsed_bulks_ == 0;
}

bool RedisMsetCommand::ParseIncompleteQuery() {
  ReadBuffer* buffer = client_conn_->buffer();
  while(unparsed_bulks_ > 0 && buffer->unparsed_received_bytes() > 0) {
    size_t unparsed_bytes = buffer->unparsed_received_bytes();
    const char * entry = buffer->unparsed_data();

    redis::Bulk bulk(entry, unparsed_bytes);
    if (bulk.present_size() < 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk error";
      return false;
    }
    if (bulk.present_size() == 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk need more data";
      return true;
    }
    LOG_WARN << "ParseIncompleteQuery parsed_bytes=" << bulk.total_size();
    buffer->update_parsed_bytes(bulk.total_size());
    --unparsed_bulks_;
  }
  return true;
}

bool RedisMsetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  assert(backend_conn_ == backend);
  size_t unparsed = backend_conn_->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend_conn_->buffer()->unparsed_data();
  if (entry[0] != '+' && entry[0] != '-' && entry[0] != '$') {
    LOG_DEBUG << "RedisMsetCommand ParseReply unknown reply format(" << std::string(entry, unparsed) << ")";
    return false;
  }

  const char * p = GetLineEnd(entry, unparsed);
  if (p == nullptr) {
    LOG_DEBUG << "RedisMsetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend_conn_->buffer()->unparsed_bytes())
              << " bytes=" << backend_conn_->buffer()->unparsed_bytes();
    return true;
  }

  backend_conn_->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_DEBUG << "RedisMsetCommand ParseReply resp.size=" << p - entry + 1
            // << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_recv_complete, backend=" << backend;
  backend->set_reply_recv_complete();
  return true;
}

}

