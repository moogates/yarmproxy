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
    , unparsed_bulks_(ba.absent_bulks())
    , completed_backends_(0)
    , unreachable_backends_(0)
    , init_write_query_(true)
{
  unparsed_bulks_ += unparsed_bulks_ % 2;  // don't parse the 'key' now if 'value' not present
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
  // TODO : release all backends
//if (backend_conn_) {
//  backend_pool()->Release(backend_conn_);
//}
  LOG_DEBUG << "RedisMsetCommand dtor " << --redis_mset_cmd_count;
}

void RedisMsetCommand::WriteQuery() {
  if (!init_write_query_) {
    auto& tail_query = subqueries_[subqueries_.size() - 1];
    tail_query.phase_ = 3;
    tail_query.backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
        client_conn_->buffer()->unprocessed_bytes(),
        client_conn_->buffer()->parsed_unreceived_bytes() > 0);
    return;
  }
  init_write_query_ = false;
  for(size_t i = 0; i < subqueries_.size(); ++i) {
    auto& query = subqueries_[i];
  //if (query.phase_ > 0) {
  //  if (i == subqueries_.size() - 1) {
  //    LOG_WARN << "RedisMsetCommand WriteQuery has more data, phase=" << query.phase_;

  //    assert(query.phase_ == 2 || query.phase_ == 3);
  //    query.phase_ = 3;
  //    query.backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
  //        client_conn_->buffer()->unprocessed_bytes(),
  //        client_conn_->buffer()->parsed_unreceived_bytes() > 0);
  //  } else {
  //    // assert(query.phase_ == 2);
  //  }
  //  continue;
  //}
    if (!query.backend_) {
      query.backend_ = AllocateBackend(query.backend_endpoint_);
      backend_index_[query.backend_] = i;
    }

    client_conn_->buffer()->inc_recycle_lock();
    LOG_DEBUG << "RedisMsetCommand WriteQuery cmd=" << this
              << " backend=" << query.backend_ << ", key=("
              << redis::Bulk(query.data_, query.present_bytes_).to_string() << ")";

    // TODO : merge adjcent shared-endpoint queries
    const char MSET_PREFIX[] = "*3\r\n$4\r\nmset\r\n";
    query.backend_->WriteQuery(MSET_PREFIX, sizeof(MSET_PREFIX) - 1, true);
  }
}

void RedisMsetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_WARN << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  ++completed_backends_;

  // 判断是否最靠前的command, 是才可以转发
  if (unparsed_bulks_ == 0 && completed_backends_ == subqueries_.size()
      && client_conn_->IsFirstCommand(shared_from_this())) {
    TryWriteReply(backend);
    LOG_WARN << "OnBackendReplyReceived idx=" << backend_index_[backend] << " write reply, backend=" << backend;
  } else {
    LOG_WARN << "OnBackendReplyReceived idx=" << backend_index_[backend] << " no reply, backend=" << backend;
  }
}

void RedisMsetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply not implemented";
  // TODO : if connection refused, should report error & rotate
  // TryWriteReply(backend_conn_);
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
  client_conn_->buffer()->dec_recycle_lock();

  size_t idx = backend_index_[backend];

  bool has_artificial_query_prefix = true;
  if (has_artificial_query_prefix) {
    auto& query = subqueries_[idx];
    LOG_WARN << "OnWriteQueryFinished enter. idx=" << idx << " phase=" << query.phase_;
    if (query.phase_ == 0) {
      query.phase_ = 1;
      bool has_more_data = false;
      if (idx == subqueries_.size() - 1 && client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
        has_more_data = true;
      }
      query.backend_->WriteQuery(query.data_, query.present_bytes_, has_more_data);
      LOG_WARN << "OnWriteQueryFinished idx=" << idx << " phase 1 finished, phase 2 launched";
      return;
    } else if (query.phase_ == 1) {
      query.phase_ = 2;
      LOG_WARN << "OnWriteQueryFinished idx=" << idx << " phase 2 finished";
    } else if (query.phase_ == 3) {
      if (client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
        client_conn_->TryReadMoreQuery();
      } else {
        backend->ReadReply();
      }
      return;  // TODO : should return here? 
    } else {
      LOG_ERROR << "OnWriteQueryFinished idx=" << idx << " phase=" << query.phase_;
      // assert(false);
    }
  }

  // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
  if (!client_conn_->buffer()->recycle_locked() // 全部完成write query 才能释放recycle_lock
      && client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
    LOG_WARN << "OnWriteQueryFinished idx=" << idx << " phase 3, read more query";
    client_conn_->TryReadMoreQuery();
  }

  if (idx < subqueries_.size() - 1 || client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    LOG_WARN << "OnWriteQueryFinished idx=" << idx << " read reply, backend=" << backend;
    backend->ReadReply();
  }
}

bool RedisMsetCommand::QueryParsingComplete() {
  // assert(unparsed_bulks_ == 0);
  return unparsed_bulks_ == 0;
}

bool RedisMsetCommand::ParseIncompleteQuery() {
  LOG_WARN << "RedisMsetCommand::QueryParsingComplete unparsed_bulks_=" << unparsed_bulks_;
  ReadBuffer* buffer = client_conn_->buffer();
  std::vector<redis::Bulk> new_bulks;
  size_t total_parsed = 0;

  while(new_bulks.size() < unparsed_bulks_ && total_parsed < buffer->unparsed_received_bytes()) {
    const char * entry = buffer->unparsed_data() + total_parsed;
    size_t unparsed_bytes = buffer->unparsed_received_bytes() - total_parsed;
    new_bulks.emplace_back(entry, unparsed_bytes);
    redis::Bulk& bulk = new_bulks.back();

    if (bulk.present_size() < 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk error";
      return false;
    }

    if (bulk.present_size() == 0) {
      LOG_WARN << "ParseIncompleteQuery sub_bulk need more data";
      // return true;
      break;
    }
    total_parsed += bulk.total_size();
    LOG_WARN << "ParseIncompleteQuery parsed_bytes=" << bulk.total_size() << " total_parsed=" << total_parsed;
  }

  if (new_bulks.size() % 2 == 1) {
    total_parsed -= new_bulks.back().total_size();
    new_bulks.pop_back();
  }
  LOG_WARN << "ParseIncompleteQuery new_bulks.size=" << new_bulks.size();

  for(size_t i = 0; i + 1 < new_bulks.size(); i += 2) { 
    // TODO : limit max pending subqueries
    size_t idx = subqueries_.size();
    ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(new_bulks[i].payload_data(), new_bulks[i].payload_size(), "REDIS_bj");
    LOG_DEBUG << "RedisMsetCommand ParseIncompleteQuery, key[" << idx << "/" << ((unparsed_bulks_ - i) / 2 + idx)
              << "]=" << new_bulks[i].to_string() << " v=[" << new_bulks[i + 1].to_string() << "] ep=" << ep;
    // command->reset(new RedisMsetCommand(ep, client, ba));
    subqueries_.emplace_back(ep, 2, new_bulks[i].raw_data(), new_bulks[i].present_size() + new_bulks[i+1].present_size(), new_bulks[i+1].absent_size());
    auto& query = subqueries_.back();

    query.backend_ = AllocateBackend(query.backend_endpoint_);
    backend_index_[query.backend_] = idx;

    client_conn_->buffer()->inc_recycle_lock();
    LOG_DEBUG << "RedisMsetCommand ParseIncompleteQuery WriteQuery cmd=" << this
              << " backend=" << query.backend_ << ", key=("
              << redis::Bulk(query.data_, query.present_bytes_).to_string() << ")";

    // TODO : merge adjcent shared-endpoint queries
    const char MSET_PREFIX[] = "*3\r\n$4\r\nmset\r\n";
    query.backend_->WriteQuery(MSET_PREFIX, sizeof(MSET_PREFIX) - 1, true);
  }
  
  buffer->update_parsed_bytes(total_parsed);
  unparsed_bulks_ -= new_bulks.size();
  return true;
}

bool RedisMsetCommand::ParseReply(std::shared_ptr<BackendConn> backend) {
  size_t unparsed = backend->buffer()->unparsed_bytes();
  assert(unparsed > 0);
  const char * entry = backend->buffer()->unparsed_data();
  if (entry[0] != '+' && entry[0] != '-' && entry[0] != '$') {
    LOG_DEBUG << "RedisMsetCommand ParseReply unknown reply format(" << std::string(entry, unparsed) << ")";
    return false;
  }

  const char * p = GetLineEnd(entry, unparsed);
  if (p == nullptr) {
    LOG_DEBUG << "RedisMsetCommand ParseReply no enough data for parsing, please read more"
              // << " data=" << std::string(entry, backend->buffer()->unparsed_bytes())
              << " bytes=" << backend->buffer()->unparsed_bytes();
    return true;
  }

  backend->buffer()->update_parsed_bytes(p - entry + 1);
  LOG_WARN << "RedisMsetCommand ParseReply resp.size=" << p - entry + 1
            << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_recv_complete, backend=" << backend
            << " idx=" << backend_index_[backend];
  backend->set_reply_recv_complete();
  return true;
}

}

