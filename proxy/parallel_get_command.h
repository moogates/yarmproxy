

    ParallelGetCommand* parallel_cmd = new ParallelGetCommand(asio_service, owner, buf, cmd_len);
    parallel_cmd ->GroupGetKeys();
    (*cmd).reset(parallel_cmd);

class ParallelGetCommand : public MemcCommand {
public:
  ParallelGetCommand(boost::asio::io_service& io_service, std::shared_ptr<ClientConnection> owner, const char* buf, size_t size)
    : MemcCommand(io_service,
        MemcachedLocator::Instance().GetEndpointByKey("1"), // FIXME
        owner, buf, size) {
  }
  std::vector<std::shared_ptr<MemcCommand>> single_get_commands_;

  // TODO : refinement
  bool GroupGetKeys() {
    std::vector<std::string> keys;
    std::string key_list = cmd_line().substr(4, cmd_line().size() - 6);
    boost::split(keys, key_list, boost::is_any_of(" "), boost::token_compress_on);

    std::map<ip::tcp::endpoint, std::string> cmd_line_map;
    
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i].empty()) {
        continue;
      }
      ip::tcp::endpoint ep = MemcachedLocator::Instance().GetEndpointByKey(keys[i].c_str(), keys[i].size());

      auto it = cmd_line_map.find(ep);
      if (it == cmd_line_map.end()) {
        it = cmd_line_map.insert(make_pair(ep, std::string("get"))).first;
      }
      it->second += ' ';
      it->second += keys[i];
    }

    for (auto it = cmd_line_map.begin(); it != cmd_line_map.end(); ++it) {
      LOG_DEBUG << "GroupGetKeys " << it->first << " get_keys=" << it->second;
      it->second += "\r\n";
      std::shared_ptr<MemcCommand> cmd(new SingleGetCommand(io_service_, it->first, client_conn_, it->second.c_str(), it->second.size()));
      // fetching_cmd_set_.insert(cmd);
      single_get_commands_.push_back(cmd);
    }
    return true;
  }

  virtual void ForwardData(const char *, size_t) {
    for(auto cmd : single_get_commands_) {
      cmd->ForwardData(nullptr, 0);
    }
  }
};

