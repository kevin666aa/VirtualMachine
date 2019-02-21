#include "VirtualMachine.h"

#ifndef NULL
#define NULL    ((void *)0)
#endif

TVMMutexID TheMutex;
volatile int TheMutexAcquired = 0;
volatile int MemoryAllocationSuccess = -1;
volatile char *MemoryBase1;
volatile char *MemoryBase2;

void VMThread(void *param){
    char LocalVariable;
    
    VMPrint("VMThread Alive\nVMThread testing memory locations\n");
    if((MemoryBase1 < &LocalVariable)&&(&LocalVariable < MemoryBase2)){
        MemoryAllocationSuccess = 1;
    }
    else if((MemoryBase1 > &LocalVariable)&&(&LocalVariable > MemoryBase2)){
        MemoryAllocationSuccess = 1;
    }
    else{
        VMPrint("VMThread stack not allocated from system memory pool\n");
        MemoryAllocationSuccess = 0;
        return;
    }
    VMPrint("VMThread Acquiring Mutex\n");
    if(VM_STATUS_SUCCESS != VMMutexAcquire(TheMutex, VM_TIMEOUT_INFINITE)){
        VMPrint("VMThread failed to acquire mutex\n");
        return;
    }
    VMPrint("VMThread Mutex Acquired\n");
    TheMutexAcquired = 1;
    
    
    while(1);
}

void VMMain(int argc, char *argv[]){
    TVMThreadID OtherThreadID, ThisThreadID, BadThreadID, MutexOwner;
    TVMThreadState VMState;
    TVMMutexID BadMutexID;
    TVMMemoryPoolID MemoryPool1;
    TVMMemorySize SystemPoolSize;
    TVMTick CurrentTick, LastTick;
    char *LocalAllocation;
    int MSPerTick;
    
    
    
    VMPrint("VMMain testing VMMemoryPoolQuery.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolQuery(VM_MEMORY_POOL_ID_INVALID, &SystemPoolSize)){
        VMPrint("VMMemoryPoolQuery doesn't handle bad memoryid.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolQuery(VM_MEMORY_POOL_ID_SYSTEM, NULL)){
        VMPrint("VMMemoryPoolQuery doesn't handle NULL bytesleft.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolQuery(VM_MEMORY_POOL_ID_SYSTEM, &SystemPoolSize)){
        VMPrint("VMMemoryPoolQuery doesn't return success with valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMMemoryPoolQuery appears OK.\n");
    VMPrint("VMMain testing VMMemoryPoolAllocate.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, 0, (void **)&MemoryBase1)){
        VMPrint("VMMemoryPoolAllocate doesn't handle zero size.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, 64, NULL)){
        VMPrint("VMMemoryPoolAllocate doesn't handle NULL pointer.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_INVALID, 64, (void **)&MemoryBase1)){
        VMPrint("VMMemoryPoolAllocate doesn't handle bad memoryid.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INSUFFICIENT_RESOURCES  != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, SystemPoolSize + 256, (void **)&MemoryBase1)){
        VMPrint("VMMemoryPoolAllocate doesn't handle insufficient resources.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, 64, (void **)&MemoryBase1)){
        VMPrint("VMMemoryPoolAllocate doesn't return success with valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMMemoryPoolAllocate appears OK.\n");
    VMPrint("VMMain testing VMThreadCreate.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMThreadCreate(NULL, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &OtherThreadID)){
        VMPrint("VMThreadCreate doesn't handle NULL entry.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMThreadCreate(VMThread, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, NULL)){
        VMPrint("VMThreadCreate doesn't handle NULL tid.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMThreadCreate(VMThread, NULL, 0x100000, VM_THREAD_PRIORITY_NORMAL, &OtherThreadID)){
        VMPrint("VMThreadCreate doesn't return success with valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMThreadCreate appears OK.\n");
    if(VM_STATUS_SUCCESS != VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, 64, (void **)&MemoryBase2)){
        VMPrint("VMMemoryPoolAllocate doesn't return success with valid inputs.\n");    
        return;
    }
    
    VMPrint("VMMain testing VMMemoryPoolCreate.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolCreate(NULL, 64, &MemoryPool1)){
        VMPrint("VMMemoryPoolCreate doesn't handle NULL base.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolCreate((void *)MemoryBase1, 0, &MemoryPool1)){
        VMPrint("VMMemoryPoolCreate doesn't handle zero size.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolCreate((void *)MemoryBase1, 64, NULL)){
        VMPrint("VMMemoryPoolCreate doesn't handle NULL memoryid.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolCreate((void *)MemoryBase1, 64, &MemoryPool1)){
        VMPrint("VMMemoryPoolCreate doesn't return success with valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMMemoryPoolCreate appears OK.\n");
    if(VM_STATUS_SUCCESS != VMMemoryPoolAllocate(MemoryPool1, 32, (void **)&LocalAllocation)){
        VMPrint("VMMemoryPoolAllocate doesn't return success with valid inputs.\n");    
        return;
    }
    if((LocalAllocation != MemoryBase1)&&(LocalAllocation != (MemoryBase1 + 32))){
        VMPrint("VMMemoryPoolAllocate allocated from the wrong pool.\n");    
        return;        
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolQuery(MemoryPool1, &SystemPoolSize)){
        VMPrint("VMMemoryPoolQuery doesn't return success with valid inputs.\n");    
        return;
    }
    if(0 != SystemPoolSize){
        VMPrint("VMMemoryPoolQuery doesn't keep track of actual available memory.\n");    
        return;        
    }
    VMPrint("VMMain testing VMMemoryPoolDeallocate.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolDeallocate(MemoryPool1, NULL)){
        VMPrint("VMMemoryPoolDeallocate doesn't handle NULL base.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolDeallocate(MemoryPool1, LocalAllocation + 1)){
        VMPrint("VMMemoryPoolDeallocate doesn't handle zero size.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolDeallocate(VM_MEMORY_POOL_ID_INVALID, LocalAllocation)){
        VMPrint("VMMemoryPoolDeallocate doesn't handle NULL memoryid.\n");    
        return;
    }
    VMPrint("VMMain VMMemoryPoolDeallocate appears OK.\n");
    VMPrint("VMMain testing VMMemoryPoolDelete.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolDelete(VM_MEMORY_POOL_ID_INVALID)){
        VMPrint("VMMemoryPoolDelete doesn't handle bad memoryid.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_STATE != VMMemoryPoolDelete(MemoryPool1)){
        VMPrint("VMMemoryPoolDelete doesn't handle allocated memory pools.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolDeallocate(MemoryPool1, LocalAllocation)){
        VMPrint("VMMemoryPoolDeallocate doesn't handle valid inputs.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMemoryPoolDelete(MemoryPool1)){
        VMPrint("VMMemoryPoolDelete doesn't handle valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMMemoryPoolDelete appears OK.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolAllocate(MemoryPool1, 32, (void **)&LocalAllocation)){
        VMPrint("VMMemoryPoolAllocate doesn't handle bad memoryid.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMemoryPoolQuery(MemoryPool1, &SystemPoolSize)){
        VMPrint("VMMemoryPoolQuery doesn't handle bad memoryid.\n");    
        return;
    }
    VMPrint("VMMain testing VMThreadID.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMThreadID(NULL)){
        VMPrint("VMThreadID doesn't handle NULL threadref.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMThreadID(&ThisThreadID)){
        VMPrint("VMThreadID doesn't handle valid threadref.\n");    
        return;
    }
    BadThreadID = (OtherThreadID > ThisThreadID ? OtherThreadID : ThisThreadID) + 16;
    VMPrint("VMMain VMThreadID appears OK.\n");
    VMPrint("VMMain testing VMThreadState.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMThreadState(OtherThreadID, NULL)){
        VMPrint("VMThreadState doesn't handle NULL state.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_ID != VMThreadState(BadThreadID, &VMState)){
        VMPrint("VMThreadState doesn't handle bad thread.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMThreadState(OtherThreadID, &VMState)){
        VMPrint("VMThreadState doesn't handle valid inputs.\n");    
        return;
    }
    if(VM_THREAD_STATE_DEAD != VMState){
        VMPrint("VMThreadState returned the wrong value for state.\n");    
        return;        
    }
    VMPrint("VMMain VMThreadState appears OK.\n");
    VMPrint("VMMain testing VMTickMS.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMTickMS(NULL)){
        VMPrint("VMTickMS doesn't handle NULL ticksmsref.\n");    
        return;
    }
    MSPerTick = 100000;
    if(VM_STATUS_SUCCESS != VMTickMS(&MSPerTick)){
        VMPrint("VMTickMS doesn't handle valid inputs.\n");    
        return;
    }
    if((1 > MSPerTick)||(500 <= MSPerTick)){
        VMPrint("VMTickMS doesn't return proper value for tickmsref.\n");    
        return;        
    }
    VMPrint("VMMain VMTickMS appears OK.\n");
    VMPrint("VMMain testing VMTickCount.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMTickCount(NULL)){
        VMPrint("VMTickCount doesn't handle NULL tickref.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMTickCount(&CurrentTick)){
        VMPrint("VMTickCount doesn't handle valid inputs.\n");    
        return;
    }
    VMPrint("VMMain VMTickCount appears OK.\n");
    VMPrint("VMMain testing VMThreadTerminate.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMThreadTerminate(BadThreadID)){
        VMPrint("VMThreadTerminate doesn't handle bad thead.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_STATE != VMThreadTerminate(OtherThreadID)){
        VMPrint("VMThreadTerminate doesn't handle dead thead.\n");    
        return;
    }
    VMPrint("VMMain VMThreadTerminate appears OK.\n");
    VMPrint("VMMain testing VMMutexCreate.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMutexCreate(NULL)){
        VMPrint("VMMutexCreate doesn't handle NULL mutexref.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMutexCreate(&TheMutex)){
        VMPrint("VMMutexCreate doesn't handle valid inputs.\n");    
        return;
    }
    BadMutexID = TheMutex + 16;
    VMPrint("VMMain VMMutexCreate appears OK.\n");
    VMPrint("VMMain testing VMMutexQuery.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMMutexQuery(TheMutex, NULL)){
        VMPrint("VMMutexQuery doesn't handle NULL ownerref.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_ID != VMMutexQuery(BadMutexID, &MutexOwner)){
        VMPrint("VMMutexQuery doesn't handle bad mutex.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMutexQuery(TheMutex, &MutexOwner)){
        VMPrint("VMMutexQuery doesn't handle valid inputs.\n");    
        return;
    }
    if(VM_THREAD_ID_INVALID != MutexOwner){
        VMPrint("VMMutexQuery doesn't correct value for owner.\n");    
        return;        
    }
    VMPrint("VMMain VMMutexQuery appears OK.\n");
    VMPrint("VMMain testing VMThreadSleep.\n");
    if(VM_STATUS_ERROR_INVALID_PARAMETER != VMThreadSleep(VM_TIMEOUT_INFINITE)){
        VMPrint("VMThreadSleep doesn't handle VM_TIMEOUT_INFINITE.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMThreadSleep(VM_TIMEOUT_IMMEDIATE)){
        VMPrint("VMThreadSleep doesn't handle VM_TIMEOUT_IMMEDIATE.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    do{
        LastTick = CurrentTick;
        VMTickCount(&CurrentTick);
    }while(LastTick == CurrentTick);
    if(VM_STATUS_SUCCESS != VMThreadSleep(10)){
        VMPrint("VMThreadSleep doesn't handle valid input.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    if(CurrentTick < LastTick + 10){
        VMPrint("VMThreadSleep doesn't appear to sleep long enough.\n");    
        return;
    }
    VMPrint("VMMain VMThreadSleep appears OK.\n");
    VMPrint("VMMain testing VMThreadActivate.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMThreadActivate(BadThreadID)){
        VMPrint("VMThreadActivate doesn't handle bad thead.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    do{
        LastTick = CurrentTick;
        VMTickCount(&CurrentTick);
    }while(LastTick == CurrentTick);
    LastTick = CurrentTick;
    if(VM_STATUS_SUCCESS != VMThreadActivate(OtherThreadID)){
        VMPrint("VMThreadActivate doesn't handle valid inputs.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    if(CurrentTick != LastTick){
        VMPrint("VMThreadActivate doesn't schedule properly.\n");    
        return;
    }
    VMPrint("VMMain VMThreadActivate appears OK.\n");
    while(-1 == MemoryAllocationSuccess);
    if(0 == MemoryAllocationSuccess){
        VMPrint("VMMain stacks not allocated from system memory pool\n");  
        return;
    }
    while(!TheMutexAcquired);
    VMPrint("VMMain VMThread appears to have Acquired Mutex.\n");
    
    VMMutexQuery(TheMutex, &MutexOwner);
    if(MutexOwner != OtherThreadID){
        VMPrint("VMMutexQuery returned wrong owner of mutex.\n");    
        return;
    }
    
    VMPrint("VMMain testing VMMutexAcquire.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMMutexAcquire(BadMutexID, VM_TIMEOUT_INFINITE)){
        VMPrint("VMMutexAcquire doesn't handle bad mutex.\n");    
        return;
    }
    if(VM_STATUS_FAILURE != VMMutexAcquire(TheMutex, VM_TIMEOUT_IMMEDIATE)){
        VMPrint("VMMutexAcquire doesn't handle VM_TIMEOUT_IMMEDIATE.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    do{
        LastTick = CurrentTick;
        VMTickCount(&CurrentTick);
    }while(LastTick == CurrentTick);
    if(CurrentTick != LastTick + 2){
        VMPrint("Scheduler preemption not handled properly.\n");    
        return;
    }
    LastTick = CurrentTick;
    if(VM_STATUS_FAILURE != VMMutexAcquire(TheMutex, 10)){
        VMPrint("VMMutexAcquire doesn't handle timeout properly.\n");    
        return;
    }
    VMTickCount(&CurrentTick);
    if(CurrentTick < LastTick + 10){
        VMPrint("VMMutexAcquire doesn't handle timeout properly, woke too soon.\n");    
        return;
    }
    VMPrint("VMMain VMMutexAcquire appears OK.\n");
    VMPrint("VMMain testing VMThreadDelete.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMThreadDelete(BadThreadID)){
        VMPrint("VMThreadDelete doesn't handle bad thead.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_STATE != VMThreadDelete(OtherThreadID)){
        VMPrint("VMThreadDelete doesn't handle non-dead thead.\n");    
        return;
    }
    VMPrint("VMMain VMThreadDelete appears OK.\n");
    VMPrint("VMMain testing VMMutexDelete.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMMutexDelete(BadMutexID)){
        VMPrint("VMMutexDelete doesn't handle bad thead.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_STATE != VMMutexDelete(TheMutex)){
        VMPrint("VMMutexDelete doesn't handle held mutexes.\n");    
        return;
    }
    VMPrint("VMMain VMMutexDelete appears OK.\n");
    VMPrint("VMMain testing VMMutexRelease.\n");
    if(VM_STATUS_ERROR_INVALID_ID != VMMutexRelease(BadMutexID)){
        VMPrint("VMMutexRelease doesn't handle bad thead.\n");    
        return;
    }
    if(VM_STATUS_ERROR_INVALID_STATE != VMMutexRelease(TheMutex)){
        VMPrint("VMMutexRelease doesn't handle held mutexes.\n");    
        return;
    }
    VMPrint("VMMain VMMutexRelease appears OK.\n");
    VMPrint("VMMain terminating VMThread.\n");
    if(VM_STATUS_SUCCESS != VMThreadTerminate(OtherThreadID)){
        VMPrint("VMThreadTerminate doesn't handle valid termination.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMThreadState(OtherThreadID, &VMState)){
        VMPrint("VMThreadState doesn't handle valid inputs.\n");    
        return;
    }
    if(VM_THREAD_STATE_DEAD != VMState){
        VMPrint("VMThreadState returned the wrong value for state.\n");    
        return;        
    }
    VMPrint("VMMain VMThreadTerminate appears OK.\n");
    VMPrint("VMMain testing VMMutexAcquire\n");
    if(VM_STATUS_SUCCESS != VMMutexAcquire(TheMutex, VM_TIMEOUT_IMMEDIATE)){
        VMPrint("VMMutexAcquire doesn't handle terminated owners.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMutexQuery(TheMutex, &MutexOwner)){
        VMPrint("VMMutexQuery doesn't handle valid inputs.\n");    
        return;
    }
    if(MutexOwner != ThisThreadID){
        VMPrint("VMMutexQuery returned wrong owner of mutex.\n");    
        return;
    }
    VMPrint("VMMain VMMutexAcquire appears OK.\n");
    VMPrint("VMMain testing VMThreadDelete.\n");
    if(VM_STATUS_SUCCESS != VMThreadDelete(OtherThreadID)){
        VMPrint("VMThreadDelete doesn't handle dead thead.\n");    
        return;
    }
    VMPrint("VMMain VMThreadDelete appears OK.\n");
    
    VMPrint("VMMain testing VMMutexRelease.\n");
    if(VM_STATUS_SUCCESS != VMMutexRelease(TheMutex)){
        VMPrint("VMMutexRelease doesn't handle held mutexes.\n");    
        return;
    }
    if(VM_STATUS_SUCCESS != VMMutexQuery(TheMutex, &MutexOwner)){
        VMPrint("VMMutexQuery doesn't handle valid inputs.\n");    
        return;
    }
    if(VM_THREAD_ID_INVALID != MutexOwner){
        VMPrint("VMMutexQuery doesn't correct value for owner.\n");    
        return;        
    }
    VMPrint("VMMain VMMutexRelease appears OK.\n");
    VMPrint("VMMain testing VMMutexDelete.\n");
    if(VM_STATUS_SUCCESS != VMMutexDelete(TheMutex)){
        VMPrint("VMMutexDelete doesn't handle held mutexes.\n");    
        return;
    }
    VMPrint("VMMain VMMutexDelete appears OK.\n");
    
    VMPrint("VMMain everything appears fine.\nGoodbye\n");
    
}

