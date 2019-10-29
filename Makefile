CC		= gcc
CFLAGS		= -g
TARGET		= oss
OBJECTS		= oss.o
CHILD		= user
CHILDOBJS	= user.o
LOG		= log.txt
DEP		= customStructs.h semaphoreFunc.h bitMap.h
.SUFFIXES: .c .o

ALL: oss user

$(TARGET): $(OBJECTS) $(DEP)
	$(CC) -Wall -o $@ $(OBJECTS)

$(CHILD): $(CHILDOBJS) $(DEP)
	$(CC) -Wall -o $@ $(CHILDOBJS)

.c.o: oss.c user.c
	$(CC) -Wall $(CFLAGS) -c $<

.PHONY: clean
clean:
	/bin/rm -f *.o $(TARGET) $(CHILD)
	/bin/rm $(LOG)
