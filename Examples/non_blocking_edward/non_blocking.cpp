#include <systemc.h>   
using namespace sc_core;   
using namespace sc_dt;   
using namespace std;   
   
#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/peq_with_cb_and_phase.h"

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


/*
Initiator start sending messages to diferents nodes.
Has a thread processs which creates the transactions
*/
struct Initiator: sc_module   
{   
  /*
  Socket used to send the requests 
  */
  tlm_utils::simple_initiator_socket<Initiator> initSocket; 
  int data;   

  SC_CTOR(Initiator)   
  : initSocket("initSocket")   
  {   
    /* Register callbacks for incoming messages*/
    initSocket.register_nb_transport_bw(this, &Initiator::nb_transport_bw);
    
    SC_THREAD(createRequestsThread);   
  }   
   
  void createRequestsThread()   
  {   
    /*TML Payload is the struct use to send a request*/
    tlm::tlm_generic_payload trans;

    /*Sort of the id of the transaction*/
    ID_extension* id_extension = new ID_extension;
    trans.set_extension( id_extension ); 
     
    /*Set the request information in the payload*/
    tlm::tlm_phase phase = tlm::BEGIN_REQ;   
    sc_time delay = sc_time(20, SC_NS);   
    tlm::tlm_command cmd = static_cast<tlm::tlm_command>(rand() % 2);   
    if (cmd == tlm::TLM_WRITE_COMMAND) data = 0xFF000000 | 0;   
    trans.set_command( cmd );   
    trans.set_address( rand() % 0xFF );   
    trans.set_data_ptr( reinterpret_cast<unsigned char*>(&data) );   
    trans.set_data_length( sizeof(int));   
 
    /*Delay for BEGIN_REQ*/
    wait(10, SC_NS);
    /*Send the request to the next socket*/
    cout << name() << " BEGIN_REQ SENT" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;  
    tlm::tlm_sync_enum status = initSocket->nb_transport_fw( trans, phase, delay );
    
    /*Delay between RD/WR request*/
    wait(100, SC_NS);
      
    id_extension->transaction_id++; 
  }   
   
  // *********************************************   
  // TLM2 backward path non-blocking transport method   
  // *********************************************   
   
  virtual tlm::tlm_sync_enum nb_transport_bw( tlm::tlm_generic_payload& trans,   
                                           tlm::tlm_phase& phase, sc_time& delay )   
  {   
    tlm::tlm_command cmd = trans.get_command();   
    sc_dt::uint64    adr = trans.get_address();   
    
    ID_extension* id_extension = new ID_extension;
    trans.get_extension( id_extension ); 
    
    if (phase == tlm::BEGIN_RESP) {
      // Initiator obliged to check response status   
      if (trans.is_response_error() )   
        SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
            
      //Delay
      wait(delay);
      cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
      cout << "INIT trans/bw = { " << (cmd ? 'W' : 'R') << ", " << hex << adr   
           << " } , data = " << hex << data << " at time " << sc_time_stamp()   
           << ", delay = " << delay << endl;
      return tlm::TLM_ACCEPTED;   
    }   
  }   
};   
     
   
/*
The Router module only sends request
*/
struct Router: sc_module
{
  /*Init and target socket use to send and recibe messages*/
  tlm_utils::simple_initiator_socket<Router> initSocket; 
  tlm_utils::simple_target_socket<Router> targetSocket;
   
  /*Event used to notify the thread it has to iterate*/
  sc_event  fordwardEvent;
  sc_event  backwardEvent;

  /*Referance to trans recive in FW method*/
  tlm::tlm_generic_payload* trans_pending;   
  tlm::tlm_phase phase_pending;   
  sc_time delay_pending;

  /*Referance to trans recive in FW method*/
  tlm::tlm_generic_payload* trans_bw;   
  tlm::tlm_phase phase_bw;   
  sc_time delay_bw;


  SC_CTOR(Router)   
  : initSocket("initSocket"),targetSocket("targetSocket")  // Construct and name socket   
  {   
    // Register callbacks for incoming interface method calls
    initSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
    targetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
    SC_THREAD(fordwardThread);  
    SC_THREAD(backwardThread);  
  }  

  virtual tlm::tlm_sync_enum nb_transport_fw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay )
  {
    unsigned char*   byt = trans.get_byte_enable_ptr();
    ID_extension* id_extension = new ID_extension;
    trans.get_extension( id_extension ); 
    
    if(phase == tlm::BEGIN_REQ){
      // Obliged to check the transaction attributes for unsupported features
      // and to generate the appropriate error response
      if (byt != 0) {
        trans.set_response_status( tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE );
        return tlm::TLM_COMPLETED;
      }
      
      // Now queue the transaction until the annotated time has elapsed
      trans_pending=&trans;
      phase_pending=phase;
      delay_pending=delay;

      fordwardEvent.notify();
      
      cout << name() << " BEGIN_REQ RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;      
      //Delay
      wait(delay);
        
      
      return tlm::TLM_COMPLETED;
    }  
  }

  virtual tlm::tlm_sync_enum nb_transport_bw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay )   
  {     
    ID_extension* id_extension = new ID_extension;
    trans.get_extension( id_extension ); 
  
    /*Check the resp was done*/
    if (phase == tlm::BEGIN_RESP) {
                              
      /*Initiator obliged to check response status*/   
      if (trans.is_response_error() )   
        SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
      
      /*Delegates the problem to the thread*/
      trans_bw=&trans;
      phase_bw=phase;
      delay_bw=delay;
             
      backwardEvent.notify();
    
      cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
      
      return tlm::TLM_ACCEPTED;   
    }   
  } 
  
  void fordwardThread()  
  {   
    while (true) {
      // Wait for an event to pop out of the back end of the queue   
      wait(fordwardEvent);  
      ID_extension* id_extension = new ID_extension;
      trans_pending->get_extension( id_extension ); 
      tlm::tlm_sync_enum status; 

      status = initSocket->nb_transport_fw(*trans_pending, phase_pending, delay_pending);
      
      //Delay between RD/WR request
      wait(delay_pending);
      
      id_extension->transaction_id++; 
        
    }   
  } 

  void backwardThread()
  {
    while(true){

      wait(backwardEvent);

      tlm::tlm_command cmd = trans_bw->get_command();   
      sc_dt::uint64    adr = trans_bw->get_address();   
      
      ID_extension* id_extension = new ID_extension;
      trans_bw->get_extension( id_extension ); 
  
    
      if (phase_bw == tlm::BEGIN_RESP) {
                                
        // Initiator obliged to check response status   
        if (trans_bw->is_response_error() )   
          SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
        
        phase_bw = tlm::BEGIN_RESP;   
        targetSocket->nb_transport_bw( *trans_bw, phase_bw, delay_bw );         
        
        
        //Delay
        wait(delay_bw);
        
        cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
        
      }   
    }
  }

};   
// Target module representing a simple memory   
   
struct Memory: sc_module   
{   
  // TLM-2 socket, defaults to 32-bits wide, base protocol
  tlm_utils::simple_target_socket<Memory> targetSocket;
  
  enum { SIZE = 256 };   
  const sc_time LATENCY;   
   
  SC_CTOR(Memory)   
  : targetSocket("targetSocket"), LATENCY(10, SC_NS)   
  {   
    // Register callbacks for incoming interface method calls
    targetSocket.register_nb_transport_fw(this, &Memory::nb_transport_fw);
    //targetSocket.register_nb_transport_bw(this, &Memory::nb_transport_bw);
   
    // Initialize memory with random data   
    for (int i = 0; i < SIZE; i++)   
      mem[i] = 0xAA000000 | (rand() % 256);   
   
    SC_THREAD(thread_process);   
  }   
   
  // TLM2 non-blocking transport method 
  
  virtual tlm::tlm_sync_enum nb_transport_fw( tlm::tlm_generic_payload& trans,
                                              tlm::tlm_phase& phase, sc_time& delay )
  {
    unsigned char*   byt = trans.get_byte_enable_ptr();
    ID_extension* id_extension = new ID_extension;
    trans.get_extension( id_extension ); 
    
  
    if(phase == tlm::BEGIN_REQ){
      // Obliged to check the transaction attributes for unsupported features
      // and to generate the appropriate error response
      if (byt != 0) {
        trans.set_response_status( tlm::TLM_BYTE_ENABLE_ERROR_RESPONSE );
        return tlm::TLM_COMPLETED;
      }
     
      // Now queue the transaction until the annotated time has elapsed
      trans_pending=&trans;
      phase_pending=phase;
      delay_pending=delay;

      e1.notify();
      
      //Delay
      wait(delay);
      
      cout << name() << " BEGIN_REQ RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;      
      
      return tlm::TLM_ACCEPTED;
    }  
  }
  
  // *********************************************   
  // Thread to call nb_transport on backward path   
  // ********************************************* 
   
  void thread_process()  
  {   
    while (true) {
      // Wait for an event to pop out of the back end of the queue   
      wait(e1); 
      //printf("ACCESING MEMORY\n");
      
      //tlm::tlm_generic_payload* trans_ptr;   
      tlm::tlm_phase phase;   
      
      ID_extension* id_extension = new ID_extension;
      trans_pending->get_extension( id_extension ); 
      
      tlm::tlm_command cmd = trans_pending->get_command();   
      sc_dt::uint64    adr = trans_pending->get_address() / 4;   
      unsigned char*   ptr = trans_pending->get_data_ptr();   
      unsigned int     len = trans_pending->get_data_length();   
      unsigned char*   byt = trans_pending->get_byte_enable_ptr();   
      unsigned int     wid = trans_pending->get_streaming_width();   
   
      // Obliged to check address range and check for unsupported features,   
      //   i.e. byte enables, streaming, and bursts   
      // Can ignore DMI hint and extensions   
      // Using the SystemC report handler is an acceptable way of signalling an error   
     
      if (adr >= sc_dt::uint64(SIZE) || byt != 0 || wid != 0 || len > 4)   
        SC_REPORT_ERROR("TLM2", "Target does not support given generic payload transaction");   
      
      // Obliged to implement read and write commands   
      if ( cmd == tlm::TLM_READ_COMMAND )   
        memcpy(ptr, &mem[adr], len);   
      else if ( cmd == tlm::TLM_WRITE_COMMAND )   
        memcpy(&mem[adr], ptr, len);   

      // Obliged to set response status to indicate successful completion   
      trans_pending->set_response_status( tlm::TLM_OK_RESPONSE );  
      
      wait(20, SC_NS);
      
      delay_pending= (rand() % 4) * sc_time(10, SC_NS);
      
      cout << name() << " BEGIN_RESP SENT" << " TRANS ID " << id_extension->transaction_id <<  " at time " << sc_time_stamp() << endl;
      
      // Call on backward path to complete the transaction
      tlm::tlm_sync_enum status;
        phase = tlm::BEGIN_RESP;   
      status = targetSocket->nb_transport_bw( *trans_pending, phase, delay_pending );   
   
        // The target gets a final chance to read or update the transaction object at this point.   
        // Once this process yields, the target must assume that the transaction object   
        // will be deleted by the initiator   
   
      // Check value returned from nb_transport   
   
      switch (status)   
        
      //case tlm::TLM_REJECTED:   
        case tlm::TLM_ACCEPTED:   
          
          wait(10, SC_NS);
          
          //cout << name() << " END_RESP SENT" << " TRANS ID " << id_extension->transaction_id <<  " at time " << sc_time_stamp() << endl;
          // Expect response on the backward path  
          phase = tlm::END_RESP; 
          //targetSocket->nb_transport_bw( *trans_pending, phase, delay_pending );  // Non-blocking transport call
        //break;   
        
    }   
  } 
   
  int mem[SIZE];   
  sc_event  e1;
  tlm::tlm_generic_payload* trans_pending;   
  tlm::tlm_phase phase_pending;   
  sc_time delay_pending;
    
};   
   
   
SC_MODULE(Top)   
{   
  Initiator *initiator;
  Router    *router1;
  Router    *router2;    
  Memory    *memory;   
   
  SC_CTOR(Top)   
  {   
    // Instantiate components   
    initiator = new Initiator("initiator");  
    router1    = new Router("router1");
    router2    = new Router("router2");
    memory    = new Memory   ("memory");   
   
    // One initiator is bound directly to one target with no intervening bus   
   
    // Bind initiator targetSocket to target socket   
    initiator->initSocket.bind(router1->targetSocket);
    router1->initSocket.bind(router2->targetSocket);  
    router2->initSocket.bind(memory->targetSocket);
  }   
};   
   
   
int sc_main(int argc, char* argv[])   
{   
  Top top("top");   
  sc_start();   
  return 0;   
}   
   

 