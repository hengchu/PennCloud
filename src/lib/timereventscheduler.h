#ifndef INCLUDED_TIMEREVENTSCHEDULER
#define INCLUDED_TIMEREVENTSCHEDULER

#include <thread>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <set>
#include <cstdint>
#include <atomic>

class TimerEventScheduler {
  // This class implements a timer.

 public:
  // TYPES
  using TimerHandle = uint64_t;
  using CallBack    = std::function<void()>;

 private:
  // TYPES
  using Clock     = std::chrono::steady_clock;
  using Timestamp = std::chrono::time_point<Clock>;
  using Duration  = std::chrono::milliseconds;

  struct TimerInstance {
    TimerHandle           d_handle;
    // The id of this timer instance.

    Timestamp             d_nextWakeUpTime;
    // When this instance will be next invoked.

    Duration              d_period;
    // The periodic duration this instance should be invoked.

    CallBack              d_callback;
    // The callback to invoke when time is up.

    bool                  d_running;
    // Whether this instance is currently running.
    
    TimerInstance(TimerHandle handle = 0)
    : d_handle(handle)
    , d_running(false)
    {
      // NOTHING
    }

    template<typename Function>
    TimerInstance(TimerHandle handle,
		  Timestamp   next,
		  Duration    period,
		  Function&&  handler)
    : d_handle(handle)
    , d_nextWakeUpTime(next)
    , d_period(period)
    , d_callback(handler)
    , d_running(false)
    {
      // NOTHING
    }

    TimerInstance(TimerInstance&& other)
    : d_handle(other.d_handle)
    , d_nextWakeUpTime(other.d_nextWakeUpTime)
    , d_period(other.d_period)
    , d_callback(std::move(other.d_callback))
    , d_running(other.d_running)
    {
      // NOTHING
    }

    TimerInstance& operator=(TimerInstance&& rhs)
    {
      if (this != &rhs) {
	d_handle = rhs.d_handle;
	d_nextWakeUpTime = rhs.d_nextWakeUpTime;
	d_period = rhs.d_period;
	d_callback = std::move(rhs.d_callback);
	d_running = rhs.d_running;
      }
      return *this;
    }
      
    TimerInstance(const TimerInstance& other) = delete;
    TimerInstance& operator=(const TimerInstance& rhs) = delete;
    // NOT IMPLEMENTED
  };

  struct TimerInstanceNextWakeUpComparator {
    bool operator()(const TimerInstance& lhs,
		    const TimerInstance& rhs) const
    {
      return lhs.d_nextWakeUpTime < rhs.d_nextWakeUpTime;
    }
  };
  
  using InstanceMap   = std::unordered_map<TimerHandle, TimerInstance>;
  using InstanceRef   = std::reference_wrapper<TimerInstance>;
  using InstanceQueue = std::multiset<InstanceRef, TimerInstanceNextWakeUpComparator>;
  
  
  // DATA
  std::mutex                  d_lock;
  // Lock to protect the internals of this timer scheduler.

  std::condition_variable     d_hasWork;
  // Condition variable used to wake up the internal thread.

  TimerHandle                 d_nextId;
  // Next id to assign to timer instances.

  InstanceMap                 d_activeInstances;
  // The instances.

  InstanceQueue               d_nextInstances;
  // The instances sorted by next wake up time.

  std::thread                 d_thread;
  // The internal worker thread.

  std::atomic_bool            d_done;
  // Whether this timer event scheduler is done or not.

  // PRIVATE FUNCTIONS
  void threadLoop();
  // The main thread loop.

  TimerHandle scheduleImpl(TimerInstance&& instance);
  // The internals of scheduling an event.
  
 public:
  TimerEventScheduler();
  // Create a timer event scheduler.
  
  ~TimerEventScheduler();
  // Destroy this timer event scheduler.

  TimerHandle
    schedule(uint64_t        millisecondsFromNow,
	     uint64_t        everyMilliseconds,
	     const CallBack& callback);
  // Schedule an event that will be triggered millisecondsFromNow, and
  // repeats everyMilliseconds. Set everyMilliseconds = 0 if you don't
  // want this event to be repeated.
  
  TimerHandle
    schedule(uint64_t   millisecondsFromNow,
	     uint64_t   everyMilliseconds,
	     CallBack&& callback);
  // Schedule an event that will be triggered millisecondsFromNow, and
  // repeats everyMilliseconds. Set everyMilliseconds = 0 if you don't
  // want this event to be repeated.

  bool cancel(TimerHandle handle);
  // Cancel the event associated with the given handle. Returns true
  // if successful, otherwise return false.
};

#endif
