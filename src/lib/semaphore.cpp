#include <semaphore.h>

Semaphore::Semaphore(int count)
  : d_mutex()
  , d_cv()
  , d_count(count)
{
  // NOTHING
}

void
Semaphore::post()
{
  std::unique_lock<std::mutex> lock(d_mutex);
  d_count++;
  d_cv.notify_one();
}

void
Semaphore::wait()
{
  std::unique_lock<std::mutex> lock(d_mutex);
  while (d_count == 0) {
    d_cv.wait(lock);
  }
  d_count--;
}
