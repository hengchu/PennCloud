#include <iostream>

// Include something from the library
#include <test.h>

int main(int argc, char *argv[])
{
  std::cout << "Hello, world!"
	    << std::endl;

  TestUtil::sayHello();
}
