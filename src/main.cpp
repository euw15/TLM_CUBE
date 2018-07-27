#include <iostream>
#include <systemc.h>
#include "Router.h"
#include "Memory.h"

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
  Null_Port *Null_Port0;  
  Null_Port *Null_Port1;  
  Null_Port *Null_Port2;  
  Null_Port *Null_Port3;  
  Null_Port *Null_Port4;  
  Null_Port *Null_Port5;  
  Null_Port *Null_Port6;  
  Memory    *memory;   
  //Initiator *dummy;

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
    Null_Port0     = new Null_Port("NPO");   
    Null_Port1     = new Null_Port("NP1");   
    Null_Port2     = new Null_Port("NP2");   
    Null_Port3     = new Null_Port("NP3");   
    Null_Port4     = new Null_Port("NP4");   
    Null_Port5     = new Null_Port("NP5");   
    Null_Port6     = new Null_Port("NP6");   
    memory     = new Memory("memory");   

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
    router0->memorySocket(Null_Port0->NullSocket);

    router1->xInitSocket(router0->xTargetSocket);
    router1->yInitSocket(router5->yTargetSocket);
    router1->zInitSocket(router3->zTargetSocket);
    router1->memorySocket(Null_Port1->NullSocket);

    router2->xInitSocket(router3->xTargetSocket);
    router2->yInitSocket(router6->yTargetSocket);
    router2->zInitSocket(router0->zTargetSocket);
    router2->memorySocket(Null_Port2->NullSocket);

    router3->xInitSocket(router2->xTargetSocket);
    router3->yInitSocket(router7->yTargetSocket);
    router3->zInitSocket(router1->zTargetSocket);
    router3->memorySocket(Null_Port3->NullSocket);

    router4->xInitSocket(router5->xTargetSocket);
    router4->yInitSocket(router0->yTargetSocket);
    router4->zInitSocket(router6->zTargetSocket);
    router4->memorySocket(Null_Port4->NullSocket);
   
    router5->xInitSocket(router4->xTargetSocket);
    router5->yInitSocket(router1->yTargetSocket);
    router5->zInitSocket(router7->zTargetSocket);
    router5->memorySocket(Null_Port5->NullSocket);

    router6->xInitSocket(router7->xTargetSocket);
    router6->yInitSocket(router2->yTargetSocket);
    router6->zInitSocket(router4->zTargetSocket);
    router6->memorySocket(Null_Port6->NullSocket);

    router7->xInitSocket(router6->xTargetSocket);
    router7->yInitSocket(router3->yTargetSocket);
    router7->zInitSocket(router5->zTargetSocket);
    router7->memorySocket(memory->xTargetSocket);
    
    
  }  
};   
   

int sc_main(int argc, char* argv[])   
{   
  Top top("top");   
  sc_start();   
  return 0;   
} 
