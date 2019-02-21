#include "VirtualMachine.h"
#include <fcntl.h>

#include <stdio.h>
#ifndef NULL
#define NULL    ((void *)0)
#endif

#define QUEUE_BUFFER_SIZE  1024

typedef struct{
    volatile int DHead;
    volatile int DTail;
    volatile int DCount;
    char DBuffer[QUEUE_BUFFER_SIZE];
    TVMMutexID DMutex; 
} SProtectedQueue, *SProtectedQueueRef;

typedef struct WAIT_NODE {
    volatile int *DFlag;
    struct WAIT_NODE *DNext;
} SWaitNode, *SWaitNodeRef;

typedef struct{
    volatile int DValue;
    volatile int DWaits;
    TVMMutexID DMutex;
    SWaitNodeRef DHead;
    SWaitNodeRef DTail;
} SSemaphore, *SSemaphoreRef;

TVMThreadID VMThreadIDProducer, VMThreadIDConsumer;
SSemaphore Empty;
SSemaphore Full;
SProtectedQueue SharedQueue;
volatile int TotalBytesRead = 0; 
volatile int TotalBytesWritten = 0;
volatile int TotalEnqueued = 0;
volatile int TotalDequeued = 0;

void Enqueue(SProtectedQueueRef q, char val){
    VMMutexAcquire(q->DMutex, VM_TIMEOUT_INFINITE);
    q->DBuffer[q->DTail] = val;
    q->DTail++;
    q->DTail %= QUEUE_BUFFER_SIZE;
    q->DCount++;
    VMMutexRelease(q->DMutex);
}

char Dequeue(SProtectedQueueRef q){
    char Val;
    VMMutexAcquire(q->DMutex, VM_TIMEOUT_INFINITE);
    Val = q->DBuffer[q->DHead];
    q->DHead++;
    q->DHead %= QUEUE_BUFFER_SIZE;
    q->DCount--;
    VMMutexRelease(q->DMutex);
    return Val;
}

void Down(SSemaphoreRef s){
    volatile int WaitingFlag;
    SWaitNode WaitingNode;
    VMMutexAcquire(s->DMutex, VM_TIMEOUT_INFINITE);
    s->DValue--;
    if(s->DValue < 0){
        s->DWaits++;
        WaitingFlag = 1;
        WaitingNode.DFlag = &WaitingFlag;
        WaitingNode.DNext = NULL;
        if(NULL == s->DTail){
            s->DHead = s->DTail = &WaitingNode;
        }
        while(WaitingFlag){
            VMMutexRelease(s->DMutex);
            VMThreadSleep(VM_TIMEOUT_IMMEDIATE);
            VMMutexAcquire(s->DMutex, VM_TIMEOUT_INFINITE);   
        }
    }
    VMMutexRelease(s->DMutex);
}

void Up(SSemaphoreRef s){
    SWaitNodeRef WaitingNode;
    VMMutexAcquire(s->DMutex, VM_TIMEOUT_INFINITE);
    s->DValue++;
    if(s->DValue <= 0){
        if(NULL != s->DHead){
            WaitingNode = s->DHead;
            s->DHead = s->DHead->DNext;
            if(NULL == s->DHead){
                s->DTail = NULL;   
            }
            *WaitingNode->DFlag = 0;
        }
    }
    VMMutexRelease(s->DMutex);
}



void VMThreadProducer(void *param){
    int FileDescriptor;
    int BytesRead, Index;
    unsigned char Buffer[127];
    
    VMPrint("VMThreadProducer Alive\n");
    if(VM_STATUS_SUCCESS != VMFileOpen((const char *)param, O_RDONLY, 0644, &FileDescriptor)){
        VMPrint("VMThreadProducer failed to open file %s\n",(const char *)param);
        Down(&Empty);
        Enqueue(&SharedQueue,0xC0);
        Up(&Full);
        return;
    }
    do{
        BytesRead = sizeof(Buffer);
        if(VM_STATUS_SUCCESS == VMFileRead(FileDescriptor, (void *)Buffer, &BytesRead)){
            if(BytesRead){
                TotalBytesRead += BytesRead;
                Index = 0;
                while(Index < BytesRead){
                    if((0xC0 != Buffer[Index])&&(0xDB != Buffer[Index])){
                        Down(&Empty);
                        Enqueue(&SharedQueue,Buffer[Index]);
                        Up(&Full);
                    }
                    else{
                        Down(&Empty);
                        Enqueue(&SharedQueue,0xDB);
                        Up(&Full);
                        if(0xC0 == Buffer[Index]){
                            Down(&Empty);
                            Enqueue(&SharedQueue,0xDD);
                            Up(&Full);
                        }
                        else{
                            Down(&Empty);
                            Enqueue(&SharedQueue,0xDC);
                            Up(&Full);                            
                        }
                    }
                    TotalEnqueued++;
                    Index++;
                }
            }
        }
    }while(BytesRead);
    Down(&Empty);
    Enqueue(&SharedQueue,0xC0);
    Up(&Full);
    VMFileClose(FileDescriptor);
    VMPrint("VMThreadProducer Complete\n");
}


void VMThreadConsumer(void *param){
    int FileDescriptor;
    int BytesWritten, Index;
    unsigned char Buffer[33];
    int Done = 0;
    
    VMPrint("VMThreadConsumer Alive\n");
    if(VM_STATUS_SUCCESS != VMFileOpen((const char *)param, O_CREAT | O_TRUNC | O_RDWR, 0644, &FileDescriptor)){
        VMPrint("VMThreadConsumer failed to open file %s\n",(const char *)param);
        return;
    }
    do{
        Index = 0;
        while(Index < sizeof(Buffer)){
            Down(&Full);
            Buffer[Index] = Dequeue(&SharedQueue);
            Up(&Empty);
            if(0xDB == Buffer[Index]){
                Down(&Full);
                Buffer[Index] = Dequeue(&SharedQueue);
                Up(&Empty);
                if(0xDD == Buffer[Index]){
                    Buffer[Index] = 0xC0;   
                }
                else{
                    Buffer[Index] = 0xDB;   
                }
            }
            else if(0xC0 == Buffer[Index]){
                Done = 1;
                break;
            }
            TotalDequeued++;
            Index++;
        }
        if(Index){
            BytesWritten = Index;
            VMFileWrite(FileDescriptor, Buffer, &BytesWritten);
            TotalBytesWritten += BytesWritten;
            
        }
    }while(!Done);
    VMFileClose(FileDescriptor);
    VMPrint("VMThreadConsumer Complete\n");
}

void VMMain(int argc, char *argv[]){
    TVMThreadState VMStateP, VMStateC;
    int LocalRead, LocalWrite, LocalEnqueue, LocalDequeue, LocalCount, LocalWaits;
    if(argc != 3){
        VMPrint("VMMain invalid number of arguments. Should be copyfile src dest\n");
        return;
    }
    
    VMPrint("VMMain creating semaphore and queue mutexes\n");    
    VMMutexCreate(&Empty.DMutex);
    Empty.DValue = sizeof(SharedQueue.DBuffer);
    Empty.DWaits = 0;
    Empty.DHead = Empty.DTail = NULL;
    VMMutexCreate(&Full.DMutex);
    Full.DValue = 0;
    Full.DWaits = 0;
    Full.DHead = Full.DTail = NULL;
    VMMutexCreate(&SharedQueue.DMutex);
    SharedQueue.DHead = SharedQueue.DTail = SharedQueue.DCount = 0;
    VMPrint("VMMain creating threads\n");        
    VMThreadCreate(VMThreadProducer, argv[1], 0x100000, VM_THREAD_PRIORITY_LOW, &VMThreadIDProducer);
    VMThreadCreate(VMThreadConsumer, argv[2], 0x100000, VM_THREAD_PRIORITY_LOW, &VMThreadIDConsumer);
    VMPrint("VMMain activating threads\n");

    VMThreadActivate(VMThreadIDProducer);
    VMThreadActivate(VMThreadIDConsumer);
    VMPrint("VMMain Waiting\n");
    do{
        LocalRead = TotalBytesRead; 
        LocalWrite = TotalBytesWritten;
        LocalEnqueue = TotalEnqueued;
        LocalDequeue = TotalDequeued;
        LocalCount = SharedQueue.DCount;
        LocalWaits = Empty.DWaits;
        VMThreadState(VMThreadIDProducer, &VMStateP);
        VMThreadState(VMThreadIDConsumer, &VMStateC);
        VMPrint("%d %d %d %d %d %d\n", LocalRead, LocalEnqueue, LocalCount, LocalDequeue, LocalWrite, LocalWaits);
        VMThreadSleep(2);
    }while((VM_THREAD_STATE_DEAD != VMStateP)||(VM_THREAD_STATE_DEAD != VMStateC));
    
    VMPrint("VMMain Goodbye\n");    
}

