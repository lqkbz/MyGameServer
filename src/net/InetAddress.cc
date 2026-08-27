#include "net/InetAddress.h"

#include <arpa/inet.h>

#include <cstring>

namespace gs::net {

InetAddress::InetAddress(uint16_t port, const char* ip) {
  std::memset(&addr_, 0, sizeof addr_);
  addr_.sin_family = AF_INET;
  addr_.sin_port = htons(port);
  ::inet_pton(AF_INET, ip, &addr_.sin_addr);
}

std::string InetAddress::toIpPort() const {
  char buf[64];
  ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);
  return std::string(buf) + ":" + std::to_string(ntohs(addr_.sin_port));
}

}  // namespace gs::net
