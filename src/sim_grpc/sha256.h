#pragma once

#ifdef CROSSPOINT_SIM_GRPC

#include <cstddef>
#include <cstdint>
#include <string>

namespace SimGrpc {

std::string sha256Hex(const uint8_t *data, size_t len);

} // namespace SimGrpc

#endif
