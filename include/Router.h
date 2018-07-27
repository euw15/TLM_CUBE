#ifndef TLM_ROUTER_H_
#define TLM_ROUTER_H_

#include <systemc.h>
#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/peq_with_cb_and_phase.h"
#include <queue>

using namespace sc_core;   
using namespace sc_dt;   
using namespace std;   


/*Enum that handle the directions the reqquest can do*/
enum class RequestDirection{
  MOVE_X,
  MOVE_Y,
  MOVE_Z,
  NO_MOVE
};

struct MessageInfo
{
  tlm::tlm_generic_payload* transaction;
  tlm::tlm_phase phase;
  sc_time delay;
};

struct ID_extension: tlm::tlm_extension<ID_extension> {
  ID_extension() : transaction_id(0) {}
  virtual tlm_extension_base* clone() const { // Must override pure virtual clone method
    ID_extension* t = new ID_extension;
    t->transaction_id = this->transaction_id;
    return t;
  }

  // Must override pure virtual copy_from method
  virtual void copy_from(tlm_extension_base const &ext) {
    transaction_id = static_cast<ID_extension const &>(ext).transaction_id;
  }
  unsigned int transaction_id;
};

     /* Target memory*/  
struct Null_Port: sc_module   
{   
  /*Multiple targets but only target x is used*/
  tlm_utils::simple_target_socket<Null_Port> NullSocket;

  SC_CTOR(Null_Port)   
  : NullSocket("NullSocket")  
  {        
  }   
  
}; 

class Router : public sc_module
{
  public:

  /*Constructor*/
  	SC_CTOR(Router): xInitSocket("xInitSocket"),xTargetSocket("xTargetSocket"),
    yInitSocket("yInitSocket"),yTargetSocket("yTargetSocket"),
    zInitSocket("zInitSocket"),zTargetSocket("zTargetSocket"),
    memorySocket("memorySocket")
    {   
	    // Register callbacks for incoming interface method calls
	    xInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
	    xTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
	    yInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
	    yTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
	    zInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
	    zTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
	    memorySocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
	    SC_THREAD(fordwardThread);  
	    SC_THREAD(backwardThread);
	    SC_THREAD(createRequestsThread);
	}  
  /*Queue with the transtions pending*/
  std::queue<MessageInfo> fordwardQueue;
  std::queue<MessageInfo> backwardQueue;

  /*Init and target socket use to send and recibe messages*/
  tlm_utils::simple_initiator_socket<Router>  xInitSocket;
  tlm_utils::simple_initiator_socket<Router>  yInitSocket; 
  tlm_utils::simple_initiator_socket<Router>  zInitSocket; 
  tlm_utils::simple_target_socket<Router>     xTargetSocket;
  tlm_utils::simple_target_socket<Router>     yTargetSocket;
  tlm_utils::simple_target_socket<Router>     zTargetSocket;
  
  tlm_utils::simple_initiator_socket<Router>  memorySocket; 

  virtual tlm::tlm_sync_enum nb_transport_fw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay );
  virtual tlm::tlm_sync_enum nb_transport_bw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay );
  void setId(short id);

  protected:
  /*Event used to notify the thread it has to iterate*/
  sc_event  fordwardEvent;
  sc_event  backwardEvent;
  sc_event  sendEvent;

  /*Default ID*/
  short m_id = -1;

  /*For init router*/
  int data;

  void fordwardThread();
  void backwardThread();
  void createRequestsThread();
  RequestDirection calculateForwardRoute(short id, short destination);
  RequestDirection calculateBackwardRoute(short id, short destination);
};

#endif