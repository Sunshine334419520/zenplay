#pragma once

#include <vector>

namespace zenplay {

class ResourceLoader {
 public:
  ResourceLoader() = default;
  virtual ~ResourceLoader() = default;

  virtual std::vector<uint8_t> Read(size_t offset, size_t size) = 0;
  virtual bool Seek(size_t position) = 0;
};

}  // namespace zenplay
