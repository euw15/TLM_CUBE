#ifndef TLM_CUBE_MEMORY_H_
#define TLM_CUBE_MEMORY_H_

#include <systemc.h>
#include <algorithm>
#include "Router.h"

class Memory : public sc_module
{
	public:
	  SC_CTOR(Memory)   
	  : xTargetSocket("xTargetSocket"), LATENCY(10, SC_NS)   
	  {   
	    xTargetSocket.register_nb_transport_fw(this, &Memory::nb_transport_fw);

	    // Initialize memory with random data   
	    for (int i = 0; i < SIZE; i++)   
	      mem[i] = (i);   
	      //mem[i] = 0xAA000000 | (rand() % 256);   
	   
	    SC_THREAD(memoryRespondRequest);   
	  }

	  /*Multiple targets but only target x is used*/
	  tlm_utils::simple_target_socket<Memory> xTargetSocket;

	  /*Message request queue*/
	  std::queue<MessageInfo> incomingRequest;

	  /*Wait times*/
	  enum { SIZE = 256 };   
	  const sc_time LATENCY;   
	  
	  int mem[SIZE];   
	  sc_event  requestEvent;   		
	  virtual tlm::tlm_sync_enum nb_transport_fw( tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay );
	
	private:
	
	void memoryRespondRequest();
};

#endif