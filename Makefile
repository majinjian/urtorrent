## main compiler
CC := g++

## final target
TARGET := urtorrent

## directories
INCDIR := include
OBJDIR := build
SRCDIR := src
DEPDIR := dependency
LIBDIR := lib
SUBDIR += bencode

$(shell mkdir -p $(LIBDIR))

## source files and object files
SOURCE := $(shell find $(SRCDIR) -type f -name '*.cc')
OBJECT := $(patsubst $(SRCDIR)/%, $(OBJDIR)/%, $(SOURCE:.cc=.o))
LIBFILE := $(patsubst %, $(LIBDIR)/lib%.a, $(SUBDIR))

## compile and link options
CCFLAGS := -Wall -g -std=c++11 -I $(INCDIR)
LDFLAGS := -Wall -g
LIBS := -lcrypto -lcurl -lpthread

## dependency options
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

## rules
all: $(TARGET)

$(OBJECT): $(OBJDIR)/%.o: $(SRCDIR)/%.cc $(DEPDIR)/%.d | $(OBJDIR) $(DEPDIR)
	@echo [CC] $@
	@$(CC) $(DEPFLAGS) $(CCFLAGS) -c -o $@ $<
	$(POSTCOMPILE)

$(TARGET): $(OBJECT) $(LIBFILE)
	@echo [link] $@
	@$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

$(LIBFILE):
	@echo [AR] $@
	@$(MAKE) -s -C $(SUBDIR)

$(OBJDIR):
	@mkdir -p $@

$(DEPDIR):
	@mkdir -p $@

$(DEPDIR)/%.d: ;


## check dependencies
include $(wildcard $(patsubst $(SRCDIR)/%, $(DEPDIR)/%, $(SOURCE:.cc=.d)))

## clean option
clean:
	rm -rf $(OBJDIR) $(DEPDIR) $(TARGET)

.PRECIOUS: %.d
.PHONY: clean all
