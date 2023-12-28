#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <iostream>

class DRAMMem {
public:
  DRAMMem(const std::string &filepath, size_t size)
      : filepath_(filepath), size_(size), fd_(-1), addr_(nullptr) {
    // Open the file
    fd_ = open(filepath.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd_ == -1) {
      throw std::runtime_error("Error opening file: " + filepath);
    }

    if (ftruncate(fd_, size) == -1) {
      close(fd_);
      throw std::runtime_error("Error setting file size: " + filepath);
    }

    addr_ = static_cast<char *>(
        mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (addr_ == MAP_FAILED) {
      close(fd_);
      throw std::runtime_error("Error mapping file: " + filepath);
    }
  }

  ~DRAMMem() {
    if (addr_ != nullptr) {
      munmap(addr_, size_);
    }
    if (fd_ != -1) {
      close(fd_);
    }
  }

  template <typename T> T *accessData(size_t offset = 0) {
    checkOffset(offset, sizeof(T));
    return reinterpret_cast<T *>(addr_ + offset);
  }

  char *getRawData(size_t offset = 0) {
    checkOffset(offset, 1); 
    return addr_ + offset;
  }

private:
  std::string filepath_;
  size_t size_;
  int fd_;
  char *addr_;

  void checkOffset(size_t offset, size_t size) const {
    if (offset + size > size_) {
      throw std::runtime_error("Offset and size exceed mapped region");
    }
  }
};



struct SomeData {
    
};

int main() {
    try {
        DRAMMem manager("/tmp/shm", 1024 * 1024); // 1 MB

        SomeData* dataPtr = manager.accessData<SomeData>(0);

        char* rawData = manager.getRawData(sizeof(SomeData));

    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    return 0;
}
