#include "VirtualMachine.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

void VMThread(void *param){
    volatile int *Val = (int *)param;
    int RunTimeTicks;
    TVMTick CurrentTick, EndTick;
    
    VMTickMS(&RunTimeTicks);
    RunTimeTicks = (5000 + RunTimeTicks - 1)/RunTimeTicks;
    VMTickCount(&CurrentTick);
    EndTick = CurrentTick + RunTimeTicks;
    VMPrint("VMThread Alive\nVMThread Starting\n");
    while(EndTick > CurrentTick){
        (*Val)++;
        VMTickCount(&CurrentTick);
    }
    VMPrint("VMThread Done\n");
}

void VMMain(int argc, char *argv[]){
    TVMThreadID VMThreadID1, VMThreadID2;
    TVMThreadState VMState1, VMState2;
    volatile int Val1 = 0, Val2 = 0;
    volatile int LocalVal1, LocalVal2;
    VMPrint("VMMain creating threads.\n");
    VMThreadCreate(VMThread, (void *)&Val1, 0x100000, VM_THREAD_PRIORITY_LOW, &VMThreadID1);
    VMThreadCreate(VMThread, (void *)&Val2, 0x100000, VM_THREAD_PRIORITY_LOW, &VMThreadID2);
    VMPrint("VMMain activating threads.\n");
    VMThreadActivate(VMThreadID1);
    VMThreadActivate(VMThreadID2);
    VMPrint("VMMain Waiting\n");
    do{
        LocalVal1 = Val1; 
        LocalVal2 = Val2;
        VMThreadState(VMThreadID1, &VMState1);
        VMThreadState(VMThreadID2, &VMState2);
        VMPrint("%d %d\n", LocalVal1, LocalVal2);
        VMThreadSleep(2);
    }while((VM_THREAD_STATE_DEAD != VMState1)||(VM_THREAD_STATE_DEAD != VMState2));
    VMPrint("VMMain Done\n");
    VMPrint("Goodbye\n");
}

