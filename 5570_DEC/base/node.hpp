#pragma once

#include <sstream>
#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <set>
#include <map>

#include "glog/logging.h"

namespace csci5570 {

struct Node {
  uint32_t id;
  std::string hostname;
  int port;

  std::string DebugString() const {
    std::stringstream ss;
    ss << "Node: { id=" << id << " hostname=" << hostname << " port=" << port << " }";
    return ss.str();
  }
  bool operator==(const Node& other) const {
    return id == other.id && hostname == other.hostname && port == other.port;
  }
};

  
}  // namespace csci5570
