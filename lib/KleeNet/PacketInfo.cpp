#include "PacketInfo.h"

using namespace kleenet;

PacketInfo::PacketInfo(uint64_t addr, uint64_t offset, size_t length, klee::MemoryObject const* destMo, net::Node src, net::Node dest)
  : addr(addr), offset(offset), length(length), destMo(destMo), src(src), dest(dest) {
}

bool PacketInfo::operator<(PacketInfo const& pi) const {
#define CHECK(ARG) \
  if (ARG != pi.ARG) \
    return ARG < pi.ARG
  CHECK(addr);
  CHECK(offset);
  CHECK(length);
  CHECK(destMo);
  CHECK(src);
  CHECK(dest);
#undef CHECK
  return false; // because they're equal
}

PacketInfo::operator net::Node() const {
  return dest;
}
