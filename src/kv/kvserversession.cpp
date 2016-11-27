#include <kvserversession.h>
#include <logutil.h>
#include <sys/socket.h>
#include <unistd.h>
#include <kvprotocol.pb.h>
#include <protoutil.h>

KVServerSession::KVServerSession(int socket)
  : d_socket(socket)
  , d_thread()
  , d_running(false)
  , d_inputStream(socket)
{
  // NOTHING
}

void
KVServerSession::threadLoop()
{
  while (d_running) {
    /*
    KVServerNegotiation msg;
    bool success = ProtoUtil::readDelimitedFrom(&d_inputStream,
						&msg);
    */
    LOG_INFO << "Do something..."
	     << LOG_END;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

int
KVServerSession::start()
{
  d_running = true;
  d_thread = std::thread(&KVServerSession::threadLoop, this);
  return 0;
}

int
KVServerSession::stop()
{
  // Avoid double stopping.
  if (!d_running) {
    return -1;
  }
  
  d_running = false;

  int rc = shutdown(d_socket, SHUT_RDWR);

  if (0 != rc) {
    LOG_ERROR << "Failed to shutdown server session socket"
	      << ", error = "
	      << std::strerror(errno)
	      << LOG_END;
  }
  
  d_thread.join();
  close(d_socket);
  return 0;
}
