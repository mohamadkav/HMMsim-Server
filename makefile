#
# Copyright (c) 2015 Santiago Bock
#
# See the file LICENSE.txt for copying permission.
#

##############################################################
#
# Customize build
#
##############################################################

# Debug
DEBUG=0

# Print debug output
DEBUG_OUTPUT = 1

# To use custom compiler
CXXHOME = /usr
#CXXHOME = /home/sab104/opt/gcc
#CXXHOME = /afs/cs.pitt.edu/usr0/sab104/pcm/traces-1/gcc-4.8.2/nickel

# Use this for profiling
#CXXHOME = /home/sab104/opt/gcc_pg -pg
#CUSTOM_FLAGS += -pg

#For shared libraries
CUSTOM_LINK += -Wl,-rpath -Wl,$(CXXHOME)/lib

#For static libraries
#CUSTOM_LINK += -static-libstdc++ -static-libgcc

# Where PIN is installed
PIN_ROOT ?= /cygdrive/d/Software/pinplay-drdebug-2.2-pldi2015-pin-2.14-71313-gcc.4.4.7-linux
#PIN_ROOT ?= /cygdrive/c/Users/Salkhordeh/Desktop/pin-3.0-76991-gcc-linux/pin-3.0-76991-gcc-linux
#PIN_ROOT ?= /cygdrive/c/Users/Salkhordeh/Desktop/pin-3.0-76991-msvc-windows/pin-3.0-76991-msvc-windows
#PIN_ROOT ?= /afs/cs.pitt.edu/usr0/sab104/bin/pin

# Actual compiler to use
CXX = $(CXXHOME)/bin/g++

##############################################################
#
# set up and include *.config files
#
##############################################################

KIT=1

CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config

PINPLAY_HOME=$(PIN_ROOT)/extras/pinplay
PINPLAY_INCLUDE_HOME=$(PINPLAY_HOME)/include
PINPLAY_LIB_HOME=$(PINPLAY_HOME)/lib/$(TARGET)
EXT_LIB_HOME=$(PINPLAY_HOME)/lib-ext/$(TARGET)

include $(CONFIG_ROOT)/makefile.config
include $(TOOLS_ROOT)/Config/makefile.default.rules

##############################################################
#
# Variable definition
#
##############################################################



# Flags
CUSTOM_FLAGS += -MMD -DDEBUG=$(DEBUG_OUTPUT) -D_FILE_OFFSET_BITS=64 -std=c++11 -Wall -Werror -iquoteinclude -g -O0
#CUSTOM_FLAGS += -D_GLIBCXX_DEBUG
APP_CXXFLAGS += $(CUSTOM_FLAGS) 
APP_LIBS += -lbz2 -lz $(CUSTOM_LINK)
TOOL_CXXFLAGS  += $(CUSTOM_FLAGS) -I$(PINPLAY_INCLUDE_HOME) 
TOOL_LPATHS += -L$(PINPLAY_LIB_HOME)
TOOL_LIBS += -lbz2 -lz $(CUSTOM_LINK)

##############################################################
#
# Variable definition for build process
#
##############################################################

APP_ROOTS = analyze convert merge parse sim split texter

APPS = $(APP_ROOTS:%=$(OBJDIR)%)

TOOL_ROOTS = TracerPin

TOOLS = $(TOOL_ROOTS:%=$(OBJDIR)%$(PINTOOL_SUFFIX))

##############################################################
#
# build rules
#
##############################################################

# Build everything
all: apps tools

# Accelerate the make process and prevent errors
.PHONY: all

# Include generated dependencies
-include $(OBJDIR)*.d

# Build the applications
apps: $(OBJDIR) $(APPS)

# Build the tools
tools: $(OBJDIR) $(TOOLS)

# Create the output directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compile regular object files
$(OBJDIR)%.o: %.cpp
	$(CXX) $(APP_CXXFLAGS) $(COMP_OBJ)$@ $<

#Compile TracerPin, TraceHandler and Error with (TOOL_CXXFLAGS)
$(OBJDIR)TracerPin.o $(OBJDIR)TraceHandler.o $(OBJDIR)Error.o: $(OBJDIR)%.o : %.cpp
	$(CXX) $(TOOL_CXXFLAGS) $(COMP_OBJ)$@ $<

# Link tools
$(TOOLS): %$(PINTOOL_SUFFIX) : %.o $(PINPLAY_LIB_HOME)/libpinplay.a $(EXT_LIB_HOME)/libbz2.a $(EXT_LIB_HOME)/libz.a $(CONTROLLERLIB)
	$(LINKER) $(TOOL_LDFLAGS) $(LINK_EXE)$@ $^ $(TOOL_LPATHS) $(TOOL_LIBS)

# Link apps
$(APPS): % : %.o
	$(CXX) $(APP_LDFLAGS) $(LINK_EXE)$@  $^ $(APP_LPATHS) $(APP_LIBS)


# Dependencies for object linking
$(OBJDIR)TracerPin.so: $(OBJDIR)TraceHandler.o $(OBJDIR)Error.o
$(OBJDIR)analyze: $(OBJDIR)analyze.o $(OBJDIR)Arguments.o $(OBJDIR)Cache.o $(OBJDIR)Engine.o $(OBJDIR)Error.o $(OBJDIR)Statistics.o $(OBJDIR)TraceHandler.o
$(OBJDIR)convert: $(OBJDIR)convert.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)merge: $(OBJDIR)merge.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)parse: $(OBJDIR)parse.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)Counter.o
$(OBJDIR)sim: $(OBJDIR)sim.o $(OBJDIR)Arguments.o $(OBJDIR)Bank.o $(OBJDIR)Bus.o $(OBJDIR)Cache.o $(OBJDIR)Counter.o $(OBJDIR)CPU.o $(OBJDIR)Engine.o $(OBJDIR)Error.o $(OBJDIR)HybridMemory.o $(OBJDIR)Memory.o $(OBJDIR)MemoryManager.o $(OBJDIR)Migration.o $(OBJDIR)Partition.o $(OBJDIR)Statistics.o $(OBJDIR)TraceHandler.o
$(OBJDIR)split: $(OBJDIR)split.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o
$(OBJDIR)texter: $(OBJDIR)texter.o $(OBJDIR)Arguments.o $(OBJDIR)Error.o $(OBJDIR)TraceHandler.o


# Cleaning
#.PHONY: clean
#clean:
#	-rm -rf $(OBJDIR)

# Print variable for debugging the makefile
.PHONY: print
print:
	@echo $(APP_LIBS)
