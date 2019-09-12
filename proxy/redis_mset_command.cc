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
    , subquery_index_(0)  // TODO : for test only
    , completed_backends_(0)
    , unreachable_backends_(0)
    , init_write_query_(true)
{
  unparsed_bulks_ += unparsed_bulks_ % 2;  // don't parse the 'key' now if 'value' not present
  for(size_t i = 1; (i + 1) < ba.present_bulks(); i += 2) { // only 'key' is inadequate, 'value' field must be present
    ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(ba[i].payload_data(), ba[i].payload_size(), "REDIS_bj");
    LOG_DEBUG << "RedisMsetCommand ctor, waiting for ActivateWaitingSubquery, key[" << (i - 1) / 2 << "/" << (ba.present_bulks() - 1) / 2
              << "]=" << ba[i].to_string() << " ep=" << ep;
    client_conn_->buffer()->inc_recycle_lock();
    waiting_subqueries_.emplace_back(new Subquery(ep, 2, ba[i].raw_data(),
                                     ba[i].present_size() + ba[i+1].present_size(),
                                     ba[i+1].absent_size(), subquery_index_++));
  }
  if (!waiting_subqueries_.empty()) {
    tail_query_ = waiting_subqueries_.back();
  }
  LOG_DEBUG << "RedisMsetCommand ctor " << ++redis_mset_cmd_count;
}

RedisMsetCommand::~RedisMsetCommand() {
  for(auto query : waiting_subqueries_) {
    backend_pool()->Release(query->backend_);
  }
  // TODO : release all backends
//if (backend_conn_) {
//  backend_pool()->Release(backend_conn_);
//}
  LOG_DEBUG << "RedisMsetCommand dtor " << --redis_mset_cmd_count;
}

void RedisMsetCommand::WriteQuery() {
  if (!init_write_query_) {
    assert(tail_query_);
    tail_query_->phase_ = 3;
    LOG_DEBUG << "RedisMsetCommand WriteQuery non-init, data.size=" << client_conn_->buffer()->unprocessed_bytes()
             << " backend=" << tail_query_->backend_ << " query=" << tail_query_->index_;
    tail_query_->backend_->WriteQuery(client_conn_->buffer()->unprocessed_data(),
        client_conn_->buffer()->unprocessed_bytes(),
        client_conn_->buffer()->parsed_unreceived_bytes() > 0);
    return;
  }

  init_write_query_ = false;
  static const size_t MAX_ACTIVE_SUBQUERIES = 32;
  ActivateWaitingSubquery();
}

void RedisMsetCommand::OnBackendReplyReceived(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS || !ParseReply(backend)) {
    LOG_DEBUG << "Command::OnBackendReplyReceived error, backend=" << backend;
    client_conn_->Abort();
    return;
  }

  if (!backend->reply_recv_complete()) {
    backend->TryReadMoreReply();
    return;
  }

  ++completed_backends_;

  // 判断是否最靠前的command, 是才可以转发
  if (unparsed_bulks_ == 0 
      && waiting_subqueries_.empty() // FIXME : 这里应该是都完成，而不仅仅是开始
      && pending_subqueries_.size() == 1
      && client_conn_->IsFirstCommand(shared_from_this())) {
    // assert(pending_subqueries_.size() == 1);
    TryWriteReply(backend);
    LOG_DEBUG << "OnBackendReplyReceived query=" << pending_subqueries_[backend]->index_
             << " write reply, backend=" << backend;
  } else {
    // TODO : prepare for recycling. a bit too tedious
    backend->buffer()->update_processed_bytes(backend->buffer()->unprocessed_bytes());
    backend->set_reply_recv_complete();

    // assert(backend_index_.erase(backend) > 0);
    assert(pending_subqueries_.erase(backend) > 0);
    backend_pool()->Release(backend);  // 这里release
    ActivateWaitingSubquery();
  }
}

void RedisMsetCommand::StartWriteReply() {
  LOG_DEBUG << "StartWriteReply TryWriteReply not implemented";
  // TODO : if connection refused, should report error & rotate
  // TryWriteReply(backend_conn_);
}

void RedisMsetCommand::RotateReplyingBackend(bool success) {
  if (success) {
    ++completed_backends_;
  }
  assert(unparsed_bulks_ == 0 && waiting_subqueries_.empty());
  LOG_DEBUG << "RedisMsetCommand::RotateReplyingBackend";
  client_conn_->RotateReplyingCommand();
}

// try to keep pace with parent class impl
void RedisMsetCommand::OnWriteQueryFinished(std::shared_ptr<BackendConn> backend, ErrorCode ec) {
  if (ec != ErrorCode::E_SUCCESS) {
    if (ec == ErrorCode::E_CONNECT) {
    //LOG_DEBUG << "OnWriteQueryFinished conn_refused, endpoint=" << backend->remote_endpoint()
    //         << " backend=" << backend;
      OnBackendConnectError(backend);
    } else {
      client_conn_->Abort();
      LOG_DEBUG << "OnWriteQueryFinished error, ec=" << int(ec);
    }
    return;
  }

  auto& query = pending_subqueries_[backend];

  bool has_artificial_query_prefix = true;
  if (has_artificial_query_prefix) {
    LOG_DEBUG << "OnWriteQueryFinished enter. query=" << query->index_ << " phase=" << query->phase_;
    if (query->phase_ == 0) {
      query->phase_ = 1;
      bool has_more_data = false;
      if (query == tail_query_ && client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
        has_more_data = true;
      }
      query->backend_->WriteQuery(query->data_, query->present_bytes_, has_more_data);
      LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " phase 1 finished, phase 2 launched";
      return;
    } else if (query->phase_ == 1) {
      query->phase_ = 2;
      client_conn_->buffer()->dec_recycle_lock();
      LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " phase 2 finished";
    } else if (query->phase_ == 3) {
      if (query == tail_query_ && client_conn_->buffer()->parsed_unreceived_bytes() > 0) {
        LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " phase 3, TryReadMoreQuery";
        assert(!client_conn_->buffer()->recycle_locked());
        client_conn_->TryReadMoreQuery();
      } else {
        LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " phase 3 completed";
        query->phase_ = 4;
        backend->ReadReply();
      }
      return;  // TODO : should return here? 
    } else {
      LOG_ERROR << "OnWriteQueryFinished query=" << query->index_ << " phase=" << query->phase_;
      assert(false);
    }
  }

  // TODO : 从这里来看，应该是在write query完成之前，禁止client conn进一步的读取
  if (!client_conn_->buffer()->recycle_locked() // 全部完成write query 才能释放recycle_lock
      && (client_conn_->buffer()->parsed_unreceived_bytes() > 0
          || !QueryParsingComplete())) {
    LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " phase 3, read more query";
    client_conn_->TryReadMoreQuery();
  }

  if (query != tail_query_ || client_conn_->buffer()->parsed_unreceived_bytes() == 0) {
    LOG_DEBUG << "OnWriteQueryFinished query=" << query->index_ << " read reply, backend=" << backend;
    backend->ReadReply();
  }
}

bool RedisMsetCommand::QueryParsingComplete() {
  // assert(unparsed_bulks_ == 0);
  return unparsed_bulks_ == 0;
}

void RedisMsetCommand::ActivateWaitingSubquery() {
  LOG_DEBUG << "RedisMsetCommand ActivateWaitingSubquery begin, cmd=" << this;
  static const size_t MAX_ACTIVE_SUBQUERIES = 32;
  while(!waiting_subqueries_.empty() && pending_subqueries_.size() < MAX_ACTIVE_SUBQUERIES) {
    auto query = waiting_subqueries_.front();
    waiting_subqueries_.pop_front();

    assert(query->backend_ == nullptr);
    query->backend_ = AllocateBackend(query->backend_endpoint_);
    pending_subqueries_[query->backend_] = query;

    static const char MSET_PREFIX[] = "*3\r\n$4\r\nmset\r\n";

    LOG_DEBUG << "RedisMsetCommand WriteQuery ActivateWaitingSubquery, cmd=" << this
              << " query=" << query->index_
              << " backend=" << query->backend_ << ", key=("
              << redis::Bulk(query->data_, query->present_bytes_).to_string() << ")"
              << " k_v_present_bytes_=" << query->present_bytes_
              << " prefix=[" << MSET_PREFIX << "]"
              << " data=[" << std::string(query->data_, query->present_bytes_) << "]";

    // TODO : merge adjcent shared-endpoint queries
    query->backend_->WriteQuery(MSET_PREFIX, sizeof(MSET_PREFIX) - 1, true);
  }
}

bool RedisMsetCommand::ParseIncompleteQuery() {
  LOG_DEBUG << "RedisMsetCommand::QueryParsingComplete unparsed_bulks_=" << unparsed_bulks_;
  ReadBuffer* buffer = client_conn_->buffer();
  std::vector<redis::Bulk> new_bulks;
  size_t total_parsed = 0;

  while(new_bulks.size() < unparsed_bulks_ && total_parsed < buffer->unparsed_received_bytes()) {
    const char * entry = buffer->unparsed_data() + total_parsed;
    size_t unparsed_bytes = buffer->unparsed_received_bytes() - total_parsed;
    new_bulks.emplace_back(entry, unparsed_bytes);
    redis::Bulk& bulk = new_bulks.back();

    if (bulk.present_size() < 0) {
      LOG_DEBUG << "ParseIncompleteQuery sub_bulk error";
      return false;
    }

    if (bulk.present_size() == 0) {
      LOG_DEBUG << "ParseIncompleteQuery sub_bulk need more data";
      break;
    }
    total_parsed += bulk.total_size();
    LOG_DEBUG << "ParseIncompleteQuery parsed_bytes=" << bulk.total_size() << " total_parsed=" << total_parsed;
  }

  if (new_bulks.size() % 2 == 1) {
    total_parsed -= new_bulks.back().total_size();
    new_bulks.pop_back();
  }
  LOG_DEBUG << "ParseIncompleteQuery new_bulks.size=" << new_bulks.size() << " unparsed_bulks_=" << unparsed_bulks_;

  size_t to_process_bytes = 0;
  for(size_t i = 0; i + 1 < new_bulks.size(); i += 2) { 
    // TODO : limit max pending subqueries
    ip::tcp::endpoint ep = BackendLoactor::Instance().GetEndpointByKey(new_bulks[i].payload_data(), new_bulks[i].payload_size(), "REDIS_bj");
    LOG_DEBUG << "RedisMsetCommand ParseIncompleteQuery, key=" << new_bulks[i].to_string()
             << " v.present_size=[" << new_bulks[i + 1].present_size() << "] ep=" << ep;
    to_process_bytes += new_bulks[i].present_size();
    to_process_bytes += new_bulks[i + 1].present_size();

    client_conn_->buffer()->inc_recycle_lock();
    waiting_subqueries_.emplace_back(new Subquery(ep, 2, new_bulks[i].raw_data(),
                                     new_bulks[i].present_size() + new_bulks[i+1].present_size(),
                                     new_bulks[i+1].absent_size(), subquery_index_++));
    // TODO : 这里可能需要activate new subqueris
  }

  if (!waiting_subqueries_.empty()) {
    tail_query_ = waiting_subqueries_.back();
  }
  
  buffer->update_processed_bytes(to_process_bytes);
  buffer->update_parsed_bytes(total_parsed);
  unparsed_bulks_ -= new_bulks.size();
  ActivateWaitingSubquery();
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
  LOG_DEBUG << "RedisMsetCommand ParseReply resp.size=" << p - entry + 1
            << " contont=[" << std::string(entry, p - entry - 1) << "]"
            << " set_reply_recv_complete, backend=" << backend
            << " query=" << pending_subqueries_[backend]->index_;
  backend->set_reply_recv_complete();
  return true;
}

}

