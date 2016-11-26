#ifndef INCLUDED_PROTOUTIL
#define INCLUDED_PROTOUTIL

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>

struct ProtoUtil {
  // This namespace provides a few useful functions for dealing with
  // protobuf messages.

  bool writeDelimitedTo(const google::protobuf::Message&            message,
			google::protobuf::io::ZeroCopyOutputStream* rawOutput);
  // Write the given message to the output stream. Return true on
  // success, false otherwise.

  bool readDelimitedFrom(google::protobuf::io::ZeroCopyInputStream* rawInput,
			 google::protobuf::Message*                 message);
  // Read a message out of the given rawInput. Return true on success,
  // false otherwise.
  
};

#endif
