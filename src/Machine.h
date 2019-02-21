#ifndef MACHINE_H
#define MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>

typedef struct{
    jmp_buf DJumpBuffer;
} SMachineContext, *SMachineContextRef;

// save machine context 
#define MachineContextSave(mcntx)                   \
    setjmp((mcntx)->DJumpBuffer)

// restore machine context 
#define MachineContextRestore(mcntx)                \
    longjmp((mcntx)->DJumpBuffer, 1)

// switch machine context 
#define MachineContextSwitch(mcntxold,mcntxnew)    \
    if(setjmp((mcntxold)->DJumpBuffer) == 0) longjmp((mcntxnew)->DJumpBuffer, 1)

// create machine context 
void MachineContextCreate(SMachineContextRef mcntxref, void (*entry)(void *), void *param, void *stackaddr, size_t stacksize);

typedef void (*TMachineAlarmCallback)(void *calldata);
typedef void (*TMachineFileCallback)(void *calldata, int result);
typedef sigset_t TMachineSignalState, *TMachineSignalStateRef;
void *MachineInitialize(size_t sharesize);
void MachineTerminate(void);
void MachineEnableSignals(void);
void MachineSuspendSignals(TMachineSignalStateRef sigstate);
void MachineResumeSignals(TMachineSignalStateRef sigstate);
void MachineRequestAlarm(useconds_t usec, TMachineAlarmCallback callback, void *calldata);
void MachineFileOpen(const char *filename, int flags, int mode, TMachineFileCallback callback, void *calldata);
void MachineFileRead(int fd, void *data, int length, TMachineFileCallback callback, void *calldata);
void MachineFileWrite(int fd, void *data, int length, TMachineFileCallback callback, void *calldata);
void MachineFileSeek(int fd, int offset, int whence, TMachineFileCallback callback, void *calldata);
void MachineFileClose(int fd, TMachineFileCallback callback, void *calldata);


#ifdef __cplusplus
}
#endif

#endif

