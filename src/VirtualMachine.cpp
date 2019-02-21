#include <vector>
#include <queue>
#include <stdint.h>
#include <iostream>
#include <cstring>
#include "VirtualMachine.h"
#include "Machine.h"
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <string>
#include <fcntl.h>
#include <iomanip>
#include <errno.h>
#include <string.h>

//retrieved from https://stackoverflow.com/questions/12685787/pair-inside-priority-queue

extern "C" {
    //prototypes from VirtualMachineUtils.h
    TVMMainEntry VMLoadModule(const char *module);
    void VMUnloadModule(void);
    TVMStatus VMDateTime(SVMDateTimeRef curdatetime);
    TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);
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
    const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;
}
void FileCallback(void *calldata, int result);
TVMThreadID get_next();
TVMStatus RD_WR_sector(int flag, int sec_offset, int bytes_offset, unsigned int NumBytes);
uint32_t reverse_four(uint8_t first, uint8_t second, uint8_t third, uint8_t fourth){
    return (uint32_t)(first + (((uint16_t)second)<<8) + (((uint32_t)third)<<16) + (((uint32_t)fourth)<<24));
}
uint16_t reverse_two(uint8_t first, uint8_t second ){
    return (uint16_t)(first + (((uint16_t)second)<<8));
}

struct thread_control_block {
    SMachineContext context;
    //paras of threadcreatez
    TVMThreadEntry entry;
    void *param;
    TVMThreadPriority prio;
    TVMThreadID tid;

    //the stack
    void* stack_addr;
    TVMMemorySize stacksize;

    TVMStatus status_t;
    TVMTick sleep_time;
    bool deleted;
    int IOresult;

    bool acq_mutx; //for mutex
    int requiredSize;

    std::vector<int> fds_using;  //the fd of files that the thread is current holding
} TCB;

//thread
std::vector<TVMThreadID> sleepers;
TVMThreadID running_thread;   // put the id of the running thread in it
std::vector<thread_control_block> TCBs;
//index 0 is main thread, index 1 is idle thread

class CompareDist{
public:
    bool operator()(TVMThreadID n1, TVMThreadID n2) {
        if (TCBs.at(n1).prio >= TCBs.at(n2).prio ) {
            return false;
        }
        return true;
    }
};

std::priority_queue< TVMThreadID, std::vector<TVMThreadID>, CompareDist> ReadyThreadPrioQ;

struct chunk {
    void* base; // base of a chunk of memory
    TVMMemorySize size; // its size
};

struct memory_pool {
    TVMMemoryPoolID pid; // pool id
    void* base;
    TVMMemorySize total_size; // total size in bytes
    TVMMemorySize bytes_left; // bytes that are not yet allocated
    bool deleted;
    std::vector<chunk> freeChunks; // list of free chunks of this memory pool
    std::vector<chunk> allocatedChunks; // list of allocated chunks of this memory pool
};

struct mutex {
    TVMMutexID mid;
    TVMThreadID owner;
    bool locked;
    bool deleted;
    //waitinglist
    std::priority_queue< TVMThreadID, std::vector<TVMThreadID>, CompareDist> waiting_threads;
};


volatile unsigned int ticks = 0;
volatile unsigned int total_ticks = 0;

//memory pool
std::vector<memory_pool> MPs;
std::priority_queue<TVMThreadID, std::vector<TVMThreadID>, CompareDist> shared_waitin_Q;

// mutex
std::vector<mutex> Mutexs;


TVMThreadID get_next(){
    TVMThreadID next;
    while (!ReadyThreadPrioQ.empty()) {
        next = ReadyThreadPrioQ.top();
        ReadyThreadPrioQ.pop();
        if (TCBs.at(next).status_t == VM_THREAD_STATE_READY && !TCBs.at(next).deleted) {
            return next;
        }
    }
    return 1;
}

TVMStatus schedule_thread(TVMThreadState cur_thread_dest_state, TVMThreadID next_to_run, bool infinite_waiting = false) {
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);

    TVMThreadID running_copy = running_thread;
    if (cur_thread_dest_state == VM_THREAD_STATE_READY) {
        TCBs.at(running_thread).status_t = VM_THREAD_STATE_READY;  //change its status
        if (running_thread != 1) {
            ReadyThreadPrioQ.push(running_thread);
        }
    }
    else if (cur_thread_dest_state == VM_THREAD_STATE_WAITING) {
        TCBs.at(running_thread).status_t = VM_THREAD_STATE_WAITING;
        if (!infinite_waiting) {
            sleepers.push_back(running_thread);
        }
    } else if (cur_thread_dest_state == VM_THREAD_STATE_DEAD) {
        TCBs.at(running_thread).status_t = VM_THREAD_STATE_DEAD;
    }

    TCBs.at(next_to_run).status_t = VM_THREAD_STATE_RUNNING;  //change its status
    running_thread = next_to_run; //change global running
    MachineContextSwitch(&TCBs.at(running_copy).context, &TCBs.at(next_to_run).context); // switch to next thread on queue

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}
void idle_skeleton(){
    MachineEnableSignals();
    while (1) {
    }

}

// **************************************       file system      ******************************************************
class file;
class directory;
volatile unsigned int FileSystem_fd;
volatile unsigned int First_Free_FAT_Entry; // have the first free FAT entry number
volatile unsigned int Current_dir_index = 0;
uint8_t* FileSystem_buffer;
std::vector<directory> Dirs;
//std::vector<file> root_dir;
TVMMutexID FileSystemMutex;

struct FS_basic {
    uint8_t SecPerClus;    //13
    uint16_t RsvdSecCnt;   //14
    uint8_t NumFATs;       //16
    uint16_t RootEntCnt;   //17
    uint16_t TotalSec16;   //19
    uint16_t SecPerFAT;    //22       BPB_FATSz16
    uint32_t TotalSec32;   //32
    int firstRootSec;
    int CountRootDir;
    int firstDataSec;
    int ClusterCount;
} FileSysData;
// long file name https://doc.micrium.com/display/fsdoc/Short+and+Long+File+Names

class FAT {
public:
    std::vector<uint16_t> Entrees;
    void writeFAT(int fat_offset) {
        for (int m = 0; m < FileSysData.SecPerFAT; m++) { // for each sector in FAT
            for (int i = 0; i < 256; i++) { // for the first 256 entrees in Entrees
                uint16_t a = Entrees.at(m * 256 + i);
                a = ((a & 0xff) << 8) + ((a >> 8) & 0xff);
                memcpy(FileSystem_buffer + i * 2, &a, 2);
            }

            RD_WR_sector(0, FileSysData.RsvdSecCnt + fat_offset * FileSysData.SecPerFAT + m, 0, 512); // write filesysbuf to sector in image
        }
    }
    void readFAT() {
        for (int m = 0; m < FileSysData.SecPerFAT; m++) { // for each sector
            RD_WR_sector(1, FileSysData.RsvdSecCnt + m, 0, 512);
            for (int i = 0;i < 512; i += 2) { // for each entry in sector
                int firstByte = FileSystem_buffer[i];
                int secondByte = FileSystem_buffer[i + 1];
                int result = firstByte + (((uint16_t)secondByte) << 8);
                result = ((result << 8) | (result >> 8)); // convert to little endian
                Entrees.push_back((uint16_t) result);
            }
        }
    }

    void write_to_all(){
        for (int i = 0; i < FileSysData.NumFATs; i++) {
            writeFAT(i);
        }
    }
    /* finds free entry and assigns*/
    int AssignEntry(int currentClustNum) {
        for (int i = 0; i < (int) Entrees.size(); i++) {
            if (Entrees.at(i) == 0) {
                if(currentClustNum != -1){
                    Entrees.at(currentClustNum) = i;
                }
                Entrees.at(i) = 0xFFF8;
                return i;
            }
        }
        return -1;
    }

    void freeCluster(int startclust) {
        int nextClust;
        int const_start = startclust;
        while(! (Entrees.at(startclust) >= 0xFFF8)) {
            nextClust = Entrees.at(startclust);
            Entrees.at(startclust) = 0;
            startclust = nextClust;
        }
        Entrees.at(startclust) = 0;
        Entrees.at(const_start) = 0xFFF8;
    }

    int get_end_cluster(int startclust){
        while (! (Entrees.at(startclust) >= 0xFFF8)) {
            startclust = Entrees.at(startclust);
        }
        return startclust;
    }
};

FAT fat;

int biggest_fd = 4;
std::vector<int> freed_fds;



class file{
public:
    //intialize from FileSystem_buffer;
    file(){}
    void unmoumt_a_entry() {
        //0-10  name
        int length = (int)strlen(file_ent.DShortFileName);
        if (file_ent.DAttributes & 0x10) {
            for (int i = 0; i < 11; i++) {
                if (length > i) {
                    FileSystem_buffer[i] = file_ent.DShortFileName[i];
                } else {
                    FileSystem_buffer[i] = ' ';
                }
            }
        } else {
            int i = 0;
            while (file_ent.DShortFileName[i] != '.') {
                FileSystem_buffer[i] = file_ent.DShortFileName[i];
                i++;
            }
            for (; i < 8; i++) {
                FileSystem_buffer[i] = 32;
            }
            FileSystem_buffer[8]  = file_ent.DShortFileName[length - 3];FileSystem_buffer[9]  = file_ent.DShortFileName[length - 2];FileSystem_buffer[10]  = file_ent.DShortFileName[length - 1];
        //     memcpy(FileSystem_buffer + 8, file_ent.DShortFileName + length - 3, 3);
        }
        //11 attr
        FileSystem_buffer[11] = file_ent.DAttributes;
        //12 ntr
        FileSystem_buffer[12] = ntr;
        //13 hundredth
        FileSystem_buffer[13] = file_ent.DCreate.DHundredth;
        //14 15 Create time
        uint16_t temp_buffer = ((file_ent.DCreate.DSecond >> 1) & 0x1F) | ((file_ent.DCreate.DMinute & 0x3F ) << 5) | ( (file_ent.DCreate.DHour & 0x1F) << 11);
        memcpy(FileSystem_buffer + 14, &temp_buffer, 2);
        //16 17 Create Date
        temp_buffer = ((file_ent.DCreate.DYear - 1980) << 9) | ((file_ent.DCreate.DMonth & 0x0f) << 5) | (file_ent.DCreate.DDay & 0x1f);
        memcpy(FileSystem_buffer + 16, &temp_buffer, 2);
        //18 19 Last access time
        temp_buffer = ((file_ent.DAccess.DYear - 1980) << 9) | ((file_ent.DAccess.DMonth & 0x0f) << 5) | (file_ent.DAccess.DDay & 0x1f);
        memcpy(FileSystem_buffer + 18, &temp_buffer, 2);
        //20 21 start cluster number
        uint8_t zero = 0x0;
        memcpy(FileSystem_buffer + 20, &zero, 1);
        memcpy(FileSystem_buffer + 21, &zero, 1);
        //22 23 write time
        temp_buffer = ((file_ent.DModify.DSecond >> 1) & 0x1F) | ((file_ent.DModify.DMinute & 0x3F ) << 5) | ( (file_ent.DModify.DHour & 0x1F) << 11);
        memcpy(FileSystem_buffer + 22, &temp_buffer, 2);
        //24 25 write Date
        temp_buffer = ((file_ent.DModify.DYear - 1980) << 9) | ((file_ent.DModify.DMonth & 0x0f) << 5) | (file_ent.DModify.DDay & 0x1f);
        memcpy(FileSystem_buffer + 24, &temp_buffer, 2);
        //26 27
        temp_buffer = ((StartClusNum << 8) | (StartClusNum >> 8));
        memcpy(FileSystem_buffer + 26, &StartClusNum, 2);
        //28 29 30 31 size
        memcpy(FileSystem_buffer + 28, &size, 4);

        RD_WR_sector(0, FileSysData.firstRootSec, BytesSinceBeginDir, 32);
    }
    file(int i, int m) {
        modified = false;
        BytesSinceBeginDir = m * 512 + i;
        size = reverse_four(FileSystem_buffer[i + 28], FileSystem_buffer[i + 29], FileSystem_buffer[i + 30], FileSystem_buffer[i + 31]);
        file_ent.DSize = size;
        StartClusNum = reverse_two(FileSystem_buffer[i + 26], FileSystem_buffer[i + 27]);
        //get attr
        file_ent.DAttributes = (uint8_t)FileSystem_buffer[i + 11];
        //get the file name
        std::string name;
        for (int a = 0; a < 11; a++) {
            if (a == 8 && !(file_ent.DAttributes & 0x10)) { name.push_back('.'); }
            if (FileSystem_buffer[i + a] == ' ') { continue;  }
            name.push_back(FileSystem_buffer[i + a]);
        }
        //ntr
        ntr = (uint8_t)FileSystem_buffer[i + 12];
        DIR_FstClusLO = (uint16_t)FileSystem_buffer[i + 26];
        strcpy(file_ent.DShortFileName, name.c_str());
        //Create time
        uint16_t temp_buffer = reverse_two(FileSystem_buffer[i + 14], FileSystem_buffer[i + 15]);
        file_ent.DCreate.DSecond = (temp_buffer & 0x1f) << 1;
        file_ent.DCreate.DMinute = (temp_buffer >> 5) & 0x3f;
        file_ent.DCreate.DHour   =  temp_buffer >> 11;

        temp_buffer = reverse_two(FileSystem_buffer[i + 16], FileSystem_buffer[i + 17]);
        file_ent.DCreate.DDay    = temp_buffer & 0x1f;
        file_ent.DCreate.DMonth  = (temp_buffer >> 5) & 0x0f;
        file_ent.DCreate.DYear   = (temp_buffer >> 9) + 1980;
        file_ent.DCreate.DHundredth = FileSystem_buffer[i + 13];
        //Last access time
        temp_buffer = reverse_two(FileSystem_buffer[i + 18], FileSystem_buffer[i + 19]);
        file_ent.DAccess.DDay    = temp_buffer & 0x1f;
        file_ent.DAccess.DMonth  = (temp_buffer >> 5) & 0x0f;
        file_ent.DAccess.DYear   = (temp_buffer >> 9) + 1980;
        //Last write time
        temp_buffer = reverse_two(FileSystem_buffer[i + 22], FileSystem_buffer[i + 23]);
        file_ent.DModify.DSecond = (temp_buffer & 0x1f) << 1;
        file_ent.DModify.DMinute = (temp_buffer >> 5) & 0x3f;
        file_ent.DModify.DHour   = (temp_buffer >> 11) & 0x1f;
        temp_buffer = reverse_two(FileSystem_buffer[i + 24], FileSystem_buffer[i + 25]);
        file_ent.DModify.DDay    = temp_buffer & 0x1f;
        file_ent.DModify.DMonth  = (temp_buffer >> 5) & 0x0f;
        file_ent.DModify.DYear   = (temp_buffer >> 9) + 1980;
    }
    file(const char* filename, int BytesSinceBeginDir) :  modified(false), touched(true), BytesSinceBeginDir(BytesSinceBeginDir), size(0), ntr(0){
        //question 1: set attribute?  long file attribute
        StartClusNum = fat.AssignEntry(-1);
        //set name, attr
        //extra credit: long file name?
        strcpy(file_ent.DShortFileName, filename);
        file_ent.DSize = size;
        file_ent.DShortFileName[strlen(filename)] = '\0';
        //set create date, time
        VMDateTime(&file_ent.DCreate);
        VMDateTime(&file_ent.DAccess);
        VMDateTime(&file_ent.DModify);
        file_ent.DAttributes = 0;
    }
    struct access_info{
        int fd;
        TVMThreadID thread_id;
        int flag;
        int bytes_offset; //total offset
        int clus_offset;
        int curr_clut_num;
    };
    TVMStatus open(int flags, int* filedescriptor){
        //1.check the flag matches the attr
        if( (  (flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_APPEND) || (flags & O_TRUNC)  ) && file_ent.DAttributes == VM_FILE_SYSTEM_ATTR_READ_ONLY){
            return VM_STATUS_FAILURE;
        }else if(file_ent.DAttributes == VM_FILE_SYSTEM_ATTR_DIRECTORY || file_ent.DAttributes == VM_FILE_SYSTEM_ATTR_VOLUME_ID){
            return VM_STATUS_FAILURE;
        }
        touched = true;
        //2. set up access info, push on to access_threads vector
        access_info temp;
        temp.bytes_offset = 0;
        temp.clus_offset = 0;
        temp.curr_clut_num = StartClusNum;
        temp.flag = flags;
        temp.thread_id = running_thread;
        if (freed_fds.size() != 0) {
            temp.fd = freed_fds[0];
            freed_fds.erase(freed_fds.cbegin());
        } else {
            temp.fd = biggest_fd;
            biggest_fd++;
        }
        if (flags & (O_APPEND)) {
            temp.bytes_offset = size % (FileSysData.SecPerClus * 512);
            temp.clus_offset = size / (FileSysData.SecPerClus * 512);
            temp.curr_clut_num = fat.get_end_cluster(StartClusNum);
        }
        if (flags & O_TRUNC) {
            fat.freeCluster(StartClusNum);
            size = 0;
        }
        acc_thrds.push_back(temp);
        //3.give back filedescriptor
        *filedescriptor = temp.fd;
        //4.push the fd onto TCB's fds_using
        TCBs.at(running_thread).fds_using.push_back(temp.fd);
        //5.change last access time
        VMDateTime(&file_ent.DAccess);
        return VM_STATUS_SUCCESS;
    }

    TVMStatus close(int index_ac){
        //delete access thread, rm fd from TCB
        for (int k = 0; k < (int)TCBs.at(running_thread).fds_using.size(); k++) {
            if (TCBs.at(running_thread).fds_using.at(k) == acc_thrds.at(index_ac).fd) {
                TCBs.at(running_thread).fds_using.erase(TCBs.at(running_thread).fds_using.cbegin() + k);
                acc_thrds.erase(acc_thrds.cbegin() + index_ac);
                VMDateTime(&file_ent.DAccess);
                return VM_STATUS_SUCCESS;
            }
        }
        return VM_STATUS_FAILURE;
    }

    void RDWR_a_cluster(int flag, int curr_clut_num, int& bytes_offset, int bytes_needed, char* src_ptr){
        VMMutexAcquire(FileSystemMutex, VM_TIMEOUT_INFINITE);
        if (bytes_needed > 512) {
            //if length more than 512, so need to call rd_wr_sector more than once
            int read_write_size = 0;
            while (bytes_needed > 0) {
                //1. get the size to copy
                if (bytes_needed < 512) {
                    read_write_size = bytes_needed;
                    bytes_needed = 0;
                } else {
                    read_write_size = 512;
                    bytes_needed -= 512;
                }

                //2. write or read
                if (flag == 0) {
                    memcpy(FileSystem_buffer, src_ptr, read_write_size);
                    RD_WR_sector(0, curr_clut_num * FileSysData.SecPerClus + FileSysData.firstDataSec, bytes_offset, read_write_size);
                } else {
                    RD_WR_sector(1, curr_clut_num * FileSysData.SecPerClus + FileSysData.firstDataSec, bytes_offset, read_write_size);
                    memcpy(src_ptr, FileSystem_buffer, read_write_size);
                }
                //3.incread offset both in src_ptr and file system cluster
                src_ptr += read_write_size;
                bytes_offset += read_write_size;
            }
        } else {
            if (flag == 0) {
                memcpy(FileSystem_buffer, src_ptr, bytes_needed);
                RD_WR_sector(0, curr_clut_num * FileSysData.SecPerClus + FileSysData.firstDataSec, bytes_offset, bytes_needed);
            } else {
                RD_WR_sector(1, curr_clut_num * FileSysData.SecPerClus + FileSysData.firstDataSec, bytes_offset, bytes_needed);
                memcpy(src_ptr, FileSystem_buffer, bytes_needed);
            }
            bytes_offset += bytes_needed;
        }
        VMMutexRelease(FileSystemMutex);
    }

    //0write
    void read(void *data, int *length, int i) {
        int file_left_size = size -  acc_thrds.at(i).clus_offset * FileSysData.SecPerClus * 512 - acc_thrds.at(i).bytes_offset;
        if (file_left_size < *length) {
            *length = file_left_size; //actual length can read
        }

        char* data_ptr = (char*)data;  // get the pointer of *data
        int bytes_needed = *length;
        int curr_clust_left_size = FileSysData.SecPerClus * 512 - acc_thrds.at(i).bytes_offset;

        //if current cluster has more bytes than the bytes_needed
        if (curr_clust_left_size >= bytes_needed) {
            //only need to read the current cluster
            RDWR_a_cluster(1, acc_thrds.at(i).curr_clut_num, acc_thrds.at(i).bytes_offset, bytes_needed, data_ptr);
        } else {
            while (bytes_needed > 0) {
                bytes_needed -= curr_clust_left_size;
                RDWR_a_cluster(1, acc_thrds.at(i).curr_clut_num, acc_thrds.at(i).bytes_offset, curr_clust_left_size, data_ptr);

                //go to a new cluster
                acc_thrds.at(i).clus_offset++;
                acc_thrds.at(i).bytes_offset = 0;
                acc_thrds.at(i).curr_clut_num = fat.Entrees.at(acc_thrds.at(i).curr_clut_num);
                curr_clust_left_size = FileSysData.SecPerClus * 512;
                if (curr_clust_left_size > bytes_needed) {
                    RDWR_a_cluster(1, acc_thrds.at(i).curr_clut_num, acc_thrds.at(i).bytes_offset, bytes_needed, data_ptr);
                    break;
                }
            }
        }
    }

    void write(void *data, int *length, int i) {
        if (!(acc_thrds.at(i).flag & O_APPEND)) {
            acc_thrds.at(i).clus_offset = 0;
            acc_thrds.at(i).bytes_offset = 0;
        }

        char* data_ptr = (char*)data;  // get the pointer of *data
        int bytes_needed = *length;

        while (bytes_needed > 0) {
            if (bytes_needed > FileSysData.SecPerClus * 512) {
                bytes_needed -= FileSysData.SecPerClus * 512;

                RDWR_a_cluster(0, acc_thrds.at(i).curr_clut_num,  acc_thrds.at(i).bytes_offset, FileSysData.SecPerClus * 512, data_ptr);
                if (fat.Entrees.at(acc_thrds.at(i).curr_clut_num) >= 0xfff8) {
                    acc_thrds.at(i).curr_clut_num = fat.AssignEntry(acc_thrds.at(i).curr_clut_num);
                    //check failure here
                } else{
                    acc_thrds.at(i).curr_clut_num = fat.Entrees.at(acc_thrds.at(i).curr_clut_num);
                }

            } else {
                RDWR_a_cluster(0, acc_thrds.at(i).curr_clut_num,  acc_thrds.at(i).bytes_offset, bytes_needed, data_ptr);
                //write to cluster
                fat.freeCluster( acc_thrds.at(i).curr_clut_num);
                bytes_needed = 0;
                //clear sequent cluster
            }
        }
        size = *length;
        file_ent.DSize = size;
    }

    TVMStatus seek(int offset, int whence, int *newoffset, int i) {
        if (whence > (int)size) {
            return VM_STATUS_FAILURE;
        }
        //move the file offset to whence
        acc_thrds.at(i).clus_offset = 0;
        acc_thrds.at(i).curr_clut_num = StartClusNum;
        while (whence > FileSysData.SecPerClus * 512) {
            whence -= FileSysData.SecPerClus * 512;
            acc_thrds.at(i).clus_offset++;
            acc_thrds.at(i).curr_clut_num = fat.Entrees.at(acc_thrds.at(i).curr_clut_num);
        }
        acc_thrds.at(i).bytes_offset = whence;

        if (whence + offset > (int)size) {
            *newoffset = size;
        } else {
            *newoffset = whence + offset;
        }
        //move the file offset to max( whence + offset)
        while (offset > 0) {
            if (FileSysData.SecPerClus * 512 - acc_thrds.at(i).bytes_offset > offset) {
                acc_thrds.at(i).bytes_offset += offset;
                offset = 0;
            } else {
                offset -= (FileSysData.SecPerClus * 512 - acc_thrds.at(i).bytes_offset);
                acc_thrds.at(i).bytes_offset = 0;
                acc_thrds.at(i).clus_offset++;
                acc_thrds.at(i).curr_clut_num = fat.Entrees.at(acc_thrds.at(i).curr_clut_num);
            }
        }
        return VM_STATUS_SUCCESS;
    }
    //A_CP    TXT  d?X}M}M  ?X}M ?
    //A_CP    TXT  d?X}M}M ?X}M ?
    bool modified;
    bool touched;
    int BytesSinceBeginDir;
    uint32_t size;
    uint16_t StartClusNum;
    uint8_t ntr;
    uint16_t DIR_FstClusLO;
    std::vector<access_info> acc_thrds;
    SVMDirectoryEntry file_ent; // name, attr, dates, size
};

class directory{
public:
    directory(){
    }
    TVMStatus new_file(const char* name) {
        if (Current_dir_index == 0) {
            //extra credit: long file name?
            //in root dir
            if (first_free_byte >= FileSysData.CountRootDir * 512) {
                return VM_STATUS_FAILURE;
            }
            file a = file(name, first_free_byte);
            first_free_byte += 32;
            files.push_back(a);
        } else {
            //extra credit: in sub dir
        }
        return VM_STATUS_SUCCESS;
    }

    TVMStatus find_file(int filedescriptor, int& index_file, int& index_ac_info) {
        for (int i = 0; i < (int)files.size(); i++) {
            for (int j = 0; j < (int)files.at(i).acc_thrds.size(); j++) {
                if (files.at(i).acc_thrds.at(j).fd == filedescriptor) {
                    if (files.at(i).acc_thrds.at(j).thread_id == running_thread) {
                        index_file = i;
                        index_ac_info = j;
                        return VM_STATUS_SUCCESS;
                    }
                    return VM_STATUS_FAILURE;
                }
            }
        }
        return VM_STATUS_FAILURE;
    }

    file itself;
    int parent_index;
    std::vector<file> files;
    std::string path;
    int first_free_byte;
};

// *************************************************************************************************************************

TVMStatus unmount(){
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    //unmount fat
    fat.write_to_all();
    //unmount root dir
    for (int i = 0; i < (int)Dirs.at(0).files.size(); i++) {
        if (Dirs.at(0).files.at(i).touched) {
            Dirs.at(0).files.at(i).unmoumt_a_entry();
        }
    }
    //iterate through Dirs, if one dir is in dir, itself's entry need to be changed
    //iterate through files in one dir, if touched, write back

    //the data section should be modified directly so don't need to unmount
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

//Flag: 0 is write, 1 is read
//maximum bytes is 512
//When do write, should first copy the inteneded writing data into FileSystem_buffer first
TVMStatus RD_WR_sector(int flag, int sec_offset, int bytes_offset, unsigned int NumBytes) {
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

 //   VMMutexAcquire(FileSystemMutex, VM_TIMEOUT_INFINITE);
    TVMThreadID current_thread = running_thread;

    MachineFileSeek(FileSystem_fd, sec_offset * 512 + bytes_offset, 0, FileCallback, &current_thread);
    schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);

    if (TCBs.at(current_thread).IOresult < 0) {
       // VMMutexRelease(FileSystemMutex);
         MachineResumeSignals(&signals);
        return VM_STATUS_FAILURE;
    }

    if (flag == 1) {
        MachineFileRead(FileSystem_fd, FileSystem_buffer, NumBytes, FileCallback, &current_thread);
    } else {
        MachineFileWrite(FileSystem_fd, FileSystem_buffer, NumBytes, FileCallback, &current_thread);
    }
    schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);

 //   VMMutexRelease(FileSystemMutex);
    MachineResumeSignals(&signals);
    if (TCBs.at(current_thread).IOresult < 0) {  return VM_STATUS_FAILURE; }
    else { return VM_STATUS_SUCCESS;  }
}

TVMStatus mount_image(const char* mount) {

    VMMutexCreate(&FileSystemMutex);
    //1.initialize fd, buffer
    TVMThreadID prev_thread = running_thread;
    MachineFileOpen(mount, O_RDWR, 0600, FileCallback, &prev_thread);
    schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
    FileSystem_fd = TCBs.at(0).IOresult; // set the main thread's ioresult to be the fd of the image
    VMMemoryPoolAllocate(1, 512, (void**)&FileSystem_buffer); // allocate space for system buffer

    //2.read in the first 512 bytes / boot block
    RD_WR_sector(1, 0, 0, 512);
    //3.assign data to FileSysData
    FileSysData.SecPerClus = *(uint8_t*)(FileSystem_buffer + 13);
    FileSysData.RsvdSecCnt = *(uint16_t*)(FileSystem_buffer + 14);
    FileSysData.NumFATs    = *(uint8_t*)(FileSystem_buffer + 16);
    FileSysData.RootEntCnt = *(uint16_t*)(FileSystem_buffer + 17);
    FileSysData.TotalSec16 = *(uint16_t*)(FileSystem_buffer + 19) ;
    FileSysData.SecPerFAT  = *(uint16_t*)(FileSystem_buffer + 22);
    FileSysData.TotalSec32 = *(uint32_t*)(FileSystem_buffer + 32);
    FileSysData.firstRootSec = FileSysData.RsvdSecCnt + FileSysData.NumFATs * FileSysData.SecPerFAT;
    FileSysData.CountRootDir = (FileSysData.RootEntCnt * 32) / 512;
    FileSysData.firstDataSec = FileSysData.firstRootSec + FileSysData.CountRootDir;
    if (FileSysData.TotalSec16 != 0) {
        FileSysData.ClusterCount = (FileSysData.TotalSec16 - FileSysData.firstDataSec) / FileSysData.SecPerClus;
    } else {
        FileSysData.ClusterCount = (FileSysData.TotalSec32 - FileSysData.firstDataSec) / FileSysData.SecPerClus;
    }
    //4.read in fat

    fat.readFAT();
    //5.read in the root directory only
    directory dir;
    for (int m = 0; m < FileSysData.CountRootDir; m++) {
        RD_WR_sector(1, FileSysData.firstRootSec + m, 0, 512);

        for (int i = 0; i < 512; i += 32) {
            //to the end of entry or skip empty
            if ((uint8_t)FileSystem_buffer[i] == 0x00) {
                dir.first_free_byte = m * 512 + i;
                Dirs.push_back(dir);
                return VM_STATUS_SUCCESS;
            } else if ((uint8_t)FileSystem_buffer[i] == 0x0E5){
                continue;
            }
            //skip long file names
            if ((uint8_t)FileSystem_buffer[i + 11] == 0x0F) {
                if ((uint8_t)FileSystem_buffer[i] == 0x01) {
                    i += 32;
                }
                continue;
            }
            file temp(i, m);
            if (m == 0 && i == 0) {
                dir.itself = temp;
            }
            dir.files.push_back(temp);
        }
    }
    Dirs.push_back(dir);
    return VM_STATUS_SUCCESS;
}


void upper_case(const char *filename, char* temp){
    for (int a = 0; a < (int)strlen(filename); a++)  {
        if (isalpha(filename[a])) {
            temp[a] = toupper(filename[a]);
        }else {
            temp[a] = filename[a];
        }
    }
    temp[strlen(filename)] = '\0';
}


TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor) {
    if (filename == NULL || filedescriptor == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    //1.suspend signals and get thread ready
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    char name[strlen(filename) + 1];
    upper_case(filename, name);

    //2.check files in current directory, find the matching one and try to open
    for (int i = 0; i < (int)Dirs.at(Current_dir_index).files.size(); i++) {
        //extra credit: long name?
        if (strcmp(Dirs.at(Current_dir_index).files.at(i).file_ent.DShortFileName, name) == 0) {
            if (Dirs.at(Current_dir_index).files.at(i).open(flags, filedescriptor) == VM_STATUS_FAILURE) {
                MachineResumeSignals(&signals);
                return VM_STATUS_FAILURE;
            }
            MachineResumeSignals(&signals);
            return VM_STATUS_SUCCESS;
        }
    }
    //3. create the file if can create new and the file does not exist
    if (flags & O_CREAT) {
        if (Dirs.at(Current_dir_index).new_file(name) == VM_STATUS_FAILURE) {
            MachineResumeSignals(&signals);
            return VM_STATUS_FAILURE;
        }
        Dirs.at(Current_dir_index).files.at(Dirs.at(Current_dir_index).files.size() - 1).open(flags, filedescriptor);
        MachineResumeSignals(&signals);
        return VM_STATUS_SUCCESS;
    }

    MachineResumeSignals(&signals);
    return VM_STATUS_FAILURE;
}


//FAT file system: https://www.win.tue.nl/~aeb/linux/fs/fat/fat-1.html
//                 http://www.tavi.co.uk/phobos/fat.html

TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
    //1.suspend signals and get thread ready
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    if (data == NULL || length == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    if (filedescriptor < 3) {
        TVMThreadID current_thread = running_thread;
        int byte_left = *length, read_size = 0;
        void* shared_ptr = NULL;
        char* src_ptr = (char*)data;
        *length = 0;

        while (byte_left > 0) {
            //1. get the size to read
            if (byte_left < 512){
                read_size = byte_left;
                byte_left = 0;
            } else {
                read_size = 512;
                byte_left -= 512;
            }

            //2. allocate space in shared memory for one time
            if (VMMemoryPoolAllocate(1, read_size, &shared_ptr) == VM_STATUS_ERROR_INSUFFICIENT_RESOURCES) {
                // wait until there is enough memory
                // need implementation: push onto waiting queue?
                TCBs.at(running_thread).requiredSize = read_size;
                shared_waitin_Q.push(running_thread);
                schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);

                VMMemoryPoolAllocate(1, read_size, &shared_ptr);
            }
            //3. read from file into shared memory
            MachineFileRead(filedescriptor, shared_ptr, read_size, FileCallback, &current_thread);
            schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
            //4. check the result
            if (TCBs.at(current_thread).IOresult < 0) {
                VMMemoryPoolDeallocate(1, shared_ptr);
                MachineResumeSignals(&signals);
                return VM_STATUS_FAILURE;
            }

            //5. copy from shared memory to *data
            std::memcpy(src_ptr, shared_ptr, read_size);
            VMMemoryPoolDeallocate(1, shared_ptr);
            //6. move the pointer down, increase actually length read
            *length += TCBs.at(current_thread).IOresult;
            src_ptr += read_size;
        }
    } else {
        int index_file, index_ac_info;
        if (Dirs.at(Current_dir_index).find_file(filedescriptor, index_file, index_ac_info) == VM_STATUS_SUCCESS) {
            Dirs.at(Current_dir_index).files.at(index_file).read(data, length, index_ac_info);
        } else {
            MachineResumeSignals(&signals);
            return VM_STATUS_FAILURE;
        }
    }
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    if (data == NULL || length == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    //1.suspend signals and get thread ready

    if (filedescriptor < 3) {
        TVMThreadID current_thread = running_thread;

        int byte_left = *length, read_size = 0;
        void* shared_ptr = NULL;
        char* src_ptr = (char*)data;  // get the pointer of *data
        *length = 0;

        while (byte_left > 0) {
            //1. get the size to copy
            if (byte_left < 512) {
                read_size = byte_left;
                byte_left = 0;
            } else {
                read_size = 512;
                byte_left -= 512;
            }
            //2. allocate space in shared memory
            if (VMMemoryPoolAllocate(1, read_size, &shared_ptr) == VM_STATUS_ERROR_INSUFFICIENT_RESOURCES) {
                TCBs.at(running_thread).requiredSize = read_size;
                shared_waitin_Q.push(running_thread);
                schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
                VMMemoryPoolAllocate(1, read_size, &shared_ptr);
            }
            //3. copy from *data to shared memory
            std::memcpy(shared_ptr, src_ptr, read_size);
            //4. write into file from shared memory
            MachineFileWrite(filedescriptor, shared_ptr, read_size, FileCallback, &current_thread);
            schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
            //deallocate
            VMMemoryPoolDeallocate(1, shared_ptr);
            //5. check results
            if (TCBs.at(current_thread).IOresult < 0) {
                VMMemoryPoolDeallocate(1, shared_ptr);
                MachineResumeSignals(&signals);
                return VM_STATUS_FAILURE;
            }
            //6. move the pointer down, increase actually length read
            *length += TCBs.at(current_thread).IOresult;
            src_ptr += read_size;
        }
    } else {
        int index_file, index_ac_info;
        if (Dirs.at(Current_dir_index).find_file(filedescriptor, index_file, index_ac_info) == VM_STATUS_SUCCESS) {
            Dirs.at(Current_dir_index).files.at(index_file).write(data, length, index_ac_info);
        } else {
            MachineResumeSignals(&signals);
            return VM_STATUS_FAILURE;
        }
    }

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset) {
    //1.suspend signals and get thread ready
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    if (filedescriptor < 3) {
        TVMThreadID prev_thread = running_thread;
        //2.call MachineFileSeek
        MachineFileSeek(filedescriptor, offset, whence, FileCallback, &prev_thread);
        //3.Switch to a ready thread
        schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
        //4.give back the newoffset and return
        *newoffset = TCBs.at(prev_thread).IOresult;
        MachineResumeSignals(&signals);
        if (*newoffset < 0) {  return VM_STATUS_FAILURE;  }
        else {  return VM_STATUS_SUCCESS;  }
    } else {
        int index_file, index_ac_info;
        if (Dirs.at(Current_dir_index).find_file(filedescriptor, index_file, index_ac_info) == VM_STATUS_SUCCESS) {
            Dirs.at(Current_dir_index).files.at(index_file).seek(offset, whence, newoffset, index_ac_info);
            MachineResumeSignals(&signals);
            return VM_STATUS_SUCCESS;
        }
        MachineResumeSignals(&signals);
        return VM_STATUS_FAILURE;
    }
}

TVMStatus VMFileClose(int filedescriptor) {
    //1.suspend signals and get thread ready
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    if (filedescriptor < 3) {
        TVMThreadID prev_thread = running_thread;
        //2.call MachineFileClose
        MachineFileClose(filedescriptor, FileCallback, &prev_thread);
        //3.Switch to a ready thread
        schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
        MachineResumeSignals(&signals);
        if (TCBs.at(prev_thread).IOresult < 0) {
            return VM_STATUS_FAILURE;
        }
    } else {
        int index_file, index_ac_info;
        if (Dirs.at(Current_dir_index).find_file(filedescriptor, index_file, index_ac_info) == VM_STATUS_FAILURE) {
            MachineResumeSignals(&signals);
            return VM_STATUS_FAILURE;
        } else {
            Dirs.at(Current_dir_index).files.at(index_file).close(index_ac_info);
        }
        MachineResumeSignals(&signals);
    }
    return VM_STATUS_SUCCESS;
}


bool rootOpened = false;
int rootOffset = 0;
char cur_path[VM_FILE_SYSTEM_MAX_PATH + 1];

TVMStatus VMDirectoryOpen(const char *dirname, int *dirdescriptor) {
    if (dirname == NULL || dirdescriptor == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (VMFileSystemValidPathName(dirname) == VM_STATUS_ERROR_INVALID_PARAMETER) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    if (strcmp(dirname, "/") != 0) {
        return VM_STATUS_FAILURE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    rootOpened = true;
    rootOffset = 0;
    *dirdescriptor = 3;
    strcpy(cur_path, dirname);

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryClose(int dirdescriptor) {
    if (dirdescriptor != 3) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    rootOpened = false;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRead(int dirdescriptor, SVMDirectoryEntryRef dirent) {
    if (dirent == NULL || dirdescriptor != 3) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (rootOffset >= (int)Dirs.at(Current_dir_index).files.size()) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    // read into dirent
    *dirent = Dirs.at(0).files.at(rootOffset).file_ent;
    rootOffset++;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryRewind(int dirdescriptor) {
    if (dirdescriptor != 50) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    rootOffset = 0;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryCurrent(char *abspath) {
    if (abspath == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    strcpy(abspath, "/");
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMDirectoryChange(const char *path) {
    if (path == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    char ab[VM_FILE_SYSTEM_MAX_PATH + 1];
    VMFileSystemGetAbsolutePath(ab, cur_path, path);
    if (strcmp(ab, "/") == 0) {
        MachineResumeSignals(&signals);
        return VM_STATUS_SUCCESS;
    }
    MachineResumeSignals(&signals);
    return VM_STATUS_FAILURE;
}

TVMStatus VMDirectoryCreate(const char *dirname){
    return VM_STATUS_FAILURE;
}
TVMStatus VMDirectoryUnlink(const char *path){
    return VM_STATUS_FAILURE;
}

void callback(void* calldata){
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    //1.increase ticks
    total_ticks++;
    //2.check sleepers
    TVMThreadID wake_up_thread = 0;
    for (int i = 0; i < (int)sleepers.size(); i++) {
        TCBs.at(sleepers.at(i)).sleep_time--;
        if (TCBs.at(sleepers.at(i)).sleep_time <= 0) {
            wake_up_thread = sleepers.at(i);
            sleepers.erase(sleepers.cbegin() + i);
            if (TCBs.at(wake_up_thread).acq_mutx) {
                TCBs.at(wake_up_thread).acq_mutx = false;
                // machinecontextswitch to wake_up_thread, push the running thread on prio_q
                schedule_thread(VM_THREAD_STATE_READY, wake_up_thread);
                break;
            } else {
                TCBs.at(wake_up_thread).status_t = VM_THREAD_STATE_READY;
                ReadyThreadPrioQ.push(wake_up_thread);
                schedule_thread(VM_THREAD_STATE_READY, get_next());
            }
        }
    }
    //3.for preempt, swith to same priority thread
    if (!ReadyThreadPrioQ.empty() && TCBs.at(running_thread).prio == TCBs.at(ReadyThreadPrioQ.top()).prio) {
        schedule_thread(VM_THREAD_STATE_READY, get_next()); // schedule the next ready thread
    }
    MachineResumeSignals(&signals);
}

void initialze_main_idle_thread(){
    //1. assign a TCB for the main thread
    thread_control_block main_thread;
    main_thread.entry = NULL;
    main_thread.param = NULL;
    main_thread.prio = VM_THREAD_PRIORITY_NORMAL;
    main_thread.tid = 0;
    main_thread.stack_addr = NULL;
    main_thread.stacksize = 0x10000;
    main_thread.status_t = VM_THREAD_STATE_RUNNING;
    main_thread.tid = 0;
    main_thread.context = SMachineContext();
    main_thread.sleep_time = 0;
    main_thread.IOresult = 0;
    main_thread.deleted = false;
    main_thread.acq_mutx = false;
    main_thread.requiredSize = 0;
    TCBs.push_back(main_thread);

    //2. intialize the global var
    running_thread = 0;

    //3. create and activate idle thread
    thread_control_block idle_thread;
    idle_thread.context = SMachineContext();
    void* stackaddr = NULL;
    VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, 0x100000, &stackaddr);
    idle_thread.stack_addr = stackaddr;
    idle_thread.stacksize = 0x100000;
    idle_thread.tid = (int)TCBs.size();
    idle_thread.deleted = false;
    idle_thread.IOresult = 0;
    idle_thread.status_t = VM_THREAD_STATE_READY;
    idle_thread.acq_mutx = false;
    TCBs.push_back(idle_thread);
    MachineContextCreate(&TCBs.at(1).context, (TVMThreadEntry)idle_skeleton, NULL, idle_thread.stack_addr, idle_thread.stacksize);

}

void CombineChunks(TVMMemoryPoolID memory) {
    for (int i = 0; i < (int) MPs.at(memory).freeChunks.size(); i++) { // for each free chunk in mem pool
        if (i + 1 < (int) MPs.at(memory).freeChunks.size()) { // if there's a next free chunk
            // find tail pointer of current chunk
            void* tail = (void*) ((uint8_t*) MPs.at(memory).freeChunks.at(i).base + MPs.at(memory).freeChunks.at(i).size);
            if (tail == MPs.at(memory).freeChunks.at(i + 1).base) { // current chunk's tail == next chunk's base
                MPs.at(memory).freeChunks.at(i).size += MPs.at(memory).freeChunks.at(i + 1).size; // combine the sizes
                MPs.at(memory).freeChunks.erase(MPs.at(memory).freeChunks.begin() + i + 1); // delete the next chunk
                i--;
            }
        }
    }
}

void FreeChunk(TVMMemoryPoolID memory, int location) {
    bool inserted = false;
    for (int i = 0; i < (int) MPs.at(memory).freeChunks.size(); i++) { // for each free chunk in mem pool
        // if the base of this free chunk is after the base of chunk we want to deallocate
        if (MPs.at(memory).freeChunks.at(i).base > MPs.at(memory).allocatedChunks.at(location).base) {
            MPs.at(memory).freeChunks.insert(MPs.at(memory).freeChunks.begin() + i, MPs.at(memory).allocatedChunks.at(location)); // insert into free chunks here
            inserted = true;
            break; // done
        }
    }

    if (!inserted) {
        MPs.at(memory).freeChunks.push_back(MPs.at(memory).allocatedChunks.at(location));
    }
    CombineChunks(memory);
}
/********************************************           VMStart            *************************************************/
TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, const char *mount, int argc, char *argv[]) {
    //1. allocate shared memory and heap
    void* sys_pointer = malloc(heapsize);
    void* shared_pointer = MachineInitialize(sharedsize);
    TVMMemoryPoolID shared_id, sys_id;
    VMMemoryPoolCreate(sys_pointer, heapsize, &sys_id);
    VMMemoryPoolCreate(shared_pointer, sharedsize, &shared_id);
    //2. set up alarm
    MachineRequestAlarm(tickms * 1000, callback, NULL);
    ticks = tickms;
    //3. load the module
    TVMMainEntry VMMain = VMLoadModule(argv[0]);
    if (VMMain == NULL) {
        MachineTerminate();
        return VM_STATUS_FAILURE;
    }
    //4. initialize main and idle thread
    initialze_main_idle_thread();

    //5. mount the file system
    mount_image(mount);
    //6. call VMMain function
    MachineEnableSignals();
    VMMain(argc, argv);
    //7.finsh and clean up
    unmount();
    int curr_thrd = running_thread;
    MachineFileClose(FileSystem_fd, FileCallback, &curr_thrd);
    schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
    VMUnloadModule();
    MachineTerminate();
    free(sys_pointer);
    return VM_STATUS_SUCCESS;
} // end of vmstart


TVMStatus VMTickMS(int *tickmsref) {
    if (tickmsref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);
    *tickmsref = ticks;
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMTickCount(TVMTickRef tickref) {
    if (tickref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    //MachineEnableSignals();    // unsafe here!!!!!!
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);
    *tickref = total_ticks;
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid) {
    if (entry == NULL || tid == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);

    //add new thread block
    thread_control_block new_thread;                                                  // VMThreadCreate main code
    new_thread.entry = entry;
    new_thread.param = param;
    new_thread.stacksize = memsize;
    new_thread.prio = prio;
    new_thread.tid = (int)TCBs.size();
    void* stackaddr = NULL;
    VMMemoryPoolAllocate(VM_MEMORY_POOL_ID_SYSTEM, memsize, &stackaddr);
    new_thread.stack_addr = stackaddr;
    new_thread.status_t = VM_THREAD_STATE_DEAD;
    new_thread.context = SMachineContext();
    new_thread.sleep_time = 0;
    new_thread.IOresult = 0;
    new_thread.deleted = false;
    new_thread.acq_mutx = false;
    TCBs.push_back(new_thread);  //push the new TCB onto TCB vector
    *tid = new_thread.tid;  // give the tid back to the user
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadDelete(TVMThreadID thread){
    if (thread >= TCBs.size() || TCBs.at(thread).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(TCBs.at(thread).status_t != VM_THREAD_STATE_DEAD) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);

    TCBs.at(thread).deleted = true;                                       //  VMThreadDelete main code

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

void skeleton(){
    MachineEnableSignals();
    TCBs.at(running_thread).entry(TCBs.at(running_thread).param);
    VMThreadTerminate(running_thread);
    return;
}

TVMStatus VMThreadActivate(TVMThreadID thread){
    if (thread >= TCBs.size() || TCBs.at(thread).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(TCBs.at(thread).status_t != VM_THREAD_STATE_DEAD) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    //VMThreadActivate main code
    MachineContextCreate(&TCBs.at(thread).context, (TVMThreadEntry)skeleton, NULL, TCBs.at(thread).stack_addr, TCBs.at(thread).stacksize); // call MCC

    TCBs.at(thread).status_t = VM_THREAD_STATE_READY; //set the status to ready
    ReadyThreadPrioQ.push(thread);  //push it onto ready queue

    if (TCBs.at(thread).prio >  TCBs.at(running_thread).prio) { // if the activated one have a higher priority, switch to it.
        schedule_thread(VM_THREAD_STATE_READY, get_next());
    }

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){

    if (thread >= TCBs.size() || TCBs.at(thread).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(TCBs.at(thread).status_t == VM_THREAD_STATE_DEAD) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    //1. free the mutex
    for (int i = 0; i < (int)Mutexs.size(); i++) {
        if (!Mutexs.at(i).deleted && Mutexs.at(i).locked && Mutexs.at(i).owner == thread) {
            Mutexs.at(i).locked = false;
            TVMThreadID thread;
            if(!Mutexs.at(i).waiting_threads.empty()) {
                thread = Mutexs.at(i).waiting_threads.top();
                Mutexs.at(i).waiting_threads.pop();
                if (TCBs.at(i).acq_mutx) { // if the thread is still acquiring
                    Mutexs.at(i).owner = thread;
                    Mutexs.at(i).locked = true;
                    if (TCBs.at(thread).prio > TCBs.at(running_thread).prio) {
                        schedule_thread(VM_THREAD_STATE_READY, thread);
                    } else {
                        TCBs.at(thread).status_t = VM_THREAD_STATE_READY;
                        ReadyThreadPrioQ.push(thread);
                        //push on ready q
                    }
                }//if
            }//if :checking the released mutex's waiting list
        }//if the thread has some mutx
    }//for loop
    //2. terminte the thread
    if (TCBs.at(thread).status_t == VM_THREAD_STATE_RUNNING) {
        if (sleepers.size() == 0 && ReadyThreadPrioQ.size() == 0) {
            MachineResumeSignals(&signals);
            return VM_STATUS_SUCCESS;
        }
        schedule_thread(VM_THREAD_STATE_DEAD, get_next());

    } else if (TCBs.at(thread).status_t == VM_THREAD_STATE_WAITING){
        for (int i = 0; i < (int)sleepers.size(); i++) {
            if (TCBs.at(sleepers.at(i)).tid == thread) {
                sleepers.erase(sleepers.cbegin() + i);
                break;
            }
        }
    }
    TCBs.at(thread).status_t = VM_THREAD_STATE_DEAD;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadID(TVMThreadIDRef threadref) {
    if (threadref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    *threadref = running_thread;                                        // VMThreadID main code

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref) {
    if (thread >= TCBs.size() || TCBs.at(thread).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (stateref == NULL){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    *stateref = TCBs.at(thread).status_t;               // VMThreadState main code

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadSleep(TVMTick tick){
    if (tick == VM_TIMEOUT_INFINITE) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    if (tick == VM_TIMEOUT_IMMEDIATE) { // switch to another thread of equal priority
        if (!ReadyThreadPrioQ.empty() && TCBs.at(running_thread).prio == TCBs.at(ReadyThreadPrioQ.top()).prio) {
            //NOTE1: schedule get called
            schedule_thread(VM_THREAD_STATE_READY, get_next()); // schedule the next ready thread
        } else {
            MachineResumeSignals(&signals);
            return VM_STATUS_SUCCESS; // <--- Ask professor
        }
    } else {
        TCBs.at(running_thread).sleep_time = tick;
        schedule_thread(VM_THREAD_STATE_WAITING, get_next());
    }
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}


//https://www.cs.princeton.edu/courses/archive/fall08/cos318/lectures/Lec6-MutexImplementation.pdf
TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
    if (mutexref == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    mutex mutex;
    mutex.deleted = false;
    mutex.locked = false;
    mutex.mid = (int)Mutexs.size();
    *mutexref = mutex.mid; //give back the id
    Mutexs.push_back(mutex);

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexDelete(TVMMutexID mutex){
    if (mutex >= Mutexs.size() || Mutexs.at(mutex).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(Mutexs.at(mutex).locked){
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    Mutexs.at(mutex).deleted = true;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
    if (mutex >= Mutexs.size() || Mutexs.at(mutex).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(ownerref == NULL){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    if (!Mutexs.at(mutex).locked){
        *ownerref = VM_THREAD_ID_INVALID;
    } else {
        *ownerref = Mutexs.at(mutex).owner;
    }

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout) {
    if (mutex >= Mutexs.size() || Mutexs.at(mutex).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if (timeout == VM_TIMEOUT_IMMEDIATE && Mutexs.at(mutex).locked) {
        return VM_STATUS_FAILURE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    if (Mutexs.at(mutex).locked &&  Mutexs.at(mutex).owner == running_thread) {
        return VM_STATUS_SUCCESS;
    }
    if (!Mutexs.at(mutex).locked) {
        Mutexs.at(mutex).locked = true;
        Mutexs.at(mutex).owner = running_thread;
        MachineResumeSignals(&signals);
        return VM_STATUS_SUCCESS;
    }
    Mutexs.at(mutex).waiting_threads.push(running_thread); //put the thread onto the mutex's waiting queue
    TCBs.at(running_thread).acq_mutx = true;

    if (timeout == VM_TIMEOUT_INFINITE) {
        schedule_thread(VM_THREAD_STATE_WAITING, get_next(), true);
        // this thread is waiting
        // switch to another thread, wait until the mutex is acquired
    } else {
        VMThreadSleep(timeout); //swtich to another thread, make this one sleep
    } // if not infinite, put the current to sleep


    MachineResumeSignals(&signals);
    if (Mutexs.at(mutex).owner != running_thread) {
        return VM_STATUS_FAILURE;
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMutexRelease(TVMMutexID mutex) {
    if (mutex >= Mutexs.size() || Mutexs.at(mutex).deleted) {
        return VM_STATUS_ERROR_INVALID_ID;
    } else if(Mutexs.at(mutex).owner != running_thread) {
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    Mutexs.at(mutex).locked = false;
    TVMThreadID thread;
    if (!Mutexs.at(mutex).waiting_threads.empty()) {
        thread = Mutexs.at(mutex).waiting_threads.top();
        Mutexs.at(mutex).waiting_threads.pop();
        if (TCBs.at(thread).acq_mutx) { // if the thread is still acquiring
            Mutexs.at(mutex).owner = thread;
            Mutexs.at(mutex).locked = true;
            if (TCBs.at(thread).prio > TCBs.at(running_thread).prio) {
                schedule_thread(VM_THREAD_STATE_READY, thread);
            } else {
                TCBs.at(thread).status_t = VM_THREAD_STATE_READY;
                ReadyThreadPrioQ.push(thread);
                //push on ready q
            }
        }
    }
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

/*       creates a memory pool of requested size      */
TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory) {
    if (base == NULL || memory == NULL || size == 0) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine
    memory_pool pool; // instantiate a memory_pool object
    pool.pid = (int) MPs.size(); // assign unique memory pool id
    pool.base = base; // make it point to the base of some array indicated by base
    pool.total_size = pool.bytes_left = size; // set its size to given size
    pool.deleted = false;
    // create new free chunk
    chunk chunk;
    chunk.base = base;
    chunk.size = size;
    pool.freeChunks.push_back(chunk); // push chunk onto list of free chunks of this memory pool
    *memory = pool.pid; // give back the id
    MPs.push_back(pool);
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory) {
    if (memory >= MPs.size() || MPs.at(memory).deleted) { // if memory pool does not exist
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (MPs.at(memory).allocatedChunks.size()) { // if there are allocated chunks in the memory pool
        return VM_STATUS_ERROR_INVALID_STATE;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals);

    MPs.at(memory).deleted = true;

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft) {
    if (memory >= MPs.size() || MPs.at(memory).deleted || bytesleft == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    *bytesleft = MPs.at(memory).bytes_left; // give back amount of unallocated memory

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

/*       finds a memory chunk of requested size from within requested memory pool      */
TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer) {
    bool allocated = false;
    if (memory >= MPs.size() || MPs.at(memory).deleted || size == 0 || pointer == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TMachineSignalState signals;
    MachineSuspendSignals(&signals); //routine

    // create a chunk for allocation
    chunk chunk;

    if (size % 64 == 0) {
        chunk.size = size;
    } else { // need to round size to next multiple of 64
        chunk.size = (size / 64 + 1) * 64;
    }

    // find a free chunk large enough for requested size
    for (int i = 0; i < (int) MPs.at(memory).freeChunks.size(); i++) { // for each free chunk in mem pool
        if (MPs.at(memory).freeChunks.at(i).size >= chunk.size) { // found large enough memory chunk
            chunk.base = MPs.at(memory).freeChunks.at(i).base; // set the new chunk's base pointer
            *pointer = chunk.base; // give back base pointer

            // insert the chunk into the allocatedChunks vector
            MPs.at(memory).allocatedChunks.push_back(chunk);
            MPs.at(memory).bytes_left -= chunk.size; // subtract from the bytes_left field

            MPs.at(memory).freeChunks.at(i).base = (uint8_t*) MPs.at(memory).freeChunks.at(i).base + chunk.size; // shift the free chunk's base pointer
            MPs.at(memory).freeChunks.at(i).size -= chunk.size; // update the free chunk's size

            // if the length of the free chunk is now 0 then we used all of its space
            if (MPs.at(memory).freeChunks.at(i).size == 0) {
                MPs.at(memory).freeChunks.erase(MPs.at(memory).freeChunks.begin() + i);
            }
            allocated = true;
            break; // done
        }
    }

    if (!allocated) { // if didn't find a large enough chunk
        MachineResumeSignals(&signals);
        return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }

    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer) {
    bool deallocated = false;
    if (memory >= MPs.size() || MPs.at(memory).deleted || pointer == NULL) {
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    TMachineSignalState signals;
    MachineSuspendSignals(&signals);
    // loop through all of the allocated chunks in this memory pool
    for (int i = 0; i < (int) MPs.at(memory).allocatedChunks.size(); i++) { // for each allocated chunk in mem pool
        if (MPs.at(memory).allocatedChunks.at(i).base == pointer) { // found the memory location specified by pointer
            FreeChunk(memory, i); // free the chunk
            MPs.at(memory).bytes_left += MPs.at(memory).allocatedChunks.at(i).size; // update amount of free space in mem pool
            MPs.at(memory).allocatedChunks.erase(MPs.at(memory).allocatedChunks.begin() + i); // erase the chunk from allocatedChunks
            deallocated = true;
            break; // done
        }
    }
    if (!deallocated) { // if didn't deallocate a chunk
        MachineResumeSignals(&signals);
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (memory == 1 && !shared_waitin_Q.empty()) {
        TVMMemoryPoolID id = shared_waitin_Q.top();
        int requiredSize = TCBs.at(id).requiredSize;

        for (int i = 0; i < (int) MPs.at(memory).freeChunks.size(); i++) { // for each free chunk
            if (requiredSize <=  (int)MPs.at(memory).freeChunks.at(i).size) { //found a chunk with enough space
                shared_waitin_Q.pop();
                if (TCBs.at(id).prio > TCBs.at(running_thread).prio) {
                    schedule_thread(VM_THREAD_STATE_READY, id);
                } else {
                    TCBs.at(id).status_t = VM_THREAD_STATE_READY;
                    ReadyThreadPrioQ.push(id);
                }

            }
        }
    }
    MachineResumeSignals(&signals);
    return VM_STATUS_SUCCESS;
}

//FileCallback function, only used in functions below
void FileCallback(void *calldata, int result) {
    TCBs.at(*(TVMThreadID*)calldata).IOresult = result;
    TCBs.at(*(TVMThreadID*)calldata).status_t = VM_THREAD_STATE_READY;
    ReadyThreadPrioQ.push(*(TVMThreadID*)calldata);
    if (TCBs.at(running_thread).prio <= TCBs.at(*(TVMThreadID*)calldata).prio || running_thread == 1) {
        schedule_thread(VM_THREAD_STATE_READY, get_next());
    }
}
