#ifndef INCLUDED_KVLOGMANAGER
#define INCLUDED_KVLOGMANAGER

#include <kvservicemessages.pb.h>

class KVLogManager {
  // This class defines an interface for a mechanism that manages
  // logs.

 public:
  virtual ~KVLogManager();
  // Destroy the log manager.

  virtual int numberOfLogEntries() = 0;
  // Return a count of the number of log entries.

  virtual int append(int                     term,
		     const KVServiceRequest& entry) = 0;
  // Append the entry with the given term, and returns its index.

  virtual void retrieve(int              *term,
			KVServiceRequest *entry,
			int               index) = 0;
  // Retrieves the log entry at the given index.
  // It's undefined behavior if index >= numberOfLogEntries();

  virtual void applyLog(int index) = 0;
  // Apply the log at the given index to the underlying storage.

  virtual void removeEntries(int index) = 0;
  // Remove all the entries at and after the given index.
};

#endif
