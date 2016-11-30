#include <timereventscheduler.h>

using UniqueLock = std::unique_lock<std::mutex>;

TimerEventScheduler::TimerEventScheduler()
  : d_lock()
  , d_hasWork()
  , d_nextId(0)
  , d_activeInstances()
  , d_nextInstances(TimerInstanceNextWakeUpComparator())
  , d_thread()
  , d_done(false)
{
  UniqueLock lock(d_lock);
  d_thread = std::thread(&TimerEventScheduler::threadLoop, this);
}

TimerEventScheduler::~TimerEventScheduler()
{
  UniqueLock lock(d_lock);
  d_done = true;
  d_hasWork.notify_all();
  lock.unlock();
  d_thread.join();
}

TimerEventScheduler::TimerHandle
TimerEventScheduler::schedule(uint64_t        millisecondsFromNow,
			      uint64_t        everyMilliseconds,
			      const CallBack& callback)
{
  return scheduleImpl(TimerInstance(0,
				    Clock::now() + Duration(millisecondsFromNow),
				    Duration(everyMilliseconds),
				    callback));
}

TimerEventScheduler::TimerHandle
TimerEventScheduler::schedule(uint64_t   millisecondsFromNow,
			      uint64_t   everyMilliseconds,
			      CallBack&& callback)
{
  return scheduleImpl(TimerInstance(0,
				    Clock::now() + Duration(millisecondsFromNow),
				    Duration(everyMilliseconds),
				    callback));
}

TimerEventScheduler::TimerHandle
TimerEventScheduler::scheduleImpl(TimerInstance&& instance)
{
  UniqueLock lock(d_lock);
  instance.d_handle = d_nextId++;
  auto it = d_activeInstances.emplace(instance.d_handle, std::move(instance));
  d_nextInstances.insert(it.first->second);
  d_hasWork.notify_all();
  return instance.d_handle;
}

bool
TimerEventScheduler::cancel(TimerHandle handle)
{
  UniqueLock lock(d_lock);
  
  auto it = d_activeInstances.find(handle);
  if (it == d_activeInstances.end()) {
    return false;
  } else if (it->second.d_running) {
    // The callback is being invoked currently.
    // Mark this for deletion later.
    it->second.d_running = false;
  } else {
    d_nextInstances.erase(std::ref(it->second));
    d_activeInstances.erase(it);
  }

  d_hasWork.notify_all();
  return true;
}

void
TimerEventScheduler::threadLoop()
{
  UniqueLock lock(d_lock);

  while (!d_done) {
    // Wait until we have some instances to run.
    if (d_nextInstances.empty()) {
      d_hasWork.wait(lock);
    } else {

      // Grab the first instance sorted by wake up time.
      auto firstInstance = d_nextInstances.begin();
      TimerInstance& instance = *firstInstance;

      // Check if the scheduled time has already passed.
      auto now = Clock::now();
      if (now > instance.d_nextWakeUpTime) {
	// Remove this instance from the queue.
	d_nextInstances.erase(firstInstance);
	instance.d_running = true;

	// Run the callback.
	lock.unlock();
	instance.d_callback();
	lock.lock();

	// We could have stopped the scheduler already when the
	// callback was running.
	if (d_done) {
	  break;
	} else if (!instance.d_running) {
	  d_activeInstances.erase(instance.d_handle);
	} else {
	  instance.d_running = false;

	  if (instance.d_period.count() > 0) {
	    instance.d_nextWakeUpTime += instance.d_period;
	    d_nextInstances.insert(instance);
	  } else {
	    d_activeInstances.erase(instance.d_handle);
	  }
	}
      } else {
	d_hasWork.wait_until(lock, instance.d_nextWakeUpTime);
      }
    }
  }
}
