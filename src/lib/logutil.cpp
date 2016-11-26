#include <logutil.h>
#include <iostream>

// Initialize the control variable.
bool       LogUtil::d_loggingEnabled = false;
std::mutex LogUtil::d_stderrLock;

void LogUtil::enableLogging()
{
  d_loggingEnabled = true;
}

void LogUtil::log(const std::string& output)
{
  if (d_loggingEnabled) {
    d_stderrLock.lock();
    std::cerr << output << std::endl;
    d_stderrLock.unlock();
  }
}
