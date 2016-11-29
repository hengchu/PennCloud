#include <kvstore.h>
#include <cassert>

KVStore::KVStore()
  : d_data()
  , d_dataLock()
{
  // NOTHING
}

void
KVStore::put(const std::string& column,
	     const std::string& row,
	     const std::string& value)
{
  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_dataLock);

    KVKey key;
    key.d_column = column;
    key.d_row    = row;

    d_data[key] = value;
  }
  // UNLOCK
}

int
KVStore::get(std::string        *value,
	     const std::string&  column,
	     const std::string&  row)
{
  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_dataLock);

    KVKey key;
    key.d_column = column;
    key.d_row    = row;
    
    auto it = d_data.find(key);

    if (it == d_data.end()) {
      return -1;
    } else {
      *value = it->second;
      return 0;
    }
  }
  // UNLOCK
}

int
KVStore::compareAndPut(const std::string& column,
		       const std::string& row,
		       const std::string& oldValue,
		       const std::string& newValue)
{
  // LOCK
  {
    std::lock_guard<std::mutex> guard(d_dataLock);

    KVKey key;
    key.d_column = column;
    key.d_row    = row;
    
    auto it = d_data.find(key);

    if (it == d_data.end()) {
      // NOT FOUND
      return -1;
    } else if (it->second != oldValue) {
      // NOT SAME
      return -2;
    } else if (it->second == oldValue) {
      it->second = newValue;
      return 0;
    } else {
      // Should be unreachable.
      assert(false);
      return -3;
    }
  }
  // UNLOCK
}

int
KVStore::deleteValue(const std::string& column,
		     const std::string& row)
{
    // LOCK
  {
    std::lock_guard<std::mutex> guard(d_dataLock);

    KVKey key;
    key.d_column = column;
    key.d_row    = row;
    
    auto it = d_data.find(key);

    if (it == d_data.end()) {
      return -1;
    } else {
      d_data.erase(it);
      return 0;
    }
  }
  // UNLOCK
}
