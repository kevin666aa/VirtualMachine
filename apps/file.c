#include "VirtualMachine.h"
#include <fcntl.h>

void VMMain(int argc, char *argv[]){
    int FileDescriptor, Length, Offset;
    char Buffer[128];

    VMPrint("VMMain opening test.txt\n");    
    VMFileOpen("test.txt", O_CREAT | O_TRUNC | O_RDWR, 0644, &FileDescriptor);
    VMPrint("VMMain VMFileOpen returned %d\n", FileDescriptor);
    
    VMPrint("VMMain writing file\n");
    Length = 13;
    VMFileWrite(FileDescriptor,"Hello world!\n",&Length);
    VMPrint("VMMain VMFileWrite returned %d\n", Length);
    VMPrint("VMMain seeking file\n");
    VMFileSeek(FileDescriptor, 6, 0, &Offset);    
    VMPrint("VMMain VMFileSeek offset at %d\n",Offset);
    
    VMPrint("VMMain reading file\n");
    Length = sizeof(Buffer);
    VMFileRead(FileDescriptor,Buffer,&Length);
    VMPrint("VMMain VMFileRead returned %d\n", Length);
    if(0 <= Length){
        Buffer[Length] = '\0';
        VMPrint("VMMain read in \"%s\"\n", Buffer);
    }
    VMPrint("VMMain closing file\n");
    VMFileClose(FileDescriptor);
    VMPrint("Goodbye\n");    
}

