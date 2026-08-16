#pragma once

#ifdef CROSSPOINT_SIM_GRPC

#include <cstdint>
#include <vector>

namespace SimGrpc {

bool encodeGrayPng(const uint8_t *gray, int width, int height,
                   std::vector<uint8_t> *out);

} // namespace SimGrpc

#endif
