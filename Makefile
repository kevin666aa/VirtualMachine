AR=ar
AS=as
CC=gcc
CPP=cpp
CXX=g++
LD=ld
OBJCOPY=objcopy
OBJDUMP=objdump
STRIP=strip

SRC_DIR=./src
OBJ_DIR=./obj
BIN_DIR=./bin
APPSRC_DIR=./apps
APPOBJ_DIR=./apps/obj

OBJS=$(OBJ_DIR)/Machine.o \
     $(OBJ_DIR)/VirtualMachineUtils.o \
     $(OBJ_DIR)/VirtualMachine.o \
     $(OBJ_DIR)/main.o

MODOBJS=$(OBJ_DIR)/module.o
     
     
#DEBUG_MODE=TRUE
UNAME := $(shell uname)

ifdef DEBUG_MODE
DEFINES += -DDEBUG
endif

INCLUDES += -I $(SRC_DIR) 
LIBRARIES = -ldl

CFLAGS += -Wall -U_FORTIFY_SOURCE $(INCLUDES) $(DEFINES)
APPCFLAGS += -Wall -fPIC $(INCLUDES) $(DEFINES)

ifdef DEBUG_MODE
CFLAGS += -g -ggdb
APPCFLAGS += -g -ggdb
else
CFLAGS += -O3
APPCFLAGS += -O3
endif

ifeq ($(UNAME), Darwin)
LDFLAGS = $(DEFINES) $(INCLUDES) $(LIBRARIES) 
APPLDFLAGS += $(DEFINES) $(INCLUDES) -shared -rdynamic -flat_namespace -undefined suppress
else
LDFLAGS = $(DEFINES) $(INCLUDES) $(LIBRARIES) -Wl,-E
APPLDFLAGS += $(DEFINES) $(INCLUDES) -shared -rdynamic -Wl,-E
endif

all: directories $(BIN_DIR)/vm 
apps: directories $(BIN_DIR)/hello.so $(BIN_DIR)/sleep.so $(BIN_DIR)/file.so $(BIN_DIR)/thread.so $(BIN_DIR)/preempt.so $(BIN_DIR)/file2.so $(BIN_DIR)/memory.so $(BIN_DIR)/mutex.so $(BIN_DIR)/copyfile.so $(BIN_DIR)/badprogram.so $(BIN_DIR)/badprogram2.so $(BIN_DIR)/copyfile2.so $(BIN_DIR)/shell.so $(BIN_DIR)/shell2.so 

$(BIN_DIR)/vm: $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $(BIN_DIR)/vm
	
FORCE: ;

.PRECIOUS: $(APPOBJ_DIR)/%.o
#
# use gmake's implicit rules for now, except this one:
#
$(BIN_DIR)/%.so: $(APPOBJ_DIR)/%.o
	$(CC) $(APPLDFLAGS) $< -o $@

$(APPOBJ_DIR)/%.o : $(APPSRC_DIR)/%.c
	$(CC) -c $(APPCFLAGS) $< -o $@

$(APPOBJ_DIR)/%.o : $(APPSRC_DIR)/%.cpp 
	$(CXX) -c $(APPCFLAGS) $(CPPFLAGS) $< -o $@

$(OBJ_DIR)/Machine.o : $(SRC_DIR)/Machine.cpp 
	$(CXX) -c $(CFLAGS) $(CPPFLAGS) -O0 $(SRC_DIR)/Machine.cpp -o $(OBJ_DIR)/Machine.o
	
$(OBJ_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJ_DIR)/%.o : $(SRC_DIR)/%.cpp 
	$(CXX) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

	
.PHONY: directories
directories:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)
	mkdir -p $(APPOBJ_DIR)

	
.PHONY: clean_directories
clean_directories:
	rm -rf $(BIN_DIR)
	rm -rf $(OBJ_DIR)
	rm -rf $(APPOBJ_DIR)
	
.PHONY: clean
clean: clean_directories directories
