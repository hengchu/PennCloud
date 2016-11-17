# PennCloud
CIS 505 Final Project

-----

### How do I work on this?

The structure of the repository is like this:

```
.
├── LICENSE
├── README.md
├── build
│   ├── CMakeFiles.txt
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
