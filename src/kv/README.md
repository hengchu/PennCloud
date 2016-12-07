### How to use the KV servers.

TLDR: `#include <kvapi.h>` and use its public interface.

This file is located in `src/lib/kvapi.h`.

-----

1. First, run `make kv` to build the server.

2. Create a `kvconfig` file.

Here's an example one.

```
servers {
  listen_addr {
    ip_address: "127.0.0.1"
    port: 2500
  }

  client_addr {
    ip_address: "127.0.0.1"
    port: 3500
  }

  log_file_path: "./server0.dat"
}

servers {
  listen_addr {
    ip_address: "127.0.0.1"
    port: 2501
  }

  client_addr {
    ip_address: "127.0.0.1"
    port: 3501
  }

  log_file_path: "./server1.dat"
}

servers {
  listen_addr {
    ip_address: "127.0.0.1"
    port: 2502
  }

  client_addr {
    ip_address: "127.0.0.1"
    port: 3502
  }

  log_file_path: "./server2.dat"
}

servers {
  listen_addr {
    ip_address: "127.0.0.1"
    port: 2503
  }

  client_addr {
    ip_address: "127.0.0.1"
    port: 3503
  }

  log_file_path: "./server3.dat"
}
```

The configuration file simply lists the client and server addresses
used for communication.

4. The `kvapi.h` file defines `KVSession` class. Its constructor takes
the IP address and a port number. Use the `client_addr` you have
defined in the configuration file for connecting to the server.

5. Run the following sequence of commands:

```
./kv -c ./kvconfig -s 0
./kv -c ./kvconfig -s 1
./kv -c ./kvconfig -s 2
./kv -c ./kvconfig -s 3
```

Note that the number passed to the `-s` corresponds to the config
defined for the server at that given in the configuration file, and
this sequence of commands corresponds to the example configuraiton
file above.

-----

Note that there is an executable called `kvtool` that can be built
with `make kvtool`. You can use it to interact with the KV storage
servers on the command line.

For example:

```
./kvtool -m put -c "abc" -r "cba" -v "hello, world" 127.0.0.1 3500
```

Would `put` the value `hello, world` in column `abc` and row `cba`.

You can later retrieve the particular column and row with the command.

```
./kvtool -m get -c "abc" -r "cba" 127.0.0.1 3500
```