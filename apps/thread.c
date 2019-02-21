#include "VirtualMachine.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

void VMThread(void *param){
    VMPrint("VMThread Alive\n");
    VMThreadSleep(10);
    VMPrint("VMThread Awake\n");
}

void VMMain(int argc, char *argv[]){
    TVMThreadID VMThreadID;
    TVMThreadState VMState;
    VMPrint("VMMain creating thread.\n");
    VMThreadCreate(VMThread, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &VMThreadID);
    VMPrint("VMMain getting thread state: ");
    VMThreadState(VMThreadID, &VMState);
    switch(VMState){
        case VM_THREAD_STATE_DEAD:       VMPrint("DEAD\n");
                                        break;
        case VM_THREAD_STATE_RUNNING:    VMPrint("RUNNING\n");
                                        break;
        case VM_THREAD_STATE_READY:      VMPrint("READY\n");
                                        break;
        case VM_THREAD_STATE_WAITING:    VMPrint("WAITING\n");
                                        break;
        default:                        break;
    }
    VMPrint("VMMain activating thread.\n");
    VMThreadActivate(VMThreadID);
    VMPrint("VMMain going to sleep 50\n");
    VMThreadSleep(50);
    VMPrint("VMMain Awake\nGoodbye\n");
    
}

