#include "VirtualMachine.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

TVMMutexID MutexHigh, MutexMedium, MutexLow, MutexMain;

void VMThreadHigh(void *param){
    VMPrint("VMThreadHigh Alive\n");
    VMMutexAcquire(MutexHigh, VM_TIMEOUT_INFINITE);
    VMPrint("VMThreadHigh Awake\n");
}

void VMThreadMedium(void *param){
    VMPrint("VMThreadMedium Alive\n");
    VMMutexAcquire(MutexMedium, VM_TIMEOUT_INFINITE);
    VMPrint("VMThreadMedium Awake\n");
}

void VMThreadLow(void *param){
    VMMutexAcquire(MutexMain, VM_TIMEOUT_INFINITE);
    VMPrint("VMThreadLow Alive\n");
    VMMutexAcquire(MutexLow, VM_TIMEOUT_INFINITE);
    VMPrint("VMThreadLow Awake\n");
    VMMutexRelease(MutexMain);
}

void VMMain(int argc, char *argv[]){
    TVMThreadID VMThreadIDHigh, VMThreadIDMedium, VMThreadIDLow;
    
    VMPrint("VMMain creating threads.\n");
    VMThreadCreate(VMThreadLow, NULL, 0x100000, VM_THREAD_PRIORITY_LOW, &VMThreadIDLow);
    VMThreadCreate(VMThreadMedium, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &VMThreadIDMedium);
    VMThreadCreate(VMThreadHigh, NULL, 0x100000, VM_THREAD_PRIORITY_HIGH, &VMThreadIDHigh);
    VMPrint("VMMain creating mutexes.\n");
    VMMutexCreate(&MutexMain);
    VMMutexCreate(&MutexLow);
    VMMutexCreate(&MutexMedium);
    VMMutexCreate(&MutexHigh);
    VMPrint("VMMain locking mutexes.\n");
    VMMutexAcquire(MutexLow, VM_TIMEOUT_INFINITE);
    VMMutexAcquire(MutexMedium, VM_TIMEOUT_INFINITE);
    VMMutexAcquire(MutexHigh, VM_TIMEOUT_INFINITE);
    VMPrint("VMMain activating processes.\n");
    VMThreadActivate(VMThreadIDLow);
    VMThreadActivate(VMThreadIDMedium);
    VMThreadActivate(VMThreadIDHigh);
    VMPrint("VMMain releasing mutexes.\n");
    VMMutexRelease(MutexLow);
    VMMutexRelease(MutexMedium);
    VMMutexRelease(MutexHigh);
    VMPrint("VMMain going to sleep.\n");
    VMThreadSleep(1);
    VMPrint("VMMain acquiring main mutex.\n");
    VMMutexAcquire(MutexMain, VM_TIMEOUT_INFINITE);
    VMPrint("VMMain acquired main mutex.\nGoodbye\n");
}

