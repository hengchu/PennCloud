#include <iostream>

// Include something from the library
#include <test.h>
#include <testmessage.pb.h>

int main(int argc, char *argv[])
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  
  std::cout << "Hello, world!"
	    << std::endl;

  TestUtil::sayHello();

  Person p;
  std::string name = "hengchu";
  std::string email = "hello@gmail.com";
  p.set_name(name);
  p.set_email(email);

  std::cout << p.IsInitialized() << std::endl;
}
