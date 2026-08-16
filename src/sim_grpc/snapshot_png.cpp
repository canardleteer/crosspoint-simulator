#ifdef CROSSPOINT_SIM_GRPC

#include "snapshot_png.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../stb_image_write.h"

namespace SimGrpc {
namespace {

void pngAppend(void *context, void *data, int size) {
  auto *out = static_cast<std::vector<uint8_t> *>(context);
  const auto *bytes = static_cast<const uint8_t *>(data);
  out->insert(out->end(), bytes, bytes + size);
}

} // namespace

bool encodeGrayPng(const uint8_t *gray, int width, int height,
                   std::vector<uint8_t> *out) {
  if (!gray || !out || width <= 0 || height <= 0) {
    return false;
  }
  out->clear();
  return stbi_write_png_to_func(pngAppend, out, width, height, 1, gray,
                                width) != 0;
}

} // namespace SimGrpc

#endif
