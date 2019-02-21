#ifndef VIRTUALMACHINE_H
#define VIRTUALMACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#define VM_STATUS_FAILURE                       ((TVMStatus)0x00)
#define VM_STATUS_SUCCESS                       ((TVMStatus)0x01)
#define VM_STATUS_ERROR_INVALID_PARAMETER       ((TVMStatus)0x02)
#define VM_STATUS_ERROR_INVALID_ID              ((TVMStatus)0x03)
#define VM_STATUS_ERROR_INVALID_STATE           ((TVMStatus)0x04)
#define VM_STATUS_ERROR_INSUFFICIENT_RESOURCES  ((TVMStatus)0x05)
                                                
#define VM_THREAD_STATE_DEAD                    ((TVMThreadState)0x00)
#define VM_THREAD_STATE_RUNNING                 ((TVMThreadState)0x01)
#define VM_THREAD_STATE_READY                   ((TVMThreadState)0x02)
#define VM_THREAD_STATE_WAITING                 ((TVMThreadState)0x03)
                                                
#define VM_THREAD_PRIORITY_LOW                  ((TVMThreadPriority)0x01)
#define VM_THREAD_PRIORITY_NORMAL               ((TVMThreadPriority)0x02)
#define VM_THREAD_PRIORITY_HIGH                 ((TVMThreadPriority)0x03)
                                                
#define VM_THREAD_ID_INVALID                    ((TVMThreadID)-1)
                                                
#define VM_MUTEX_ID_INVALID                     ((TVMMutexID)-1)
                                                
#define VM_TIMEOUT_INFINITE                     ((TVMTick)0)
#define VM_TIMEOUT_IMMEDIATE                    ((TVMTick)-1)

#define VM_FILE_SYSTEM_MAX_PATH                 256
#define VM_FILE_SYSTEM_SFN_SIZE                 13
#define VM_FILE_SYSTEM_LFN_SIZE                 VM_FILE_SYSTEM_MAX_PATH
#define VM_FILE_SYSTEM_DIRECTORY_DELIMETER      '/'

#define VM_FILE_SYSTEM_ATTR_READ_ONLY           0x01 
#define VM_FILE_SYSTEM_ATTR_HIDDEN              0x02
#define VM_FILE_SYSTEM_ATTR_SYSTEM              0x04
#define VM_FILE_SYSTEM_ATTR_VOLUME_ID           0x08
#define VM_FILE_SYSTEM_ATTR_DIRECTORY           0x10
#define VM_FILE_SYSTEM_ATTR_ARCHIVE             0x20
                                   
typedef unsigned int TVMMemorySize, *TVMMemorySizeRef;
typedef unsigned int TVMStatus, *TVMStatusRef;
typedef unsigned int TVMTick, *TVMTickRef;
typedef unsigned int TVMThreadID, *TVMThreadIDRef;
typedef unsigned int TVMMutexID, *TVMMutexIDRef;
typedef unsigned int TVMThreadPriority, *TVMThreadPriorityRef;  
typedef unsigned int TVMThreadState, *TVMThreadStateRef;  
typedef unsigned int TVMMemoryPoolID, *TVMMemoryPoolIDRef;

typedef struct{
    unsigned int DYear;
    unsigned char DMonth;
    unsigned char DDay;
    unsigned char DHour;
    unsigned char DMinute;
    unsigned char DSecond;
    unsigned char DHundredth;
} SVMDateTime, *SVMDateTimeRef;

typedef struct{
    char DLongFileName[VM_FILE_SYSTEM_MAX_PATH];
    char DShortFileName[VM_FILE_SYSTEM_SFN_SIZE];
    unsigned int DSize;
    unsigned char DAttributes;
    SVMDateTime DCreate;
    SVMDateTime DAccess;
    SVMDateTime DModify;
} SVMDirectoryEntry, *SVMDirectoryEntryRef;

extern const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM;
#define VM_MEMORY_POOL_ID_INVALID               ((TVMMemoryPoolID)-1)

typedef void (*TVMMainEntry)(int, char*[]);
typedef void (*TVMThreadEntry)(void *);

TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]);

TVMStatus VMTickMS(int *tickmsref);
TVMStatus VMTickCount(TVMTickRef tickref);

TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid);
TVMStatus VMThreadDelete(TVMThreadID thread);
TVMStatus VMThreadActivate(TVMThreadID thread);
TVMStatus VMThreadTerminate(TVMThreadID thread);
TVMStatus VMThreadID(TVMThreadIDRef threadref);
TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref);
TVMStatus VMThreadSleep(TVMTick tick);

TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory);
TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory);
TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft);
TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer);
TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer);       

TVMStatus VMMutexCreate(TVMMutexIDRef mutexref);
TVMStatus VMMutexDelete(TVMMutexID mutex);
TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref);
TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout);     
TVMStatus VMMutexRelease(TVMMutexID mutex);

#define VMPrint(format, ...)        VMFilePrint ( 1,  format, ##__VA_ARGS__)
#define VMPrintError(format, ...)   VMFilePrint ( 2,  format, ##__VA_ARGS__)

TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor);
TVMStatus VMFileClose(int filedescriptor);      
TVMStatus VMFileRead(int filedescriptor, void *data, int *length);
TVMStatus VMFileWrite(int filedescriptor, void *data, int *length);
TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset);
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);

TVMStatus VMDateTime(SVMDateTimeRef curdatetime);

TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor);
TVMStatus VMDirectoryClose(int dirdescriptor);
TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent);
TVMStatus VMDirectoryRewind(int dirdescriptor);
TVMStatus VMDirectoryCurrent(char *abspath);
TVMStatus VMDirectoryChange(const char *path);
TVMStatus VMDirectoryCreate(const char *dirname);
TVMStatus VMDirectoryUnlink(const char *path);

TVMStatus VMFileSystemValidPathName(const char *name);
TVMStatus VMFileSystemIsRelativePath(const char *name);
TVMStatus VMFileSystemIsAbsolutePath(const char *name);
TVMStatus VMFileSystemGetAbsolutePath(char *abspath, const char *curpath, const char *destpath);
TVMStatus VMFileSystemPathIsOnMount(const char *mntpt, const char *destpath);
TVMStatus VMFileSystemDirectoryFromFullPath(char *dirname, const char *path);
TVMStatus VMFileSystemFileFromFullPath(char *filename, const char *path);
TVMStatus VMFileSystemConsolidatePath(char *fullpath, const char *dirname, const char *filename);
TVMStatus VMFileSystemSimplifyPath(char *simpath, const char *abspath, const char *relpath);
TVMStatus VMFileSystemRelativePath(char *relpath, const char *basepath, const char *destpath);

#ifdef __cplusplus
}
#endif

#endif

