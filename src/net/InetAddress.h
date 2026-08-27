#pragma once
#include <netinet/in.h>

#include <string>

namespace gs::net {

// sockaddr_in 的薄封装，只支持 IPv4（YAGNI）
class InetAddress {
 public:
  explicit InetAddress(uint16_t port, const char* ip = "0.0.0.0");
  explicit InetAddress(const sockaddr_in& addr) : addr_(addr) {}
  std::string toIpPort() const;
  const sockaddr* sockAddr() const {
    return reinterpret_cast<const sockaddr*>(&addr_);
  }
  sockaddr_in& raw() { return addr_; }

 private:
  sockaddr_in addr_{};
};

}  // namespace gs::net
