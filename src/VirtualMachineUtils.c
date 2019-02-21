#include "VirtualMachine.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <time.h>
#include <stdint.h>

#define SMALL_BUFFER_SIZE       256

void *VMLibraryHandle = NULL;

TVMMainEntry VMLoadModule(const char *module){
    
    VMLibraryHandle = dlopen(module, RTLD_NOW);
    if(NULL == VMLibraryHandle){
        fprintf(stderr,"Error dlopen failed %s\n",dlerror());
        return NULL;
    }
    
    return (TVMMainEntry)dlsym(VMLibraryHandle, "VMMain");
}

void VMUnloadModule(void){
    if(NULL != VMLibraryHandle){
        dlclose(VMLibraryHandle);
    }
    VMLibraryHandle = NULL;
}

TVMStatus VMFilePrint(int filedescriptor, const char *format, ...){
    va_list ParamList;
    char *OutputBuffer;
    char SmallBuffer[SMALL_BUFFER_SIZE];
    int SizeRequired;
    TVMStatus ReturnValue;

    va_start(ParamList, format);
    OutputBuffer = SmallBuffer;

    SizeRequired = vsnprintf(OutputBuffer, SMALL_BUFFER_SIZE, format, ParamList);
    if(SizeRequired < SMALL_BUFFER_SIZE){
        ReturnValue = VMFileWrite(filedescriptor, OutputBuffer, &SizeRequired);
        return ReturnValue;
    }
    OutputBuffer = (char *)malloc(sizeof(char) *(SizeRequired + 1));
    va_start(ParamList, format);
    SizeRequired = vsnprintf(OutputBuffer, SizeRequired + 1, format, ParamList);
    ReturnValue = VMFileWrite(filedescriptor, OutputBuffer, &SizeRequired);
    free(OutputBuffer);
    return ReturnValue;
}

TVMStatus VMDateTime(SVMDateTimeRef curdatetime){
    time_t CurrentTime = time(NULL);
    struct tm LocalTime = *localtime(&CurrentTime);
    
    curdatetime->DYear = LocalTime.tm_year + 1900;
    curdatetime->DMonth = LocalTime.tm_mon + 1;
    curdatetime->DDay = LocalTime.tm_mday;
    curdatetime->DHour = LocalTime.tm_hour;
    curdatetime->DMinute = LocalTime.tm_min;
    curdatetime->DSecond = LocalTime.tm_sec;
    curdatetime->DHundredth = 0;

    return VM_STATUS_SUCCESS;
}

uint32_t VMStringLength(const char *str){
    uint32_t Length = 0;

    while(*str){
        str++;
        Length++;
    }
    return Length;
}

void VMStringCopy(char *dest, const char *src){
    while(*src){
        *dest++ = *src++;
    }
    *dest = '\0';
}

void VMStringCopyN(char *dest, const char *src, int32_t n){
    while(*src && n){
        n--;
        *dest++ = *src++;
    }
    *dest = '\0';
}

void VMStringConcatenate(char *dest, const char *src){
    VMStringCopy(dest + VMStringLength(dest),src);
}

TVMStatus VMFileSystemValidPathName(const char *name){
    const char InvalidCharacters[]={'\\', '?', '*','<','>','|'};
    int32_t CharIndex;

    while(*name){
        for(CharIndex = 0; CharIndex < sizeof(InvalidCharacters); CharIndex++){
            if(InvalidCharacters[CharIndex] == *name){
                return VM_STATUS_ERROR_INVALID_PARAMETER;
            }
        }
        name++;
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemIsRelativePath(const char *name){
    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER != *name){
        return VM_STATUS_SUCCESS;
    }
    return VM_STATUS_ERROR_INVALID_PARAMETER;
}

TVMStatus VMFileSystemIsAbsolutePath(const char *name){
    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == *name){
        return VM_STATUS_SUCCESS;
    }
    return VM_STATUS_ERROR_INVALID_PARAMETER;
}

TVMStatus VMFileSystemGetAbsolutePath(char *abspath, const char *curpath, const char *destpath){
    if(VM_STATUS_SUCCESS == VMFileSystemIsAbsolutePath(destpath)){
        VMStringCopy(abspath, destpath);
        return VM_STATUS_SUCCESS;
    }
    else{
        // figure out the absolute path
        return VMFileSystemSimplifyPath(abspath, curpath, destpath);
    }
}

TVMStatus VMFileSystemPathIsOnMount(const char *mntpt, const char *destpath){
    char RelPath[VM_FILE_SYSTEM_MAX_PATH];
    // figure out if on this mount point
    if(NULL != mntpt){
        VMFileSystemRelativePath(RelPath, mntpt, destpath);
        if(('.' == RelPath[0])&&('.' == RelPath[1])&&((VM_FILE_SYSTEM_DIRECTORY_DELIMETER == RelPath[2])||('\0' == RelPath[2]))){
            // Not on this mount point
            return VM_STATUS_FAILURE;
        }
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemDirectoryFromFullPath(char *dirname, const char *path){
    int32_t DelimiterPosition = -1, Index;

    Index = 0;
    while(path[Index]){
        if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == path[Index]){
            DelimiterPosition = Index;
        }
        Index++;
    }
    if(VM_FILE_SYSTEM_MAX_PATH <= DelimiterPosition){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(-1 == DelimiterPosition){
        dirname[0] = '\0';
    }
    else if(DelimiterPosition){
        Index = 0;
        while(Index <  DelimiterPosition){
            dirname[Index] = path[Index];
            Index++;
        }
        dirname[Index] = '\0';
    }
    else{
        dirname[0] = VM_FILE_SYSTEM_DIRECTORY_DELIMETER;
        dirname[1] = '\0';
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemFileFromFullPath(char *filename, const char *path){
    int32_t DelimiterPosition = -1, Index;

    Index = 0;
    while(path[Index]){
        if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == path[Index]){
            DelimiterPosition = Index;
        }
        Index++;
    }
    if(-1 == DelimiterPosition){
        if(VM_FILE_SYSTEM_MAX_PATH <= Index){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        VMStringCopy(filename, path);
    }
    else{
        if(VM_FILE_SYSTEM_MAX_PATH <= (Index - DelimiterPosition - 1)){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        VMStringCopy(filename, path + DelimiterPosition + 1);
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemConsolidatePath(char *fullpath, const char *dirname, const char *filename){
    uint32_t Index, DelimeterWasLast = 0;

    Index = 0;
    while(dirname[Index]){
        DelimeterWasLast = VM_FILE_SYSTEM_DIRECTORY_DELIMETER == dirname[Index];
        *fullpath++ = dirname[Index];
        if(VM_FILE_SYSTEM_MAX_PATH - 1 == Index){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        Index++;
    }
    if(!DelimeterWasLast){
        *fullpath++ = VM_FILE_SYSTEM_DIRECTORY_DELIMETER;
        Index++;
    }
    if(VM_FILE_SYSTEM_MAX_PATH <= (VMStringLength(filename) + Index)){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    VMStringCopy(fullpath, filename);

    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemSimplifyPath(char *simpath, const char *abspath, const char *relpath){
    int32_t SimpathLength;

    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == *relpath){
        // relative path is an absolute path
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER != *abspath){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    SimpathLength = 0;
    while(abspath[SimpathLength]){
        if(VM_FILE_SYSTEM_MAX_PATH <= SimpathLength){
            return VM_STATUS_ERROR_INVALID_PARAMETER;
        }
        simpath[SimpathLength] = abspath[SimpathLength];
        SimpathLength++;
    }
    while(*relpath){
        if('.' == *relpath){
            if('.' == relpath[1]){
                // Could be ..
                if(('\0' == relpath[2])||(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == relpath[2])){
                    if(0 == SimpathLength){
                        return VM_STATUS_FAILURE;
                    }
                    SimpathLength--;
                    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == simpath[SimpathLength]){
                        if(0 == SimpathLength){
                            return VM_STATUS_FAILURE;
                        }
                        SimpathLength--;
                    }
                    while(SimpathLength && (VM_FILE_SYSTEM_DIRECTORY_DELIMETER != simpath[SimpathLength])){
                        SimpathLength--;
                    }
                    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == simpath[SimpathLength]){
                        SimpathLength++;
                    }
                    if('\0' == relpath[2]){
                        break;
                    }
                    relpath += 3;
                    continue;
                }
            }
            else if('\0' == relpath[1]){
                break;
            }
            else if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == relpath[1]){
                relpath += 2;
                continue;
            }
        }

        // add next entry
        if((0 == SimpathLength)||(VM_FILE_SYSTEM_DIRECTORY_DELIMETER != simpath[SimpathLength-1])){
            simpath[SimpathLength++] = VM_FILE_SYSTEM_DIRECTORY_DELIMETER;
        }
        while(*relpath){
            if(VM_FILE_SYSTEM_MAX_PATH <= SimpathLength){
                return VM_STATUS_ERROR_INVALID_PARAMETER;
            }
            simpath[SimpathLength++] = *relpath;
            if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == *relpath){
                relpath++;
                break;
            }
            relpath++;
        }
    }
    if((1 < SimpathLength)&&(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == simpath[SimpathLength - 1])){
        SimpathLength--;
    }
    simpath[SimpathLength] = '\0';
    return VM_STATUS_SUCCESS;
}

TVMStatus VMFileSystemRelativePath(char *relpath, const char *basepath, const char *destpath){
    int32_t DelimiterPosition = -1, Index, Updirectories, RelPathLength = 0;

    Updirectories = 0;
    Index = 0;
    while(basepath[Index] && (basepath[Index] == destpath[Index])){
        if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == basepath[Index]){
            DelimiterPosition = Index;
        }
        Index++;
    }
    if(('\0' == basepath[Index])&&((VM_FILE_SYSTEM_DIRECTORY_DELIMETER == destpath[Index])||('\0' == destpath[Index]))){
        destpath += Index;
    }
    else if(-1 == DelimiterPosition){
        if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == destpath[0]){
            // can't determine relative path
            return VM_STATUS_FAILURE;
        }
        if('\0' != destpath[0]){
            Updirectories = 1;
            Index = 0;
            while(basepath[Index]){
                if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == basepath[Index]){
                    Updirectories++;
                }
                Index++;
            }
        }
    }
    else{
        Index = DelimiterPosition+1;
        while(basepath[Index]){
            if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == basepath[Index-1]){
                Updirectories++;
            }
            Index++;
        }
        destpath += DelimiterPosition;
    }
    if(Updirectories){
        while(Updirectories--){
            RelPathLength += 3;
            if(VM_FILE_SYSTEM_MAX_PATH <= RelPathLength){
                return VM_STATUS_ERROR_INVALID_PARAMETER;
            }
            *relpath++ = '.';
            *relpath++ = '.';
            *relpath++ = VM_FILE_SYSTEM_DIRECTORY_DELIMETER;
        }
    }
    else{
        *relpath++ = '.';
        *relpath++ = VM_FILE_SYSTEM_DIRECTORY_DELIMETER;
    }
    if(VM_FILE_SYSTEM_DIRECTORY_DELIMETER == *destpath){
        destpath++;
    }
    if(VM_FILE_SYSTEM_MAX_PATH <= (VMStringLength(destpath) + RelPathLength)){
        return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    // Now copy dest to relpath
    VMStringCopy(relpath, destpath);

    return VM_STATUS_SUCCESS;
}

