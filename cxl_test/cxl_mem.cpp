#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

class MemoryMapper {
public:
//"/dev/dax0.0"
  MemoryMapper(const std::string &filename, size_t size)
      : fd_(-1), addr_(nullptr), size_(size) {
    fd_ = open(filename.c_str(), O_RDWR);
    if (fd_ == -1) {
      throw std::runtime_error("Error opening file: " + filename);
    }

    addr_ = static_cast<char *>(
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (addr_ == MAP_FAILED) {
      close(fd_);
      throw std::runtime_error("Error mapping file: " + filename);
    }
  }

  ~MemoryMapper() {
    if (addr_ != nullptr) {
      munmap(addr_, size_);
    }
    if (fd_ != -1) {
      close(fd_);
    }
  }

  char *getAddr() const { return addr_; }
  size_t getSize() const { return size_; }

  MemoryMapper(const MemoryMapper &) = delete;
  MemoryMapper &operator=(const MemoryMapper &) = delete;

private:
  int fd_;
  char *addr_;
  size_t size_;
};

class CXLMem {
public:
  explicit CXLMem(const MemoryMapper &mapper) : mapper_(mapper) {}

  template <typename T> T *accessData(size_t offset = 0) {
    checkOffset(offset, sizeof(T));
    return reinterpret_cast<T *>(mapper_.getAddr() + offset);
  }

  char *getRawData(size_t offset = 0) {
    checkOffset(offset, 1); // Minimal check for offset
    return mapper_.getAddr() + offset;
  }

private:
  const MemoryMapper &mapper_;

  void checkOffset(size_t offset, size_t size) const {
    if (offset + size > mapper_.getSize()) {
      throw std::runtime_error("Offset and size exceed mapped region");
    }
  }
};
