SHELL=/bin/sh

# Teleinfo Makefile version 1.0.8

# if you do not want teleinfo log to mysql database you can remove 
# the link with mysql client library
# could be usfull because installing mysqlclient library can be quite
# complicated on NAS device such as synology and different versions
# so compile with it only is needed or if it works
# Comment these 2 following lines if you want to use mysql features
CFLAGSSQL=-DUSE_MYSQL
LIBSSQL=-lmysqlclient

# if you do not want to compile for EMONCMS publication
# remove the link to curl library
# Comment these 2 following lines if you do not want to use emoncms features
CFLAGSEMONCMS=-DUSE_EMONCMS 
LIBSEMONCMS=-lcurl 

# Where you want it installed when you do 'make install'
# /bin will be added to that prefix.
PREFIX=/usr/local

OBJS= teleinfo.o 

all: teleinfo 

# ===== Compile
teleinfo.o: teleinfo.c
	$(CC) $(CFLAGS) $(CFLAGSSQL) $(CFLAGSEMONCMS) -c teleinfo.c

# ===== Link
teleinfo: teleinfo.o 
	$(CC) $(CFLAGS) $(CFLAGSMYSQL) $(CFLAGSEMONCMS) $(LDFLAGS) -o teleinfo teleinfo.o $(LIBSEMONCMS) $(LIBSSQL)

# ===== Install
install: teleinfo 
	if ( test ! -d $(PREFIX)/bin ) ; then mkdir -p $(PREFIX)/bin ; fi
	cp -f teleinfo $(PREFIX)/bin/teleinfo
	chmod a+x $(PREFIX)/bin/teleinfo

clean: 
	rm -f *.o teleinfo 

