#include "VirtualMachine.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]){
    int TickTimeMS = 100;
    TVMMemorySize HeapSize = 0x1000000;
    TVMMemorySize SharedSize = 0x4000;
    int Offset = 1;
    char *FATMount = "fat.ima";
    
    while(Offset < argc){
        if(0 == strcmp(argv[Offset], "-t")){
            // Tick time in ms
            Offset++;
            if(Offset >= argc){
                break;   
            }
            if(1 != sscanf(argv[Offset],"%d",&TickTimeMS)){
                fprintf(stderr,"Invalid parameter for -t of \"%s\".\n",argv[Offset]);
                return 1;
            }
            if(0 >= TickTimeMS){
                fprintf(stderr,"Invalid parameter for -t must be positive!\n"); 
                return 1;
            }
        }
        else if(0 == strcmp(argv[Offset], "-h")){
            // Tick time in ms
            Offset++;
            if(Offset >= argc){
                break;   
            }
            if(1 != sscanf(argv[Offset],"%u",&HeapSize)){
                fprintf(stderr,"Invalid parameter for -h of \"%s\".\n",argv[Offset]);    
                return 1;
            }
            if(0 >= HeapSize){
                fprintf(stderr,"Invalid parameter for -h must be positive!\n");    
                return 1;
            }
        }
        
        else if(0 == strcmp(argv[Offset], "-s")){
            // Tick time in ms
            Offset++;
            if(Offset >= argc){
                break;   
            }
            if(1 != sscanf(argv[Offset],"%u",&SharedSize)){
                fprintf(stderr,"Invalid parameter for -s of \"%s\".\n",argv[Offset]);    
                return 1;
            }
            if(0 >= SharedSize){
                fprintf(stderr,"Invalid parameter for -s must be positive!\n");    
                return 1;
            }
        }
        else if(0 == strcmp(argv[Offset], "-f")){
            // FAT Mount
            Offset++;
            if(Offset >= argc){
                break;   
            }
            FATMount = argv[Offset];
        }
        else{
            break;
        }
        Offset++;
    }
    
    if(Offset >= argc){
        fprintf(stderr,"Syntax Error: vm [options] module [moduleoptions]\n");    
        return 1;
    }
    
    
    if(VM_STATUS_SUCCESS != VMStart(TickTimeMS, HeapSize, SharedSize, FATMount, argc - Offset, argv + Offset)){
        fprintf(stderr,"Virtual Machine failed to start.\n");    
        return 1;
    }
    
    
    return 0;
}




