#include "VirtualMachine.h"
#include <fcntl.h>

void VMMain(int argc, char *argv[]){
    int FileDescriptor, Length, Offset;
    char Buffer[1024];

    VMPrint("VMMain opening longtest.txt\n");    
    VMFileOpen("longtest.txt", O_CREAT | O_TRUNC | O_RDWR, 0644, &FileDescriptor);
    VMPrint("VMMain VMFileOpen returned %d\n", FileDescriptor);
    
    
    Offset = ((int)'~' - (int)' ') + 1;
    for(Length = 0; Length < sizeof(Buffer); Length++){
        Buffer[Length] = ' ' + (Length % Offset);
    }
    VMPrint("VMMain writing file\n");
    Length = sizeof(Buffer);
    VMFileWrite(FileDescriptor,Buffer,&Length);
    VMPrint("VMMain VMFileWrite returned %d\n", Length);
    VMPrint("VMMain seeking file\n");
    VMFileSeek(FileDescriptor, 448, 0, &Offset);    
    VMPrint("VMMain VMFileSeek offset at %d\n",Offset);
    
    VMPrint("VMMain reading file\n");
    Length = 128;
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

