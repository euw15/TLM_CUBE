#include "Router.h"

tlm::tlm_sync_enum Router::nb_transport_bw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay )   
{     
  ID_extension* id_extension;
  trans.get_extension( id_extension ); 
  tlm::tlm_command cmd = trans.get_command();   
  sc_dt::uint64    adr = trans.get_address();   
  /*Check the resp was done*/
  if (phase == tlm::BEGIN_RESP) {
                            
    /*Initiator obliged to check response status*/   
    if (trans.is_response_error() )   
      SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
    
    /*Delegates the problem to the thread*/
    MessageInfo backwardMessage {&trans,phase,delay};
    backwardQueue.push(backwardMessage);


    //Delay
    wait(delay);
    backwardEvent.notify();

    cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
    cout << "INIT trans/bw = { " << (cmd ? 'W' : 'R') << ", " << hex << adr   
         << " } , data = " << hex << data << " at time " << sc_time_stamp()   
         << ", delay = " << delay << endl;

    return tlm::TLM_ACCEPTED;   
  }     
} 

/*
The Router module only sends request
*/
tlm::tlm_sync_enum Router::nb_transport_fw( tlm::tlm_generic_payload& trans,tlm::tlm_phase& phase, sc_time& delay )
{
  unsigned char*   byt = trans.get_byte_enable_ptr();
  ID_extension* id_extension;
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

    
    //Delay
    wait(delay);
    fordwardEvent.notify();      
    cout << name() << " BEGIN_REQ RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;      
    
    return tlm::TLM_COMPLETED;
  }  
}

void Router::fordwardThread()  
{   
  while (true) {
    /*Waits for someone notify the event*/
    wait(fordwardEvent);  
    /*Pop de queue and send the message*/
    if(!fordwardQueue.empty())
    {
      MessageInfo pendingMessage = fordwardQueue.front();
      ID_extension* id_extension;
      pendingMessage.transaction->get_extension( id_extension );
      //pendingMessage.delay = sc_time(10, SC_NS);

      /*Send the request to the corresponding socket*/
      RequestDirection nextStep = calculateForwardRoute(m_id, 7);
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
        memorySocket->nb_transport_fw(*pendingMessage.transaction, pendingMessage.phase, pendingMessage.delay);
        //cout<<"SOMETHING IS WRONG";
      }
      /*Delay between RD/WR request*/
      
      //id_extension->transaction_id++; 
    }
  }   
} 

void Router::backwardThread()
{
  while(true){
    /*Waits for someone notify the event*/
    wait(backwardEvent);
    /*Pop de queue and send the message back*/
    if(!backwardQueue.empty())
    {
      MessageInfo backwardMessage = backwardQueue.front();
      //backwardMessage = backwardQueue.front();
      tlm::tlm_command cmd = backwardMessage.transaction->get_command();   
      sc_dt::uint64    adr = backwardMessage.transaction->get_address();   
      
      ID_extension* id_extension;
      backwardMessage.transaction->get_extension( id_extension ); 
  
    
      if (backwardMessage.phase == tlm::BEGIN_RESP) {
                                
        // Initiator obliged to check response status   
        if (backwardMessage.transaction->is_response_error() )   
          SC_REPORT_ERROR("TLM2", "Response error from nb_transport");   
        
        //backwardMessage.phase = tlm::BEGIN_RESP;
        //backwardMessage.delay = sc_time(10, SC_NS);

        RequestDirection nextStep = calculateBackwardRoute(m_id, id_extension->transaction_id>>8);
        if(nextStep == RequestDirection::MOVE_X){
          xTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);
        }
        else if(nextStep == RequestDirection::MOVE_Y){
          yTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);
        }
        else if(nextStep == RequestDirection::MOVE_Z){
          zTargetSocket->nb_transport_bw( *backwardMessage.transaction, backwardMessage.phase, backwardMessage.delay);
        }
        else{
          sendEvent.notify();
        }
      
        //Delay
        
       //cout << name () << " BEGIN_RESP RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;
        
      }   
    }
  }
}

void Router::createRequestsThread()   
{   
  if(m_id == 0){
    wait(m_id*10, SC_NS);
    /*TML Payload is the struct use to send a request*/
    tlm::tlm_generic_payload trans;
      
    /*Sort of the id of the transaction*/
    ID_extension* id_extension = new ID_extension();
    id_extension->transaction_id += m_id<<8;
    //id_extension->transaction_id = m_id*100;
    for(int i = 0; i<2; i++){
      /*Sort of the id of the transaction*/
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
      wait(delay);
      /*Send the request to the next socket*/
      cout << name() << " BEGIN_REQ SENT" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;  
    
      RequestDirection nextStep = calculateForwardRoute(m_id, 7);
      if(nextStep == RequestDirection::MOVE_X){
        xInitSocket->nb_transport_fw(trans, phase, delay);
      }
      else if(nextStep == RequestDirection::MOVE_Y){
        yInitSocket->nb_transport_fw(trans, phase, delay);
      }
      else if(nextStep == RequestDirection::MOVE_Z){
        zInitSocket->nb_transport_fw(trans, phase, delay);
      }
      wait(sendEvent);
      id_extension->transaction_id++; 
    }
    /*Delay between RD/WR request*/
      
  }
}  

RequestDirection Router::calculateForwardRoute(short id, short destination){
  if((destination&1)!=(id&1))
  {
    return RequestDirection::MOVE_X;
  }
  else if((destination&4)!=(id&4)){
    return RequestDirection::MOVE_Y;
  }
  else if((destination&2)!=(id&2)){
    return RequestDirection::MOVE_Z;
  }
  else{
    return RequestDirection::NO_MOVE;
  }
}


RequestDirection Router::calculateBackwardRoute(short id, short destination){
  if((destination&2)!=(id&2))
  {
    return RequestDirection::MOVE_Z;
  }
  else if((destination&4)!=(id&4)){
    return RequestDirection::MOVE_Y;
  }
  else if((destination&1)!=(id&1)){
    return RequestDirection::MOVE_X;
  }
  else{
    return RequestDirection::NO_MOVE;
  }
}


void Router::setId(short id)
{
   m_id = id;
}

