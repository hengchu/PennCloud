# PennCloud
CIS 505 Final Project

-----

### Message formats

This section describes a message format that is sufficient and
flexible enough for communication between servers of the same service
and servers across components.

This is the dependency between components

```
HTTP --> Email --> Storage --> KV Store
  |                   ^           ^
  |                   |           |
  .___________________.___________.

```

And since KV storage will have multiple nodes holding a replicated
database, there will also be communication among those nodes.

The proposed message format is the following.

```
+-----------------------------+-----------------------------+
|0|1|2|3|4|5|6|7|8|9|a|b|c|d|e|0|1|2|3|4|5|6|7|8|9|a|b|c|d|e|
+-----------------------------+-----------------------------+
|                        MESSAGE SIZE                       |
+-----------------------------------------------------------+
|                        MESSAGE TYPE                       |
+-----------------------------------------------------------+
|                        MESSAGE BODY                       |
|                            ...                            |
|                            ...                            |
+-----------------------------------------------------------+
```

- Message size: 4 bytes, this is the size of the entire message, from
  the beginning of Message Size itself to the last byte of Message
  body.

- Message type: 4 bytes, this field encodes the different types of
  messages we would like to use.

- Message body: a raw sequence of bytes, the size of the body is
  `MSGSIZE - 4 - 4`.

### Programming Interface of Messages

```

class Message {

      enum MessageType {
      	   KV_PUT
	 , KV_GET
	 , KV_CGET
	 , KV_DELETE
	 , ...
      };

      MessageType type();
      // Returns the type of the message

      uint32_t    rawSize();
      // Returns the size of the entire message.

      uint32_t    size();
      // Returns the size of the message sans field for Message Size and Message Type.
      // AKA the size of the part that the application actually cares about.

      char       *bytes();
      // Returns the raw bytes of this message.

      template<typename T>
      T           bytesAs();
      // Return the raw bytes of this message interpreted as the given type T.
};

class MessageBuilder {
      // Use this class to build messages.

      Message pack(MessageType type, char *bytes, uint32_t size);
      // Returns a packed message with the given type, bytes as content.
      // Note that size MUST be the size of the bytes buffer.

      template<typename T>
      Message pack(MessageType type, T content);
      // Pack the given value of type T into the message.
      // Note that the T type must be memcpy-able.
};

class Channel {
      // A communication channel that is based on TCP, in order to
      send/recv messages.

      int send(Message msg);      
      // Send the given message, return 0 on success, and a non-zero
      error code on failure.

      Message recv();
      // Blocking read until a message comes in and returns that message.

      int recvTimeout(Message *msg,
		      int      milliseconds);		    
      // Blocking read until either a message comes in, or the
      specified number of milliseconds // have already passed.
      
};

```