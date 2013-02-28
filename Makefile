SHELL=/bin/sh

# if you do not have mysql libraries or do not want
# to use mysql, you just can comment the two following
# lines
#CFLAGS=-DUSE_MYSQL
#LIBS=-lmysqlclient

# Where you want it installed when you do 'make install'
PREFIX=/usr/local

OBJS= teleinfo.o 

all: teleinfo 

# ===== Compile
teleinfo.o: teleinfo.c
	$(CC) $(CFLAGS) -c teleinfo.c

# ===== Link
teleinfo: teleinfo.o 
	$(CC) $(CFLAGS) $(LDFLAGS) -o teleinfo teleinfo.o $(LIBS)

# ===== Install
install: teleinfo 
	if ( test ! -d $(PREFIX)/bin ) ; then mkdir -p $(PREFIX)/bin ; fi
	cp -f teleinfo $(PREFIX)/bin/teleinfo
	chmod a+x $(PREFIX)/bin/teleinfo

clean: 
	rm -f *.o teleinfo 

