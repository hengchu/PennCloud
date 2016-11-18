# PennCloud
CIS 505 Final Project

-----

### How do I initialize the build system?

Note: You only need to do the following once! (The first time you
setup the directory).

Install these following tools on your system if you haven't already:
```
sudo apt-get install autoconf automake libtool curl make g++ unzip cmake
```

Now, run the following commands to setup the source directory and the
build directory.

```
git clone --recursive git@github.com:hengchu/PennCloud.git
cd PennCloud
./initbuild.sh
```

If everything goes well, now in the build folder, you can run the command

```
make
```

or

```
make http
make kv
make storage
make webmail
make clean
```

For each individual targets.

**protobuf**: I have included a very basic example of what a
  `protobuf` message definition looks like in the `src/http`
  folder. Basically, you would need to define your own `.proto` files
  for new message formats. The build system is setup so that cmake
  will automatically pickup your `.proto` files and generate
  corresponding `.pb.h` and `.pb.cc` files when you run `make` in the
  `build` folder, the include flags are also setup automatically so
  you can just write stuff like `#include <testmessage.pb.h>` and
  it'll just work.

Let Hengchu know if you run into any trouble!

-----

### How do I work on this?

The structure of the repository is like this:

```
.
├── LICENSE
├── README.md
├── build
│   ├── All kinds of build artifacts
└── src
    ├── http
    ├── kv
    ├── lib
    ├── storage
    └── webmail
```

All the source code is located in the `src` folder, with one folder
for each major component `http`, `kv`, `storage` and `webmail`. The
`lib` folder contains code for common infrastructural code that's
shared across all the other components.

Note that since each of the major component will be built into an
executable, each of `http`, `kv`, `storage` and `webmail` already has
a `main.cpp` file in it, that holds the `main()` function.

The `lib` folder will be built into a static library that's linked to
the executables.

Now, let's say you're working on the `http` component, and you want to
add two extra files `server.h` and `server.cpp` to that
component. You'd run the following sequence of commands in the
`src/http` folder.

```
# Go to the http directory
cd src/http

# Create the files I want
touch server.h server.cpp

# Edit the files
emacs server.h
emacs server.cpp

# I also want to use the new interface defined in server.h here so I
# put an '#include <server.h>' in main.cpp.
emacs main.cpp

# Go to the build directory
cd build

# Generate Makefile
cmake .

# Build http executable
make http

# Clean the build directory
make clean
```

If you have any questions/advice/suggestion regarding adding files/how
the build system works, feel free to talk to Hengchu Zhang.

-----

### Message formats

We'll use `protobuf`, check this tutorial out :)

https://developers.google.com/protocol-buffers/docs/cpptutorial
