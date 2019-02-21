#include "Machine.h"
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <map>

extern "C"{

#ifndef NULL
#define NULL (void *)0
#endif

#define MACHINE_REQUEST_NONE            1
#define MACHINE_REQUEST_OPEN            2
#define MACHINE_REQUEST_READ            3
#define MACHINE_REQUEST_WRITE           4
#define MACHINE_REQUEST_SEEK            5
#define MACHINE_REQUEST_CLOSE           6
#define MACHINE_REQUEST_TERMINATE       7

#define MACHINE_MAX_MESSAGE_SIZE        0x10000
#define MACHINE_PAGE_SIZE               4096
#define MACHINE_MAX_TRANSFER_SIZE       512

typedef struct{
    pid_t DParentPID;
    pid_t DChildPID;
    int DRequestChannel;
    int DReplyChannel;
    int DMMapFile;
    uint8_t *DSharedBase;
    size_t DSharedSize;
} SMachineData, *SMachineDataRef;

typedef struct{
    TMachineFileCallback DCallback;
    void *DCalldata;
} SMachinePendingCallback, *SMachinePendingCallbackRef;

typedef struct{
    long DType;
    uint32_t DRequestID;
    uint8_t DPayload[1];
} SMachineRequest, *SMachineRequestRef;

typedef struct{
    uint32_t DRequestID;
    int DFileDescriptor;
    int DLength;
    uint8_t *DBuffer;
} SMachinePendingRead, *SMachinePendingReadRef;

static bool MachineInitialized = false;
static SMachineData MachineData;
static SMachineContext MachineContextCaller;
static sig_atomic_t MachineContextCalled;
static SMachineContextRef MachineContextCreateRef;
static void (*MachineContextCreateFunction)(void *);
static void *MachineContextCreateParam;
static sigset_t MachineContextCreateSignals;
//static volatile sig_atomic_t MachinePendingRequest = false;
static int MachineSignalPipe[2];
static TMachineAlarmCallback MachineAlarmCallback = NULL;
static void *MachineAlarmCalldata = NULL;
struct sigaction MachineAlarmActionSave;
static volatile uint32_t MachineRequestID = 0;
static std::map< uint32_t , SMachinePendingCallback > MachinePendingCallbacks;

void MachineContextCreateTrampoline(int sig);
void MachineContextCreateBoot(void);

void MachineContextCreate(SMachineContextRef mcntxref, void (*entry)(void *), void *param, void *stackaddr, size_t stacksize){
    struct sigaction SigAction;
    struct sigaction OldSigAction;
    stack_t SigStack;
    stack_t OldSigStack;
    sigset_t OldSigSet;
    sigset_t SigSet;
    
    // Step 1: 
    sigemptyset(&SigSet);
    sigaddset(&SigSet, SIGUSR1);
    sigprocmask(SIG_BLOCK, &SigSet, &OldSigSet);
    
    // Step 2: 
    memset((void *)&SigAction, 0, sizeof(struct sigaction));
    SigAction.sa_handler = MachineContextCreateTrampoline;
    SigAction.sa_flags = SA_ONSTACK;
    sigemptyset(&SigAction.sa_mask);
    sigaction(SIGUSR1, &SigAction, &OldSigAction);
    
    // Step 3: 
    SigStack.ss_sp = stackaddr;
    SigStack.ss_size = stacksize;
    SigStack.ss_flags = 0;
    sigaltstack(&SigStack, &OldSigStack);
    
    // Step 4: 
    MachineContextCreateRef = mcntxref;
    MachineContextCreateFunction = entry;
    MachineContextCreateParam = param;
    MachineContextCreateSignals = OldSigSet;
    MachineContextCalled = false;
    kill(getpid(), SIGUSR1);
    sigfillset(&SigSet);
    sigdelset(&SigSet, SIGUSR1);
    while (!MachineContextCalled){
        sigsuspend(&SigSet);
    }
    
    // Step 6: 
    sigaltstack(NULL, &SigStack);
    SigStack.ss_flags = SS_DISABLE;
    sigaltstack(&SigStack, NULL);
    if(!(OldSigStack.ss_flags & SS_DISABLE)){
        sigaltstack(&OldSigStack, NULL);
    }
    sigaction(SIGUSR1, &OldSigAction, NULL);
    sigprocmask(SIG_SETMASK, &OldSigSet, NULL);
    
    // Step 7 & Step 8: 
    MachineContextSwitch(&MachineContextCaller, mcntxref);
    
    // Step 14: 
    return;
}

void MachineContextCreateTrampoline(int sig){
    // Step 5: 
    if(MachineContextSave(MachineContextCreateRef) == 0){
        MachineContextCalled = true;
        return;
    }
    
    // Step 9: 
    MachineContextCreateBoot();
}

void MachineContextCreateBoot(void){
    void (*MachineContextStartFunction)(void *);
    void *MachineContextStartParam;
    
    // Step 10: 
    sigprocmask(SIG_SETMASK, &MachineContextCreateSignals, NULL);
    
    // Step 11: 
    MachineContextStartFunction = MachineContextCreateFunction;
    MachineContextStartParam = MachineContextCreateParam;
    
    // Step 12 & Step 13: 
    MachineContextSwitch(MachineContextCreateRef, &MachineContextCaller);
    
    // The thread "magically" starts... 
    MachineContextStartFunction(MachineContextStartParam);
    
    // NOTREACHED 
    abort();
}

int MachineGetInt(uint8_t *ptr){
    int Value = 0;
    for(size_t Index = 0; Index < sizeof(int); Index++){
        Value <<= 8;
        Value |= ptr[Index];
    }
    return Value;
}

uint8_t *MachineGetPointer(uint8_t *ptr){
    uint8_t *Value = (uint8_t *)0;
    uint8_t *Dest = (uint8_t *)&Value;
    for(size_t Index = 0; Index < sizeof(uint8_t *); Index++){
        Dest[Index] = ptr[Index];
    }
    return Value;
}

void MachineSetInt(uint8_t *ptr, int val){
    for(size_t Index = 0; Index < sizeof(int); Index++){
        ptr[Index] = (val>>((sizeof(int) - Index - 1) * 8));
    }
}

void MachineSetPointer(uint8_t *ptr, uint8_t *val){
    uint8_t *Source = (uint8_t *)&val;
    for(size_t Index = 0; Index < sizeof(uint8_t *); Index++){
        ptr[Index] = Source[Index];//(val>>((sizeof(int) - Index - 1) * 8));
    }
}

bool MachineValidSharePointer(uint8_t *ptr){
    if(ptr < MachineData.DSharedBase){
        return false;   
    }
    if(ptr >= (MachineData.DSharedBase + MachineData.DSharedSize)){
        return false;
    }
    return true;
}

void MachineRequestSignalHandler(int signum){
    uint8_t TempByte = 0;
    write(MachineSignalPipe[1],&TempByte, 1);
}

void MachineReplySignalHandler(int signum){
    uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
    SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
    ssize_t MessageSize;

    do{
        MessageSize = msgrcv(MachineData.DReplyChannel, MessageRef, sizeof(Buffer), 0, IPC_NOWAIT);
        if(0 < MessageSize){
            if(MachinePendingCallbacks.end() != MachinePendingCallbacks.find(MessageRef->DRequestID)){
                SMachinePendingCallback Callinfo = MachinePendingCallbacks[MessageRef->DRequestID];
                int ReturnValue = MachineGetInt(MessageRef->DPayload);
                MachinePendingCallbacks.erase(MessageRef->DRequestID);
                Callinfo.DCallback(Callinfo.DCalldata, ReturnValue);
            }
            else{
                fprintf(stderr,"\n*****UKNOWN Reply %u*****\n",MessageRef->DRequestID);
            }
        }
    }while(0 < MessageSize);
    
    
}

uint32_t MachineAddRequest(TMachineFileCallback callback, void *calldata){
    SMachinePendingCallback Callback;
    
    Callback.DCallback = callback;
    Callback.DCalldata = calldata;
    
    MachineRequestID++;
    MachinePendingCallbacks[(uint32_t)MachineRequestID] = Callback;
    return MachineRequestID;
}

void MachineSendReply(SMachineRequestRef mess, int length){
    msgsnd(MachineData.DReplyChannel, mess, length, 0);
    kill(MachineData.DParentPID, SIGUSR2);
}

void *MachineInitialize(size_t sharesize){
    TMachineSignalState SigStateSave;
    struct sigaction OldSigAction, SigAction;
    uint8_t TempPage[MACHINE_PAGE_SIZE];
    int PageCount = ((sizeof(TempPage) - 1) + sharesize)/sizeof(TempPage);
    
    if(MachineInitialized){
        return NULL;
    }
    
    sigaction(SIGALRM, NULL, &MachineAlarmActionSave);
    MachineData.DParentPID = getpid();
    MachineData.DRequestChannel = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if(0 > MachineData.DRequestChannel){
        fprintf(stderr,"Failed to create message queue: %s\n", strerror(errno));
        exit(1);
    }
    MachineData.DReplyChannel = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if(0 > MachineData.DReplyChannel){
        msgctl(MachineData.DRequestChannel, IPC_RMID, NULL);
        fprintf(stderr,"Failed to create message queue: %s\n", strerror(errno));
        exit(1);
    }
    MachineData.DMMapFile = open("./vm_shmem", O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
    if(0 > MachineData.DMMapFile){
        msgctl(MachineData.DReplyChannel, IPC_RMID, NULL);
        msgctl(MachineData.DRequestChannel, IPC_RMID, NULL);
        fprintf(stderr,"Failed to create shared memory file: %s\n", strerror(errno));
        exit(1);
    }
    memset(TempPage,0,sizeof(TempPage));
    for(int Index = 0; Index < PageCount; Index++){
        write(MachineData.DMMapFile,TempPage,sizeof(TempPage));   
    }
    MachineData.DSharedSize = sizeof(TempPage) * PageCount;
    MachineData.DSharedBase = (uint8_t *)mmap(NULL, MachineData.DSharedSize, PROT_READ | PROT_WRITE, MAP_SHARED, MachineData.DMMapFile, 0);
    if(MAP_FAILED == MachineData.DSharedBase){
        close(MachineData.DMMapFile);
        unlink("./vm_shmem");
        msgctl(MachineData.DReplyChannel, IPC_RMID, NULL);
        msgctl(MachineData.DRequestChannel, IPC_RMID, NULL);
        fprintf(stderr,"Failed to map shared memory file: %s\n", strerror(errno));
        exit(1);
    }
    
    
    MachineSuspendSignals(&SigStateSave);
    
    MachineData.DChildPID = fork();
    if(0 == MachineData.DChildPID){
        bool Terminated = false;
        std::vector< struct pollfd > PollFDs;
        std::vector< SMachinePendingRead > PendingReads;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        ssize_t MessageSize;
        sigset_t SigMask;
        int Result, FileDescriptor, Length, Flags, Mode;
        int Offset, Whence;
        uint8_t *BufferPointer;
        
        MachineData.DChildPID = getpid();
        pipe(MachineSignalPipe);
        PollFDs.resize(1);
        PollFDs[0].fd = MachineSignalPipe[0];
        PollFDs[0].events = POLLIN;
        PollFDs[0].revents = 0;
        memset((void *)&SigAction, 0, sizeof(struct sigaction));
        SigAction.sa_handler = MachineRequestSignalHandler;
        sigemptyset(&SigAction.sa_mask);
        sigaction(SIGUSR2, &SigAction, &OldSigAction);
        MachineEnableSignals();
        while(!Terminated){
            sigemptyset(&SigMask);
            PollFDs[0].events = POLLIN;
            PollFDs[0].revents = 0;
            Result = poll(PollFDs.data(), PollFDs.size(), 1);
            if((0 < Result)&&(PollFDs[0].revents)){
                SMachinePendingRead PendingRead;
                bool Found;
                uint8_t TempByte;

                read(PollFDs[0].fd, &TempByte, 1);
                while(true){
                    MessageSize = msgrcv(MachineData.DRequestChannel, MessageRef, sizeof(Buffer), 0, IPC_NOWAIT);
                    if(0 < MessageSize){
                        switch(MessageRef->DType){
                            case MACHINE_REQUEST_NONE:          break;
                            case MACHINE_REQUEST_OPEN:          Flags = MachineGetInt(MessageRef->DPayload + strlen((char *)MessageRef->DPayload) + 1);
                                                                Mode = MachineGetInt(MessageRef->DPayload + strlen((char *)MessageRef->DPayload) + sizeof(int) + 1);
                                                                FileDescriptor = open((char *)MessageRef->DPayload, Flags, Mode);
                                                                MachineSetInt(MessageRef->DPayload, FileDescriptor);
                                                                MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1);
                                                                break;
                            case MACHINE_REQUEST_READ:          PendingRead.DRequestID = MessageRef->DRequestID;
                                                                PendingRead.DFileDescriptor = MachineGetInt(MessageRef->DPayload);
                                                                PendingRead.DLength = MachineGetInt(MessageRef->DPayload + sizeof(int));
                                                                PendingRead.DBuffer = MachineGetPointer(MessageRef->DPayload + sizeof(int) * 2);
                                                                if(MachineValidSharePointer(PendingRead.DBuffer) && (PendingRead.DLength <= MACHINE_MAX_TRANSFER_SIZE)){
                                                                    Found = false;
                                                                    for(size_t Index = 0; Index < PollFDs.size(); Index++){
                                                                        if(PollFDs[Index].fd == PendingRead.DFileDescriptor){
                                                                            Found = true;
                                                                            break;
                                                                        }
                                                                    }
                                                                    if(!Found){
                                                                        struct pollfd NewReadFD;
                                                                        
                                                                        NewReadFD.fd = PendingRead.DFileDescriptor;
                                                                        NewReadFD.events = POLLIN;
                                                                        NewReadFD.revents = 0;
                                                                        PollFDs.push_back(NewReadFD);
                                                                    }
                                                                    PendingReads.push_back(PendingRead);
                                                                }
                                                                else{
                                                                    MachineSetInt(MessageRef->DPayload, -1);
                                                                    MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1); 
                                                                }
                                                                break;
                            case MACHINE_REQUEST_WRITE:         FileDescriptor = MachineGetInt(MessageRef->DPayload);
                                                                Length = MachineGetInt(MessageRef->DPayload + sizeof(int));
                                                                BufferPointer = MachineGetPointer(MessageRef->DPayload + sizeof(int) * 2);
                                                                if(MachineValidSharePointer(BufferPointer) && (Length <= MACHINE_MAX_TRANSFER_SIZE)){
                                                                    do{
                                                                        Result = write(FileDescriptor, BufferPointer, Length);
                                                                    }while((-1 == Result) && (EINTR == errno));
                                                                    MachineSetInt(MessageRef->DPayload, Result);
                                                                }
                                                                else{
                                                                    MachineSetInt(MessageRef->DPayload, -1);
                                                                }
                                                                MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1);
                                                                break;
                            case MACHINE_REQUEST_SEEK:          FileDescriptor = MachineGetInt(MessageRef->DPayload);
                                                                Offset = MachineGetInt(MessageRef->DPayload + sizeof(int));
                                                                Whence = MachineGetInt(MessageRef->DPayload + sizeof(int) * 2);
                                                                Offset = lseek(FileDescriptor, Offset, Whence);
                                                                MachineSetInt(MessageRef->DPayload, Offset);
                                                                MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1);
                                                                break;
                            case MACHINE_REQUEST_CLOSE:         FileDescriptor = close(MachineGetInt(MessageRef->DPayload));
                                                                MachineSetInt(MessageRef->DPayload, FileDescriptor);
                                                                MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1);
                                                                break;
                            case MACHINE_REQUEST_TERMINATE:     Terminated = true;
                            default:                            break;
                        }
                    }
                    else{
                        break;   
                    }   
                }
            }
            else if(0 == Result){
                if(0 > kill(MachineData.DParentPID, 0)){
                    if(ESRCH == errno){
                        Terminated = true;
                    }
                }
            }
            for(size_t Index = 1; Index < PollFDs.size(); Index++){
                if(PollFDs[Index].revents){
                    for(size_t ReadIndex = 0; ReadIndex < PendingReads.size(); ReadIndex++){
                        if(PendingReads[ReadIndex].DFileDescriptor == PollFDs[Index].fd){
                            do{
                                Result = read(PendingReads[ReadIndex].DFileDescriptor, PendingReads[ReadIndex].DBuffer, PendingReads[ReadIndex].DLength);
                            }while((-1 == Result) && (EINTR == errno));
                            MessageRef->DRequestID = PendingReads[ReadIndex].DRequestID;
                            MachineSetInt(MessageRef->DPayload, Result);
                            MachineSendReply(MessageRef,sizeof(SMachineRequest) + sizeof(int) - 1);
                            PendingReads.erase(PendingReads.begin() + ReadIndex);
                            break;
                        }
                    }
                }
            }
            for(size_t Index = 1; Index < PollFDs.size();){
                bool Found = false;
                for(size_t ReadIndex = 0; ReadIndex < PendingReads.size(); ReadIndex++){
                    if(PendingReads[ReadIndex].DFileDescriptor == PollFDs[Index].fd){
                        Found = true;
                    }
                }
                if(Found){
                    Index++;
                }
                else{
                    PollFDs.erase(PollFDs.begin() + Index);
                }
            }
        }
        msgctl(MachineData.DRequestChannel, IPC_RMID, NULL);
        msgctl(MachineData.DReplyChannel, IPC_RMID, NULL);
        close(MachineData.DMMapFile);
        unlink("./vm_shmem");
        sigaction(SIGUSR2, &OldSigAction, NULL);
        MachineResumeSignals(&SigStateSave);
        close(MachineSignalPipe[0]);
        close(MachineSignalPipe[1]);
        exit(0);
    }
    memset((void *)&SigAction, 0, sizeof(struct sigaction));
    SigAction.sa_handler = MachineReplySignalHandler;
    sigemptyset(&SigAction.sa_mask);
    sigaction(SIGUSR2, &SigAction, &OldSigAction);
    MachineInitialized = true;
    MachineResumeSignals(&SigStateSave);
    return MachineData.DSharedBase;
}

void MachineTerminate(void){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        int Status;
        
        MachineSuspendSignals(&SignalState);
        
        sigaction(SIGALRM, &MachineAlarmActionSave, NULL);
        
        MessageRef->DType = MACHINE_REQUEST_TERMINATE;
        ualarm(0,0);
        MessageRef->DRequestID = MachineAddRequest(NULL, NULL);
        close(MachineData.DMMapFile);
        Status = msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) - 1, 0);
        kill(MachineData.DChildPID, SIGUSR2);
        wait(&Status);
        MachineResumeSignals(&SignalState);
    }
    
}

void MachineEnableSignals(void){
    sigset_t NewSigset, OldSigset;
    sigfillset(&NewSigset);
    sigprocmask(SIG_UNBLOCK, &NewSigset, &OldSigset);    
}

void MachineSuspendSignals(TMachineSignalStateRef sigstate){
    sigset_t NewSigset;
    sigfillset(&NewSigset);
    sigprocmask(SIG_BLOCK, &NewSigset, sigstate);
}

void MachineResumeSignals(TMachineSignalStateRef sigstate){
    sigset_t OldSigset;

    sigprocmask(SIG_SETMASK, sigstate, &OldSigset);
}

void MachineAlarmSignalHandler(int signum){
    if(MachineAlarmCallback){
        MachineAlarmCallback(MachineAlarmCalldata); 
    }
}

void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata){
    if(MachineInitialized){
        struct sigaction NewAction;
        
        memset((void *)&NewAction, 0, sizeof(struct sigaction));
        NewAction.sa_handler = MachineAlarmSignalHandler;
        sigfillset(&NewAction.sa_mask);
        sigdelset(&NewAction.sa_mask, SIGALRM);
        NewAction.sa_flags = SA_NODEFER;
    
        MachineAlarmCallback = callback;
        MachineAlarmCalldata = calldata;      
        sigaction(SIGALRM, &NewAction, &MachineAlarmActionSave);
        ualarm(usec * 2, usec);
    }
}

void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        
        MessageRef->DType = MACHINE_REQUEST_OPEN;
        strcpy((char *)MessageRef->DPayload, filename);
        MachineSetInt(MessageRef->DPayload + strlen(filename) + 1, flags);
        MachineSetInt(MessageRef->DPayload + strlen(filename) + sizeof(int) + 1, mode);
        
        MachineSuspendSignals(&SignalState);
        MessageRef->DRequestID = MachineAddRequest(callback, calldata);
        msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) + strlen(filename) + sizeof(int), 0);
        kill(MachineData.DChildPID, SIGUSR2);
        MachineResumeSignals(&SignalState);
    }
}

void MachineFileRead(int fd, void *data, int length, TMachineFileCallback callback, void *calldata){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        
        MessageRef->DType = MACHINE_REQUEST_READ;
        MachineSetInt(MessageRef->DPayload, fd);
        MachineSetInt(MessageRef->DPayload + sizeof(int), length);
        MachineSetPointer(MessageRef->DPayload + sizeof(int) * 2, (uint8_t *)data);
        
        MachineSuspendSignals(&SignalState);
        MessageRef->DRequestID = MachineAddRequest(callback, calldata);
        msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) + 2 * sizeof(int) + sizeof(uint8_t *) - 1, 0);
        kill(MachineData.DChildPID, SIGUSR2);
        MachineResumeSignals(&SignalState);
    }
}

void MachineFileWrite(int fd, void *data, int length, TMachineFileCallback callback, void *calldata){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        
        MessageRef->DType = MACHINE_REQUEST_WRITE;
        MachineSetInt(MessageRef->DPayload, fd);
        MachineSetInt(MessageRef->DPayload + sizeof(int), length);
        MachineSetPointer(MessageRef->DPayload + sizeof(int) * 2, (uint8_t *)data);
        
        MachineSuspendSignals(&SignalState);
        MessageRef->DRequestID = MachineAddRequest(callback, calldata);
        msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) + 2 * sizeof(int) + sizeof(uint8_t *) - 1, 0);
        kill(MachineData.DChildPID, SIGUSR2);
        MachineResumeSignals(&SignalState);
    }
}

void MachineFileSeek(int fd, int offset, int whence, TMachineFileCallback callback, void *calldata){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        
        MessageRef->DType = MACHINE_REQUEST_SEEK;
        MachineSetInt(MessageRef->DPayload, fd);
        MachineSetInt(MessageRef->DPayload + sizeof(int), offset);
        MachineSetInt(MessageRef->DPayload + sizeof(int) * 2, whence);
        
        MachineSuspendSignals(&SignalState);
        MessageRef->DRequestID = MachineAddRequest(callback, calldata);
        msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) + 3 * sizeof(int) - 1, 0);
        kill(MachineData.DChildPID, SIGUSR2);
        MachineResumeSignals(&SignalState);
    }
}

void MachineFileClose(int fd, TMachineFileCallback callback, void *calldata){
    if(MachineInitialized){
        TMachineSignalState SignalState;
        uint8_t Buffer[MACHINE_MAX_MESSAGE_SIZE];
        SMachineRequestRef MessageRef = (SMachineRequestRef)Buffer;
        
        MessageRef->DType = MACHINE_REQUEST_CLOSE;
        MachineSetInt(MessageRef->DPayload, fd);
        
        MachineSuspendSignals(&SignalState);
        MessageRef->DRequestID = MachineAddRequest(callback, calldata);
        msgsnd(MachineData.DRequestChannel, MessageRef, sizeof(SMachineRequest) + sizeof(int) - 1, 0);
        kill(MachineData.DChildPID, SIGUSR2);
        MachineResumeSignals(&SignalState);
    }
}

} // End of extern "C"
