#include "Memory.h"




tlm::tlm_sync_enum Memory::nb_transport_fw( tlm::tlm_generic_payload& trans, tlm::tlm_phase& phase, sc_time& delay ){
	
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
	}
	MessageInfo pendingMessage {&trans,phase,delay};
	incomingRequest.push(pendingMessage);

	//Delay
	wait(delay);
	requestEvent.notify();
	  
	  
	cout << name() << " BEGIN_REQ RECEIVED" << " TRANS ID " << id_extension->transaction_id << " at time " << sc_time_stamp() << endl;      
	  
	return tlm::TLM_ACCEPTED;
     
}

void Memory::memoryRespondRequest(){
 
    while (true) {
      // Wait for an event to pop out of the back end of the queue   
      wait(requestEvent); 
      
      MessageInfo request = incomingRequest.front();     
    

      ID_extension* id_extension;
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
      
      wait(request.delay);
      
      //request.delay = sc_time(10, SC_NS);
      
      cout << name() << " BEGIN_RESP SENT" << " TRANS ID " << id_extension->transaction_id <<  " at time " << sc_time_stamp() << endl;
      
      // Call on backward path to complete the transaction
      tlm::tlm_sync_enum status;
      request.phase = tlm::BEGIN_RESP;   
      status = xTargetSocket->nb_transport_bw( *request.transaction, request.phase, request.delay );   
    } 
}