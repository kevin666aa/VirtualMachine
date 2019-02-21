#include "VirtualMachine.h"
#include <fcntl.h>

int StringMatch(const char *left, const char *right){
    while(*left && *right){
        if(*left != *right){
            return 0;    
        }
        left++;
        right++;
    }
    return (*left == *right);
}

int StringMatchN(const char *left, const char *right, int n){
    while(*left && *right && n){
        if(*left != *right){
            return 0;    
        }
        left++;
        right++;
        n--;
    }
    if(0 == n){
        return 1;    
    }
    return (*left == *right);
}

void VMMain(int argc, char *argv[]){
    int DirDescriptor, FileDescriptor, Length;
    char LineBuffer[1024];
    char DirectoryName[VM_FILE_SYSTEM_MAX_PATH];
    SVMDirectoryEntry DirectoryEntry;
    int CharactersIn = 0;
    int Mil, Kil, One;
    
    while(1){
        VMDirectoryCurrent(DirectoryName);
        VMPrint("%s> ",DirectoryName);
        CharactersIn = 0;
        while(1){
            Length = 1;
            VMFileRead(0, LineBuffer + CharactersIn, &Length);
            if('\n' == LineBuffer[CharactersIn]){
                LineBuffer[CharactersIn] = '\0';
                break;
            }
            if((0 == CharactersIn)&&(' ' == LineBuffer[CharactersIn])){
                continue;
            }
            CharactersIn++;
        }
        
        while(0 < CharactersIn){
            CharactersIn--;
            if(' ' != LineBuffer[CharactersIn]){
                CharactersIn++;
                break;
            }
            LineBuffer[CharactersIn] = '\0';
        }
        if(StringMatch(LineBuffer,"exit")){
            break;
        }
        else if(StringMatch(LineBuffer,"ls")){
            
            if(VM_STATUS_SUCCESS == VMDirectoryOpen(DirectoryName, &DirDescriptor)){
                VMPrint("   DATE   |  TIME  | TYPE |    SIZE   |    SFN      |  LFN\n");
                while(VM_STATUS_SUCCESS == VMDirectoryRead(DirDescriptor, &DirectoryEntry)){
                    VMPrint("%04d/%02d/%02d %02d:%02d %s ",DirectoryEntry.DModify.DYear, DirectoryEntry.DModify.DMonth, DirectoryEntry.DModify.DDay, (DirectoryEntry.DModify.DHour % 12) ? (DirectoryEntry.DModify.DHour % 12) : 12 , DirectoryEntry.DModify.DMinute, DirectoryEntry.DModify.DHour >= 12 ? "PM" : "AM");
                    VMPrint("%s ", DirectoryEntry.DAttributes & VM_FILE_SYSTEM_ATTR_DIRECTORY ? "<DIR> " : "<FILE>");
                    Mil = DirectoryEntry.DSize / 1000000;
                    Kil = (DirectoryEntry.DSize / 1000) % 1000;
                    One = DirectoryEntry.DSize % 1000;
                    if(Mil){
                        VMPrint("%3d,%03d,%03d ",Mil, Kil, One);   
                    }
                    else if(Kil){
                        VMPrint("    %3d,%03d ", Kil, One);
                    }
                    else if(0 == (DirectoryEntry.DAttributes & VM_FILE_SYSTEM_ATTR_DIRECTORY)){
                        VMPrint("        %3d ",One);
                    }
                    else{
                        VMPrint("            ");   
                    }
                    VMPrint("%-13s %s\n",DirectoryEntry.DShortFileName, DirectoryEntry.DLongFileName);
                }
                VMDirectoryClose(DirDescriptor);
            }
            else{
                VMPrint("Failed to open directory %s!\n", DirectoryName);   
            }
        }
        else if(StringMatchN(LineBuffer,"cd",2)){
            CharactersIn = 2;
            while(' ' == LineBuffer[CharactersIn]){
                CharactersIn++;
            }
            if('\0' == LineBuffer[CharactersIn]){
                CharactersIn--;
                LineBuffer[CharactersIn] = '/';
            }
            if(VM_STATUS_SUCCESS != VMDirectoryChange(LineBuffer + CharactersIn)){
                VMPrint("Failed to change directory to %s!\n", LineBuffer + CharactersIn);
            }
        }
        else if(StringMatchN(LineBuffer,"cat",3)){
            CharactersIn = 3;
            while(' ' == LineBuffer[CharactersIn]){
                CharactersIn++;
            }
            if(VM_STATUS_SUCCESS == VMFileOpen(LineBuffer + CharactersIn, O_RDONLY, 0644, &FileDescriptor)){
                Length = sizeof(LineBuffer);
                while(VM_STATUS_SUCCESS == VMFileRead(FileDescriptor, LineBuffer, &Length)){
                    if(Length){
                        VMFileWrite(1,LineBuffer,&Length);
                    }
                    if(Length < sizeof(LineBuffer)){
                        break;
                    }
                    Length = sizeof(LineBuffer);
                }
                VMFileClose(FileDescriptor);
            }
            else{
                VMPrint("Failed to open file %s!\n", LineBuffer + CharactersIn);
            }
        }
        else if(StringMatchN(LineBuffer,"mkdir",5)){
            CharactersIn = 5;
            while(' ' == LineBuffer[CharactersIn]){
                CharactersIn++;
            }
            if('\0' == LineBuffer[CharactersIn]){
                CharactersIn--;
                LineBuffer[CharactersIn] = '/';
            }
            if(VM_STATUS_SUCCESS != VMDirectoryCreate(LineBuffer + CharactersIn)){
                VMPrint("Failed to create directory to %s!\n", LineBuffer + CharactersIn);
            }
        }
        else if(StringMatchN(LineBuffer,"rm",2)){
            CharactersIn = 2;
            while(' ' == LineBuffer[CharactersIn]){
                CharactersIn++;
            }
            if(VM_STATUS_SUCCESS != VMDirectoryUnlink(LineBuffer + CharactersIn)){
                VMPrint("Failed to remove %s!\n", LineBuffer + CharactersIn);
            }
        }
    }
}

