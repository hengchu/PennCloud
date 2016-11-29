#ifndef INCLUDED_KVSTORE
#define INCLUDED_KVSTORE

#include <mutex>
#include <unordered_map>
#include <string>

class KVStore {
  // This class implements a mechanism that is the actual storage of
  // KV pairs. All functions on this mechanism is thread-safe.

 private:
  // TYPES
  struct KVKey {
    std::string d_column;
    std::string d_row;
  };

  struct KVKeyHash {
    size_t operator()(const KVKey& k) const
    {
      using StrHash = std::hash<std::string>;

      return StrHash()(k.d_column) ^ StrHash()(k.d_row);
    }
  };

  struct KVKeyEqual {
    bool operator()(const KVKey& lhs, const KVKey& rhs) const
    {
      return lhs.d_column == rhs.d_column && lhs.d_row == rhs.d_row;
    }
  };
  
  using KVMap = std::unordered_map<KVKey, std::string, KVKeyHash, KVKeyEqual>;

  // DATA

  KVMap                       d_data;
  // The data that is stored in this storage.

  std::mutex                  d_dataLock;
  // The mutex used to protect d_data.

 public:
  KVStore();
  // Create a storage.

  KVStore(const KVStore& other) = delete;
  KVStore& operator=(const KVStore& other) = delete;
  // NOT IMPLEMENTED.

  void put(const std::string& column,
	   const std::string& row,
	   const std::string& value);
  // Put the given value.

  int get(std::string        *value,
	  const std::string&  column,
	  const std::string&  row);
  // Get the given value if it exists and return 0.
  // Otherwise return a non-zero error code.

  int compareAndPut(const std::string& column,
		    const std::string& row,
		    const std::string& oldValue,
		    const std::string& newValue);
  // Compare and set (column, row) to new value only if the oldValue
  // is equal to the given one. Return 0 if the operation overwrites
  // the value.
  // Return a non-zero code if the old value doesn't exist
  // or isn't the specified one.

  int deleteValue(const std::string& column,
		  const std::string& row);
  // Delete the given column and row from the storage. Returns 0 on
  // successful deletion, return a non-zero code if the (column, row)
  // pair doesn't exist.
  
};

#endif
