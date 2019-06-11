CC := gcc
INCLUDES += -I../peer/ -I. -I..
CFLAGS   += -Wall -Werror -fdiagnostics-color=always $(INCLUDES)
LDFLAGS  += -L $(INCLUDE) -Wall -Werror -Wextra -fdiagnostics-color=always $(INCLUDES) -L../peer/
LDLIBS   += -lcrypto -lssl -lpeer

# set vpath
vpath # clear
vpath %.h ..
vpath %.h ../peer

ifeq ($(strip $(OPTIM)), yes)
  CFLAGS += -O2
endif

ifneq ($(strip $(DEBUG)), no)
  CFLAGS += -g
endif

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
TARGET  := client

.PHONY: all
all: ../peer/libpeer.a $(TARGET)    ## compile and copy upstream
	cp $(TARGET) ..

../peer/libpeer.a:
	make -C ../peer

-include autodep

$(TARGET): $(OBJECTS)	## compile target

.PHONY: clean
clean: ## remove garbage
	rm -f $(OBJECTS) $(TARGET) vgcore*
	rm -f ../utils/*.o
	rm -f ../peer/*.o

.PHONY: depend
depend: $(SOURCES) ## do the dependencies
	$(CC) $(INCLUDES) -MM $(SOURCES) > autodep

.PHONY: certs
certs: ## create certificates
	make -C .. certs

.PHONY: help
help:	## display options
	@grep -E '^[a-zA-Z_-]+:.*## .*' Makefile | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'
