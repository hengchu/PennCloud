#ifndef INCLUDED_SEMAPHORE
#define INCLUDED_SEMAPHORE

#include <mutex>
#include <condition_variable>

class Semaphore {
  // Somehow c++11 doesn't even have semaphores... So here is one.

 private:
  // DATA
  std::mutex                d_mutex;

  std::condition_variable   d_cv;

  int                       d_count;

 public:
  Semaphore(int count = 0);
  
  Semaphore(const Semaphore& other) = delete;
  Semaphore& operator=(const Semaphore& rhs) = delete;
  // NOT IMPLEMENTED.

  void post();
  void wait();
};

#endif
