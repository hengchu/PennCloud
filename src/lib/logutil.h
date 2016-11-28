#ifndef INCLUDED_LOGUTIL
#define INCLUDED_LOGUTIL

#include <mutex>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <thread>

#define DECL_TIME				      \
  auto t = std::chrono::system_clock::now();	      \
  auto now = std::chrono::system_clock::to_time_t(t);

#define LOG_PREFIX				  \
  ss << std::put_time(std::gmtime(&now), "%c %Z") \
     << __FILE__				  \
     << ":"					  \
     << __LINE__				  \
     << "["					  \
     << std::this_thread::get_id()		  \
     << "]"

#define LOG_DEBUG	   \
  {			   \
    DECL_TIME		   \
    std::ostringstream ss; \
    do {		   \
      LOG_PREFIX	   \
        << "[DEBUG]";	   \
    {{{ ss

#define LOG_INFO	   \
  {			   \
    DECL_TIME		   \
    std::ostringstream ss; \
    do {		   \
      LOG_PREFIX	   \
        << "\033[1;32m [INFO]";	   \
    {{{ ss

#define LOG_WARN	   \
  {			   \
    DECL_TIME		   \
    std::ostringstream ss; \
    do {		   \
      LOG_PREFIX	   \
        << "\033[1;33m [WARN]";	   \
    {{{ ss

#define LOG_ERROR	   \
  {			   \
    DECL_TIME		   \
    std::ostringstream ss; \
    do {		   \
      LOG_PREFIX	   \
        << "\033[1;31m [ERROR]";   \
        {{{ ss

#define LOG_END					\
  "\033[0m" << std::endl; }}}			\
  } while (0);					\
  {						\
    LogUtil::log(ss.str());			\
  }						\
  }

struct LogUtil {
  // This struct provides a namespace for logging utility functions.
private:
  static bool       d_loggingEnabled;
  // Whether logging is enabled or not.
  
  static std::mutex d_stderrLock;
  // Lock to protect stderr.

public:
  static void enableLogging();
  // Enable logging.

  static void log(const std::string& output);
  // Log the output into stderr.
};

#endif
