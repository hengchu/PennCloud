#include <iostream>

// Include something from the library
#include <testmessage.pb.h>

int main(int argc, char *argv[])
{
  Person p;
  p.set_name("chewie");
  p.set_email("test@gmail.com");

  std::cout << p.IsInitialized() << std::endl;
  std::cout << p.DebugString() << std::endl;
}
