#include <systemc.h>   
using namespace sc_core;   
using namespace sc_dt;   
using namespace std;   

#include "tlm.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/peq_with_cb_and_phase.h"
#include <queue>
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

/*Struct use for the Queue of messages and calbacks*/
struct MessageInfo
{
  tlm::tlm_generic_payload* transaction;
  tlm::tlm_phase phase;
  sc_time delay;
};

/*Enuma that handle the directions the reqquest can do*/
enum class RequestDirection{
  MOVE_X,
  MOVE_Y,
  MOVE_Z,
  NO_MOVE
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
  tlm_utils::simple_initiator_socket<Initiator> xInitSocket; 
  tlm_utils::simple_initiator_socket<Initiator> yInitSocket; 
  tlm_utils::simple_initiator_socket<Initiator> zInitSocket; 
  int data;   

  SC_CTOR(Initiator)   
  : xInitSocket("xInitSocket"),yInitSocket("yInitSocket"),zInitSocket("zInitSocket")   
  {   
    /* Register callbacks for incoming messages*/
    xInitSocket.register_nb_transport_bw(this, &Initiator::nb_transport_bw);
    yInitSocket.register_nb_transport_bw(this, &Initiator::nb_transport_bw);
    zInitSocket.register_nb_transport_bw(this, &Initiator::nb_transport_bw);
     
  }   
   
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
  
  /*Event used to notify the thread it has to iterate*/
  sc_event  fordwardEvent;
  sc_event  backwardEvent;

  /*Referance to trans recive in FW method*/
  tlm::tlm_generic_payload* trans_bw;   
  tlm::tlm_phase            phase_bw;   
  sc_time                   delay_bw;

  /*Default ID*/
  short m_id = -1;

  /*For init router*/
  int data;

  SC_CTOR(Router)   
  : xInitSocket("xInitSocket"),xTargetSocket("xTargetSocket"),
    yInitSocket("yInitSocket"),yTargetSocket("yTargetSocket"),
    zInitSocket("zInitSocket"),zTargetSocket("zTargetSocket")
  {   
    // Register callbacks for incoming interface method calls
    xInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
    xTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
    yInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
    yTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
    zInitSocket.register_nb_transport_bw(this, &Router::nb_transport_bw);
    zTargetSocket.register_nb_transport_fw(this, &Router::nb_transport_fw);
    SC_THREAD(fordwardThread);  
    SC_THREAD(backwardThread);
    SC_THREAD(createRequestsThread);
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
      MessageInfo pendingMessage {&trans, phase, delay};
      fordwardQueue.push(pendingMessage);
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
      MessageInfo backwardMessage {&trans,phase,delay};
      backwardQueue.push(backwardMessage);
      backwardEvent.notify();
      return tlm::TLM_ACCEPTED;   
    }   
  } 
  
  void fordwardThread()  
  {   
    while (true) {
      /*Waits for someone notify the event*/
      wait(fordwardEvent);  
      /*Pop de queue and send the message*/
      if(!fordwardQueue.empty())
      {
        MessageInfo pendingMessage = fordwardQueue.front();
        ID_extension* id_extension = new ID_extension;
        pendingMessage.transaction->get_extension( id_extension );
        pendingMessage.delay = sc_time(10, SC_NS);

        /*Send the request to the corresponding socket*/
        RequestDirection nextStep = calculateForwardRoute(m_id);
        if(nextStep == RequestDirection::MOVE_X){
          xInitSocket->nb_transport_fw(*pendingMessage.transaction, pendingMessage.phase, pendingMessage.delay);
        }
        else if(nextStep == RequestDirection::MOVE_Y){
          yInitSocket->nb_transport_fw(*pendingMessage.transaction, pendingMessage.phase, pendingMessage.delay);
        }
        else if(nextStep == RequestDirection::MOVE_Z){
          zInitSocket->nb_transport_fw(*pendingMessage.transaction, pendingMessage.phase, pendingMessage.delay);
        }
        else{
          cout<<"SOMETHING IS WRONG";
        }
        /*Delay between RD/WR request*/
        wait(pendingMessage.delay);
        
        id_extension->transaction_id++; 
      }
    }   
  } 

  void backwardThread()
  {
    while(true){
      /*Waits for someone notify the event*/
      wait(backwardEvent);
      /*Pop de queue and send the message back*/
      if(!backwardQueue.empty())
      {
        MessageInfo backwardMessage = backwardQueue.front();
        tlm::tlm_command cmd = backwardMessage.transaction->get_command();   
        sc_dt::uint64    adr = backwardMessage.transaction->get_address();   
        
        ID_extension* id_extension = new ID_extension;
        backwardMessage.transaction->get_extension( id_extension ); 
    
      
        if (backwardMessage.phase == tlm::BEGIN_RESP) {
                                  
          // Initiator obliged to check response status   
          if (backwardMessage.transaction->is_response_error() )   
            SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
          
          backwardMessage.phase = tlm::BEGIN_RESP;
          backwardMessage.delay = sc_time(10, SC_NS);

          xTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);         
          yTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);
          zTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);
          //Delay
          wait(backwardMessage.delay);
          
          cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
          
        }   
      }
    }
  }

  void createRequestsThread()   
  {   
    if(m_id == 0){
      /*TML Payload is the struct use to send a request*/
      tlm::tlm_generic_payload trans;

      /*Sort of the id of the transaction*/
      ID_extension* id_extension = new ID_extension;
      trans.set_extension( id_extension ); 
       
      /*Set the request information in the payload*/
      tlm::tlm_phase phase = tlm::BEGIN_REQ;   
      sc_time delay = sc_time(10, SC_NS);   
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
      tlm::tlm_sync_enum status = xInitSocket->nb_transport_fw( trans, phase, delay );
      
      /*Delay between RD/WR request*/
      wait(100, SC_NS);
        
      id_extension->transaction_id++; 
    }
  }  

  RequestDirection calculateForwardRoute(short id){
    switch(id){
      case 0:
        return RequestDirection::MOVE_X;
      case 1:
        return RequestDirection::MOVE_Y;
      case 2:
        return RequestDirection::MOVE_X;
      case 3:
        return RequestDirection::MOVE_Y;
      case 4:
        return RequestDirection::MOVE_Z;
      case 5:
        return RequestDirection::MOVE_Z;
      case 6:
        return RequestDirection::MOVE_X;
      case 7:
        return RequestDirection::MOVE_X;
      default:
        return RequestDirection::NO_MOVE;
    }
  }
  
  void setId(short id){
     m_id = id;
  };

};   

/* Target memory*/  
struct Memory: sc_module   
{   
  /*Multiple targets but only target x is used*/
  tlm_utils::simple_target_socket<Memory> xTargetSocket;
  tlm_utils::simple_target_socket<Memory> yTargetSocket;
  tlm_utils::simple_target_socket<Memory> zTargetSocket;

  /*Message request queue*/
  std::queue<MessageInfo> incomingRequest;

  /*Wait times*/
  enum { SIZE = 256 };   
  const sc_time LATENCY;   
  
  int mem[SIZE];   
  sc_event  requestEvent;

  SC_CTOR(Memory)   
  : xTargetSocket("xTargetSocket"),yTargetSocket("yTargetSocket"),zTargetSocket("zTargetSocket"), LATENCY(10, SC_NS)   
  {   
    xTargetSocket.register_nb_transport_fw(this, &Memory::nb_transport_fw);
    yTargetSocket.register_nb_transport_fw(this, &Memory::nb_transport_fw);
    zTargetSocket.register_nb_transport_fw(this, &Memory::nb_transport_fw);

    // Initialize memory with random data   
    for (int i = 0; i < SIZE; i++)   
      mem[i] = 0xAA000000 | (rand() % 256);   
   
    SC_THREAD(memoryRespondRequest);   
  }   

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
    
      MessageInfo pendingMessage {&trans,phase,delay};
      incomingRequest.push(pendingMessage);
      requestEvent.notify();
      
      //Delay
      wait(delay);
      
      cout << name() << " BEGIN_REQ RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;      
      
      return tlm::TLM_ACCEPTED;
    }  
  }
  /*Thread handling memory requests*/
  void memoryRespondRequest()  
  {   
    while (true) {
      // Wait for an event to pop out of the back end of the queue   
      wait(requestEvent); 
      
      MessageInfo request = incomingRequest.front();     
    

      ID_extension* id_extension = new ID_extension;
      request.transaction->get_extension( id_extension ); 
      
      tlm::tlm_command cmd = request.transaction->get_command();   
      sc_dt::uint64    adr = request.transaction->get_address() / 4;   
      unsigned char*   ptr = request.transaction->get_data_ptr();   
      unsigned int     len = request.transaction->get_data_length();   
      unsigned char*   byt = request.transaction->get_byte_enable_ptr();   
      unsigned int     wid = request.transaction->get_streaming_width();   
     
      if (adr >= sc_dt::uint64(SIZE) || byt != 0 || wid != 0 || len > 4)   
        SC_REPORT_ERROR("TLM2", "Target does not support given generic payload transaction");   
      
      // Obliged to implement read and write commands   
      if ( cmd == tlm::TLM_READ_COMMAND )   
        memcpy(ptr, &mem[adr], len);   
      else if ( cmd == tlm::TLM_WRITE_COMMAND )   
        memcpy(&mem[adr], ptr, len);   

      // Obliged to set response status to indicate successful completion   
      request.transaction->set_response_status( tlm::TLM_OK_RESPONSE );  
      
      wait(20, SC_NS);
      
      request.delay = sc_time(10, SC_NS);
      
      cout << name() << " BEGIN_RESP SENT" << " TRANS ID " << id_extension->transaction_id <<  " at time " << sc_time_stamp() << endl;
      
      // Call on backward path to complete the transaction
      tlm::tlm_sync_enum status;
        request.phase = tlm::BEGIN_RESP;   
      status = xTargetSocket->nb_transport_bw( *request.transaction, request.phase, request.delay );   
    }   
  }   

  
};   
   
   
SC_MODULE(Top)   
{  
  Router    *router0;
  Router    *router1;
  Router    *router2;  
  Router    *router3;
  Router    *router4;  
  Router    *router5;
  Router    *router6;  
  Router    *router7;   
  Memory    *memory;   
  Initiator *dummy;

  SC_CTOR(Top)   
  {   
    /*Init routers and memory*/ 
    router0    = new Router("router0");
    router1    = new Router("router1");
    router2    = new Router("router2");
    router3    = new Router("router3");
    router4    = new Router("router4");
    router5    = new Router("router5");
    router6    = new Router("router6");
    router7    = new Router("router7");
    memory     = new Memory("memory");   
    dummy      = new Initiator("dummy");

    /*Set Routers numbers IDs*/
    router0->setId(0);
    router1->setId(1);
    router2->setId(2);
    router3->setId(3);
    router4->setId(4);
    router5->setId(5);
    router6->setId(6);
    router7->setId(7);

    /*Creates the cub interconation*/
    router0->xInitSocket(router1->xTargetSocket);
    router0->yInitSocket(router4->yTargetSocket);
    router0->zInitSocket(router2->zTargetSocket);

    router1->xInitSocket(router0->xTargetSocket);
    router1->yInitSocket(router5->yTargetSocket);
    router1->zInitSocket(router3->zTargetSocket);

    router2->xInitSocket(router3->xTargetSocket);
    router2->yInitSocket(router6->yTargetSocket);
    router2->zInitSocket(router0->zTargetSocket);

    router3->xInitSocket(router2->xTargetSocket);
    router3->yInitSocket(router7->yTargetSocket);
    router3->zInitSocket(router1->zTargetSocket);

    router4->xInitSocket(router5->xTargetSocket);
    router4->yInitSocket(router0->yTargetSocket);
    router4->zInitSocket(router6->zTargetSocket);
   
    router5->xInitSocket(router4->xTargetSocket);
    router5->yInitSocket(router1->yTargetSocket);
    router5->zInitSocket(router7->zTargetSocket);

    router6->xInitSocket(router7->xTargetSocket);
    router6->yInitSocket(router2->yTargetSocket);
    router6->zInitSocket(router4->zTargetSocket);

    router7->xInitSocket(memory->xTargetSocket);
    router7->yInitSocket(memory->yTargetSocket);
    router7->zInitSocket(memory->zTargetSocket);

    /*Dummy conections so TLM doesnt complain*/
    dummy->xInitSocket(router6->xTargetSocket);
    dummy->yInitSocket(router3->yTargetSocket);
    dummy->zInitSocket(router5->zTargetSocket);

  }   
};   
   
   
int sc_main(int argc, char* argv[])   
{   
  Top top("top");   
  sc_start();   
  return 0;   
}   
   

 