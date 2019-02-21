#include "VirtualMachine.h"

void VMMain(int argc, char *argv[]){
    VMPrint("Going to sleep for 10 ticks\n");
    VMThreadSleep(10);
    VMPrint("Awake\nGoodbye\n");
}

