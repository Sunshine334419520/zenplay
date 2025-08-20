#pragma once

#include <string>

#include "player/loader/resource_loader.h"

namespace zenplay {
class LocalFileLoader : public ResourceLoader {
 public:
  LocalFileLoader(const std::string& file_path);
  ~LocalFileLoader() override;

  std::vector<uint8_t> Read(size_t offset, size_t size) override;
  bool Seek(size_t position) override;

 private:
  std::string file_path_;
  FILE* file_handle_;
};

}  // namespace zenplay
