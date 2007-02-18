#include "mplexer.hh"
#include "sstuff.hh"
#include <iostream>
#include <unistd.h>
#include "misc.hh"
#include <boost/lexical_cast.hpp>
#include "syncres.hh"

#if defined(__sun__) && defined(__svr4__)
#include <port.h>
#include <sys/port_impl.h>
#endif

using namespace boost;
using namespace std;

class PortsFDMultiplexer : public FDMultiplexer
{
public:
  PortsFDMultiplexer();
  virtual ~PortsFDMultiplexer()
  {
    close(d_portfd);
  }

  virtual int run(struct timeval* tv);

  virtual void addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, const boost::any& parameter);
  virtual void removeFD(callbackmap_t& cbmap, int fd);
  string getName()
  {
    return "solaris completion ports";
  }
private:
  int d_portfd;
  boost::shared_array<port_event_t> d_pevents;
  static int s_maxevents; // not a hard maximum
};


static FDMultiplexer* makePorts()
{
  return new PortsFDMultiplexer();
}

static struct PortsRegisterOurselves
{
  PortsRegisterOurselves() {
    FDMultiplexer::getMultiplexerMap().insert(make_pair(0, &makePorts)); // priority 0!
  }
} doItPorts;


int PortsFDMultiplexer::s_maxevents=1024;
PortsFDMultiplexer::PortsFDMultiplexer() : d_pevents(new port_event_t[s_maxevents])
{
  d_portfd=port_create(); // not hard max
  if(d_portfd < 0)
    throw FDMultiplexerException("Setting up port: "+stringerror());
}

void PortsFDMultiplexer::addFD(callbackmap_t& cbmap, int fd, callbackfunc_t toDo, const boost::any& parameter)
{
  accountingAddFD(cbmap, fd, toDo, parameter);

  if(port_associate(d_portfd, PORT_SOURCE_FD, fd, (&cbmap == &d_readCallbacks) ? POLLIN : POLLOUT, 0) < 0) {
    cbmap.erase(fd);
    throw FDMultiplexerException("Adding fd to port set: "+stringerror());
  }
}

void PortsFDMultiplexer::removeFD(callbackmap_t& cbmap, int fd)
{
  if(!cbmap.erase(fd))
    throw FDMultiplexerException("Tried to remove unlisted fd "+lexical_cast<string>(fd)+ " from multiplexer");

  if(port_dissociate(d_portfd, PORT_SOURCE_FD, fd) < 0)
    throw FDMultiplexerException("Removing fd from port set: "+stringerror());
}

int PortsFDMultiplexer::run(struct timeval* now)
{
  if(d_inrun) {
    throw FDMultiplexerException("FDMultiplexer::run() is not reentrant!\n");
  }
  
  struct timespec timeout;
  timeout.tv_sec=0; timeout.tv_nsec=500000000;
  unsigned int numevents=1;
  int ret= port_getn(d_portfd, d_pevents.get(), min(PORT_MAX_LIST, s_maxevents), &numevents, &timeout);

  gettimeofday(now,0);
  
  if(ret < 0 && errno!=EINTR && errno!=ETIME)
    throw FDMultiplexerException("completion port_getn returned error: "+stringerror());

  if((ret < 0 && errno==ETIME) || numevents==0) // nothing
    return 0;

  d_inrun=true;

  for(unsigned int n=0; n < numevents; ++n) {
    d_iter=d_readCallbacks.find(d_pevents[n].portev_object);
    
    if(d_iter != d_readCallbacks.end()) {
      d_iter->second.d_callback(d_iter->first, d_iter->second.d_parameter);
      if(d_readCallbacks.count(d_pevents[n].portev_object) && port_associate(d_portfd, PORT_SOURCE_FD, d_pevents[n].portev_object, 
			POLLIN, 0) < 0)
	throw FDMultiplexerException("Unable to add fd back to ports (read): "+stringerror());
    }

    d_iter=d_writeCallbacks.find(d_pevents[n].portev_object);
    
    if(d_iter != d_writeCallbacks.end()) {
      d_iter->second.d_callback(d_iter->first, d_iter->second.d_parameter);
      if(d_writeCallbacks.count(d_pevents[n].portev_object) && port_associate(d_portfd, PORT_SOURCE_FD, d_pevents[n].portev_object, 
			POLLOUT, 0) < 0)
	throw FDMultiplexerException("Unable to add fd back to ports (write): "+stringerror());
    }

  }

  d_inrun=false;
  return 0;
}

#if 0
void acceptData(int fd, boost::any& parameter)
{
  cout<<"Have data on fd "<<fd<<endl;
  Socket* sock=boost::any_cast<Socket*>(parameter);
  string packet;
  IPEndpoint rem;
  sock->recvFrom(packet, rem);
  cout<<"Received "<<packet.size()<<" bytes!\n";
}


int main()
{
  Socket s(InterNetwork, Datagram);
  
  IPEndpoint loc("0.0.0.0", 2000);
  s.bind(loc);

  PortsFDMultiplexer sfm;

  sfm.addReadFD(s.getHandle(), &acceptData, &s);

  for(int n=0; n < 100 ; ++n) {
    sfm.run();
  }
  sfm.removeReadFD(s.getHandle());
  sfm.removeReadFD(s.getHandle());
}
#endif

