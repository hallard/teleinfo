/* ======================================================================
Program : teleinfo
Purpose : grab teleinformation from serial line then write to MySql or Net
Version : 1.05
Author  : (c) Charles-Henri Hallard
Comments: some code grabbed from picocom and other from teleinfo
	: You can use or distribute this code unless you leave this comment
	: too see thos code correctly indented, please use Tab values of 2
====================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <termios.h>
#ifdef USE_MYSQL
#include <mysql/mysql.h>
#endif
#include <netinet/in.h>
#include <getopt.h>


// ----------------
// Constants
// ----------------
#define true 1
#define false 0

#define PRG_NAME              "teleinfo"

#ifdef USE_MYSQL
  #define PRG_VERSION     "1.05 (mysql)"
#else
  #define PRG_VERSION     "1.05 (nosql)"
#endif
#define PRG_DIR			"/usr/local/bin" 				// Directory where we are when in daemon mode

// Define teleinfo mode, device or network
#define TELEINFO_DEVICE 	"/dev/teleinfo"
#define TELEINFO_PORT			1200							// Port used to send frame over Network
#define TELEINFO_NETWORK	"192.168.1.255"		// Broadcast or spcific IP where to send frame

#define UUCP_LOCK_DIR	"/var/lock"

// Local File for teleinfo
//#define TELEINFO_DATALOG 	"/tmp/teleinfo.log"
//#define TELEINFO_TRAMELOG "/tmp/teleinfotrame."
#define	TELEINFO_BUFSIZE	512

#define	STX 0x02
#define	ETX 0x03

//#ifdef USE_MYSQL
  // Define mysql
  #define MYSQL_HOST 	"localhost"
  #define MYSQL_LOGIN "login"
  #define MYSQL_PASS	"password"
  #define MYSQL_DB 		"database"
  #define MYSQL_TABLE	"DbiTeleinfo"
  #define MYSQL_PORT	3306
//#endif

enum parity_e 		{  P_NONE, 	P_EVEN, 	 P_ODD };
enum flowcntrl_e 	{ FC_NONE, 	FC_RTSCTS, FC_XONXOFF };
enum mode_e 			{ MODE_SEND, 	MODE_RECEIVE,	MODE_MYSQL, MODE_TEST };

// Config Option
struct 
{
	char port[128];
	int baud;
	enum flowcntrl_e flow;
	char *flow_str;
	enum parity_e parity;
	char *parity_str;
	int databits;
	int nolock;
	int mode;
	char *mode_str;
	int netport;
	int verbose;
#ifdef USE_MYSQL
	char server[32];
	char user[32];
	char password[32];
	char database[32];
	char table[32];
#endif
        int daemon;

} opts = {
	.port = TELEINFO_DEVICE,
	.baud = 1200,
	.flow = FC_NONE,
	.flow_str = "none",
	.parity = P_EVEN,
	.parity_str = "even",
	.databits = 7,
	.nolock = false,
	.mode = MODE_RECEIVE,
	.mode_str = "receive",
	.netport = TELEINFO_PORT,
	.verbose = false,
#ifdef USE_MYSQL
	.server = MYSQL_HOST,
	.user = MYSQL_LOGIN,
	.password = MYSQL_PASS,
	.database = MYSQL_DB,
	.table = MYSQL_TABLE,
#endif
        .daemon = false
};

// ======================================================================
// Global vars 
// ======================================================================
int 	g_fd_teleinfo; 					// teleinfo serial handle
struct termios g_oldtermios ; // old serial config
int 	g_tlf_sock;							// teleinfo socket 
int		g_exit_pgm;							// indicate en of the program
char 	g_lockname[256] = ""; // Lock filename

// ======================================================================
// Global funct
// ======================================================================
void tlf_close_serial(int);

/* ======================================================================
Function: log_syslog
Purpose : write event to syslog
Input 	: stream to write if needed
					string to write in printf format
					printf other arguments
Output	: -
Comments: 
====================================================================== */
void log_syslog( FILE * stream, const char *format, ...)
{
	static char tmpbuff[512]="";
	va_list args;
	int len;

	// do a style printf style in ou buffer
	va_start (args, format);
	len = vsnprintf (tmpbuff, sizeof(tmpbuff), format, args);
	tmpbuff[sizeof(tmpbuff) - 1] = '\0';
	va_end (args);

	// Write to logfile
	openlog( PRG_NAME, LOG_PID|LOG_CONS, LOG_USER);
 	syslog(LOG_INFO, tmpbuff);
 	closelog();
 	
 	// stream passed ? write also to it
 	if (stream && opts.verbose && !opts.daemon ) 
 	{
 		fprintf(stream, tmpbuff);
 		fflush(stream);
 	}
}


/* ======================================================================
Function: clean_exit
Purpose : exit program 
Input 	: exit code
Output	: -
Comments: 
====================================================================== */
void clean_exit (int exit_code)
{
	int r;	
	
	// close serials
  if (g_fd_teleinfo)
  {
		// Restore Old parameters.
  	if (  (r = tcsetattr(g_fd_teleinfo, TCSAFLUSH, &g_oldtermios)) < 0 )
			log_syslog(stderr, "cannot restore old parameters %s: %s", opts.port, strerror(errno));

		// then close
  	tlf_close_serial(g_fd_teleinfo);
  }

 	// close socket
 	if (g_tlf_sock)
 		close(g_tlf_sock);

	if ( exit_code == EXIT_SUCCESS)
	{			
			log_syslog(stdout, "Succeded to do my job\n");
	}
	else
	{
			log_syslog(stdout, "Closing teleinfo due to error\n");
	}
		
	// wait a bit for output to drain
	if (opts.mode == MODE_SEND)
		sleep(1);

	if (!opts.nolock)
		uucp_unlock();
	
	exit(exit_code);
}

/* ======================================================================
Function: fatal
Purpose : exit program due to a fatal error
Input 	: string to write in printf format
					printf other arguments
Output	: -
Comments: 
====================================================================== */
void fatal (const char *format, ...)
{
	char tmpbuff[512] = "";
	va_list args;
	int len;

	va_start(args, format);
	len = vsnprintf(tmpbuff, sizeof(tmpbuff), format, args);
	tmpbuff[sizeof(tmpbuff) - 1] = '\0';
	va_end(args);

	// Write to logfile
	openlog( PRG_NAME, LOG_PID | LOG_CONS, LOG_USER);
 	syslog(LOG_INFO, tmpbuff);
 	closelog();

	fprintf(stderr,"\r\nFATAL: %s \r\n", tmpbuff );
	fflush(stderr);

	clean_exit(EXIT_FAILURE);
}



/* ======================================================================
Function: daemonize
Purpose : daemonize the pocess
Input 	: -
Output	: -
Comments: 
====================================================================== */
static void daemonize(void)
{
	pid_t pid, sid;
	
	// already a daemon
	if ( getppid() == 1 ) 
		return;
	
	// Fork off the parent process 
	pid = fork();
	if (pid < 0) 
   	fatal( "fork() : %s", strerror(errno));

	// If we got a good PID, then we can exit the parent process.
	if (pid > 0) 
	  exit(EXIT_SUCCESS);

	
	// At this point we are executing as the child process
	// ---------------------------------------------------

	// Change the file mode mask 
	umask(0);
	
	// Create a new SID for the child process 
	sid = setsid();
	if (sid < 0) 
   	fatal( "setsid() : %s", strerror(errno));

	// Change the current working directory.  This prevents the current
	// directory from being locked; hence not being able to remove it.
	if ((chdir(PRG_DIR)) < 0) 
   	fatal( "chdir('%s') : %s", PRG_DIR, strerror(errno));
	
	// Close standard files 
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}


/* ======================================================================
Function: uucp_lockname
Purpose : create the lock filename
Input 	: 
Output	: -
Comments: Routine taken from picocom
===================================================================== */
int uucp_lockname(const char *dir, const char *file)
{
	char *p, *cp;
	struct stat sb;

	if ( !dir || *dir=='\0' || stat(dir, &sb) != 0 )
		return -1;

	// cut-off initial "/dev/" from file-name 
	
	p = strchr(file + 1, '/');
	p = p ? p + 1 : (char *)file;
	
	// replace '/'s with '_'s in what remains (after making a copy) 
	p = cp = strdup(p);
	do 
	{ 
		if ( *p == '/' ) 
			*p = '_';
	}
	while(*p++);
	
	// build lockname 
	snprintf(g_lockname, sizeof(g_lockname), "%s/LCK..%s", dir, cp);
	
	// destroy the copy 
	free(cp);

	return 0;
}

/* ======================================================================
Function: uucp_lock
Purpose : create lock file
Input 	: -
Output	: -
Comments: Routine taken from picocom
===================================================================== */
int uucp_lock(void)
{
	int r, fd, pid;
	char buf[16];
	mode_t m;

	// alread a lock ?
	if ( g_lockname[0] == '\0' ) 
		return 0;

	fd = open(g_lockname, O_RDONLY);
	
	if ( fd >= 0 ) 
	{
		r = read(fd, buf, sizeof(buf)); 
		close(fd);
		
		// if r == 4, lock file is binary (old-style) 
		pid = (r == 4) ? *(int *)buf : strtol(buf, NULL, 10);
		if ( pid > 0 && kill((pid_t)pid, 0) < 0 && errno == ESRCH ) 
		{
			// stale lock file
			fprintf(stdout, "Removing stale lock: %s\n", g_lockname);
			sleep(1);
			unlink(g_lockname);
		}
		else
		{
			g_lockname[0] = '\0';
			errno = EEXIST;
			return -1;
		}
	}
	
	// lock it 
	m = umask(022);
	fd = open(g_lockname, O_WRONLY|O_CREAT|O_EXCL, 0666);
	if ( fd < 0 ) 
	{ 
		g_lockname[0] = '\0'; 
		return -1; 
	}
	
	umask(m);
	snprintf(buf, sizeof(buf), "%04d\n", getpid());
	write(fd, buf, strlen(buf));
	close(fd);

	return 0;
}

/* ======================================================================
Function: uucp_unlock
Purpose : unlock open lock
Input 	: -
Output	: -
Comments: Routine taken from picocom
===================================================================== */
int uucp_unlock(void)
{
	if ( g_lockname[0] ) 
		unlink(g_lockname);
	return 0;
}


#ifdef USE_MYSQL
/* ======================================================================
Function: db_open
Purpose : open database connexion
Input 	: pointer to mysql structure
Output	: EXIT_SUCCESS if ok else EXIT_FAILURE
Comments: -
===================================================================== */
int db_open( MYSQL * pmysql) 
{
	MYSQL *mysql_connection;

	// Open MySQL Database and read timestamp of the last record written
	if(!mysql_init(pmysql))
	{
		log_syslog(stderr, "Cannot initialize MySQL");
		return(EXIT_FAILURE);
	}
	
	// connect to database
	mysql_connection = mysql_real_connect(pmysql, opts.server, opts.user, opts.password, opts.database, 0, NULL, 0);

	if(mysql_connection == NULL)
	{
		log_syslog(stderr, "%d: %s \n", 	mysql_errno(pmysql), mysql_error(pmysql));
		return(EXIT_FAILURE);
	}

	return (EXIT_SUCCESS);
	
}

/* ======================================================================
Function: db_close
Purpose : close database connexion
Input 	: pointer to mysql structure
Output	: -
Comments: -
===================================================================== */
void db_close( MYSQL * pmysql) 
{
	// close MySQL Database 
	mysql_close(pmysql);
}
#endif

/* ======================================================================
Function: isr_handler
Purpose : Interrupt routine Code for signal
Input 	: -
Output	: -
Comments: 
====================================================================== */
void isr_handler (int signum)
{
	// Does we received CTRL-C ?
	if ( signum==SIGINT || signum==SIGTERM)
	{
		// Indicate we want to quit
		g_exit_pgm = true;
		
		log_syslog(NULL, "Received SIGINT/SIGTERM");
	}
	// Our receive buffer is full
	else if (signum == SIGIO)
	{
		log_syslog(NULL, "Received SIGIO");
	
	}
	
}

/* ======================================================================
Function: tlf_init_serial
Purpose : initialize serial port for receiving teleinfo
Input 	: -
Output	: Serial Port Handle
Comments: -
====================================================================== */
int tlf_init_serial(void)
{
	int tty_fd, r ;
	struct termios  termios ;
	
  // Ouverture de la liaison serie (Nouvelle version de config.)
  if ( (tty_fd = open(opts.port, O_RDONLY | O_NOCTTY)) == -1 ) 
  	fatal( "tlf_init_serial %s: %s", opts.port, strerror(errno));
  else
		log_syslog( stdout, "'%s' opened.\n",opts.port);
 
 	// lock serial if n
	if ( !opts.nolock )
	{
		uucp_lockname(UUCP_LOCK_DIR, opts.port);
		
		if ( uucp_lock() < 0 )
			fatal("cannot lock %s: %s", opts.port, strerror(errno));
	}
	
	// Get current parameters.
  if (  (r = tcgetattr(tty_fd, &g_oldtermios)) < 0 )
		log_syslog(stderr, "cannot get current parameters %s: %s",  opts.port, strerror(errno));
		
	// clear struct
	bzero(&termios, sizeof(termios)); 

	//if ( (r = cfsetispeed(&termios, B1200)) < 0 ) 
	//	fatal("cannot set term input speed %s: %s", opts.port, strerror(errno));

	//if ( (r = cfsetospeed(&termios, B1200)) < 0 ) 
	//	fatal("cannot set term output speed %s: %s", opts.port, strerror(errno));
		
	// Even Parity with 7 bits data
	termios.c_cflag = (B1200 | PARENB | CS7 | CLOCAL | CREAD) ;
	//termios.c_iflag = IGNPAR ;
	termios.c_iflag =	(IGNPAR | INPCK | ISTRIP) ; ;
	termios.c_oflag = 0;	// Pas de mode de sortie particulier (mode raw).
	termios.c_lflag = 0;	// non canonique
	termios.c_cc[VTIME] = 0;     /* inter-character timer unused */
	termios.c_cc[VMIN]  = 1;     /* blocking read until 1 character arrives */

	tcflush(tty_fd, TCIFLUSH);

  if ( ( r = tcsetattr(tty_fd,TCSANOW,&termios) < 0 ) )
		log_syslog(stderr, "cannot get current parameters %s: %s",  opts.port, strerror(errno));
 
 	//enable I/O port controls
	// iopl(3);
 
 	//get line bits for serial port
//	ioctl(tty_fd, TIOCMGET, &flags);

	//make sure RTS and DTR lines are high
//	flags &= ~TIOCM_RTS;
//	flags &= ~TIOCM_DTR;
//	ioctl(tty_fd, TIOCMSET, &flags);

	// Sleep 500ms
	usleep(500000);

 	return tty_fd ;
}

/* ======================================================================
Function: tlf_close_serial
Purpose : close serial port for receiving teleinfo
Input 	: Serial Port Handle
Output	: -
Comments: 
====================================================================== */
void tlf_close_serial(int device)
{
  if (device)
  {	
		int r;
	
	  	// Clean up
		if ( (r = tcflush(device, TCIOFLUSH)) < 0 ) 
			fatal("cannot flush before quit %s: %s", opts.port, strerror(errno));
		
		// restore old settings
		tcsetattr(device,TCSANOW,&g_oldtermios);
	  	
		close(device) ;
	}
}

/* ======================================================================
Function: tlf_checksum_ok
Purpose : check if teleinfo frame checksum is correct
Input 	: -
Output	: true or false
Comments: 
====================================================================== */
int tlf_checksum_ok(char *etiquette, char *valeur, char checksum) 
{
	int i ;
	unsigned char sum = 32 ;		// Somme des codes ASCII du message + un espace

	for (i=0; i < strlen(etiquette); i++) 
		sum = sum + etiquette[i] ;
		
	for (i=0; i < strlen(valeur); i++) 
		sum = sum + valeur[i] ;
		
	sum = (sum & 63) + 32 ;

	// Return 1 si checkum ok.
	if ( sum == checksum) 
		return ( true ) ;	

	return ( false ) ;
}


/* ======================================================================
Function: tlf_check_frame
Purpose : check for teleinfo frame
Input 	: -
Output	: the length of valid buffer, 0 otherwhile
Comments: 
====================================================================== */
int tlf_check_frame( char * pframe) 
{
	char * pstart; 
	char * pend; 
	char * pnext; 
	char * ptok; 
	char * pvalue; 
	char * pcheck;
	char	buff[TELEINFO_BUFSIZE];

#ifdef USE_MYSQL
	MYSQL  mysql; 
	static char mysql_field[ 512];
	static char mysql_value[ 512];
	static char mysql_job[1024];
#endif
	int i, frame_err , len;

	i=0;
	len = strlen(pframe); 
	
	strncpy( buff, pframe, len+1);

	pstart = &buff[0];
	pend   = pstart + len;

	if (opts.verbose)
	{
		fprintf(stdout, "------------------- Received %d char Frame.%s\n-------------------", len, buff);
		fflush(stdout);
	}	
	

#ifdef USE_MYSQL	
	// need to put in mysql
	if (opts.mode == MODE_MYSQL)
	{
		// First SQL Fields, use date time from mysql server using NOW()
		strcpy(mysql_field, "DATE");
		strcpy(mysql_value, "NOW()");
	}
#endif

	// just one vefification, buffer should be at least 100 Char
	if ( pstart && pend && (pend > pstart+100 ) )
	{
		//fprintf(stdout, "Frame to analyse [%d]%s\n", pend-pstart, pstart);
		//fflush(stdout);	

		// by default no error
		frame_err = false;
		
		//log_syslog(stderr, "Found %d Bytes Frame\n", pend-pstart);

		// ignore STX
		pstart++;

		// Init our pointers
		ptok = pvalue = pnext = pcheck = NULL;
		

		// Loop in buffer	
		while ( pstart < pend )
		{
			// start of token
			if ( *pstart=='\n' )
			{		
				// Position on token				
				ptok = ++pstart;
			}						

			// start of token value
			if ( *pstart==' ' && ptok)
			{						
				// Isolate token name
				*pstart++ = '\0';

				// no value yet, setit ?
				if (!pvalue)
					pvalue = pstart;
				// we had value, so it's checksum
				else
					pcheck = pstart;
			}						

			// new line ? ok we got all we need ?
			if ( *pstart=='\r' )
			{						
				
				*pstart='\0';

				// Good format ?
				if ( ptok && pvalue && pcheck )
				{
					// Checksum OK
					if ( tlf_checksum_ok(ptok, pvalue, *pcheck))
					{
						//fprintf(stdout, "%s=%s\n",ptok, pvalue);

					   #ifdef USE_MYSQL						

						// need to send to my sql ?
						if (opts.mode == MODE_MYSQL )
						{
							strcat(mysql_field, ",");
							strcat(mysql_field, ptok);
							
							// Compteur Monophasé IINST et IMAX doivent être reliés à
							// IINST1 et IMAX1 dans la base
							if (strcmp(ptok, "IINST")==0 || strcmp(ptok, "IMAX")==0)
								strcat(mysql_field, "1");

							strcat(mysql_value, ",'");
							strcat(mysql_value, pvalue);
							strcat(mysql_value, "'");
						}
					   #endif
					}
					else
					{
						frame_err = true;
						//fprintf(stdout, "%s=%s BAD",ptok, pvalue);
						log_syslog(stderr, "tlf_checksum_ok('%s','%s','%c') error\n",ptok, pvalue, *pcheck);
						
					}
				}
				else
				{
					frame_err = true;
					log_syslog(stderr, "tlf_check_frame() no correct frame '%s'\n",ptok);
				}
				
				// reset data
				ptok = pvalue = pnext = pcheck = NULL;

			}						
			
			// next
			pstart++;
	
		}

		// no error in this frame ?
		if (!frame_err)
		{
			if (opts.verbose)
				fprintf(stdout, "Frame OK\n", len);
#ifdef USE_MYSQL
			if (opts.mode == MODE_MYSQL)
			{
				MYSQL mysql;
				
				// Ecrit données dans base MySql.
				sprintf(mysql_job, "INSERT INTO %s\n  (%s)\nVALUES\n  (%s);\n", opts.table, mysql_field, mysql_value);
				
				//if (opts.verbose)
					//printf(mysql_job);
				
				if (opts.verbose)
					fprintf(stdout, "%s", mysql_job);	

 				if ( db_open(&mysql) != EXIT_SUCCESS )
					fatal("%d: %s \n", mysql_errno(&mysql), mysql_error(&mysql));
					
					// prepare SQL query
				if (mysql_query(&mysql,mysql_job))
					fatal("%d: %s \n", mysql_errno(&mysql), mysql_error(&mysql));

	 			db_close(&mysql);
			}
#endif

			//debug_dump_sensors(0);
			return (len);
		}
	}	
	else
	{
		log_syslog(stderr, "tlf_check_frame() No correct frame found\n");
	}

	return (0);

}

/* ======================================================================
Function: tlf_get_frame
Purpose : check for teleinfo frame
Input 	: true if we need to wait for frame, false if async (take if any)
Output	: true if frame ok, else false
Comments: 
====================================================================== */
int tlf_get_frame(char block) 
{
	struct sockaddr_in tlf_from;
	int fromlen = sizeof(tlf_from);
	int n = 0;
	int timeout = 100; // (10 * 100ms)
	int ret  =false;
	char	rcv_buff[TELEINFO_BUFSIZE];

	// clear ou receive  buffer
	memset(rcv_buff,0, TELEINFO_BUFSIZE );

	// do until received or timed out
	while (n<=0 && timeout--)
	{
		// read data received on socket ?
		n = recvfrom(g_tlf_sock,rcv_buff,TELEINFO_BUFSIZE,0, (struct sockaddr *)&tlf_from,(socklen_t *)&fromlen);

		// Do we received frame on socket ?
		if (n > 0) 
		{
			//log_syslog( stderr, "recvfrom %d buffer='%s'\n",n, rcv_buff);
			// check the frame and do stuff
			ret = tlf_check_frame( rcv_buff );
		}
		else 
		{
			// want to wait frame ?
			if ( block)
			{
				// Letting time to the Operating system doing other jobs
				// Wait 100ms it won't bother us
				usleep(100000);
			}
			else
			{
				break;
			}
		}
	}
	
	// check for timed out
	if (block && timeout<=0)
			log_syslog( stderr, "tlf_get_frame() Time-Out Expired\n");
		
	return (ret);
}

/* ======================================================================
Function: usage
Purpose : display usage
Input 	: program name
Output	: -
Comments: 
====================================================================== */
int usage( char * name)
{

	printf("%s\n", PRG_NAME );
	printf("Usage is: %s --mode s|r|y [options] <tty device>\n", PRG_NAME);
	printf("  --<m>mode  :  s (=send) | r (=receive) | y (=mysql) | t (=test)\n");
	printf("                send continuiously the teleinfo frame to network\n");
	printf("                receive one teleinfo frame from network and display it\n");
	printf("                receive one teleinfo frame from network and send to mysql database\n");
	printf("                test : display teleinfo received from serial\n");
	printf("Options are:\n");
	printf("  --no<l>ock   : do not create serial lock file\n");
	printf("  --<v>erbose  : speak more to user\n");
	printf("  --<p>ort n   : send/listen on network port n and (n+1) on send mode\n");
	printf("  --<d>aemon   : daemonize the process (only possible with mode send\n");
#ifdef USE_MYSQL
	printf("  --<u>ser     : mysql user login\n");
	printf("  --pass<w>ord : mysql user password\n");
	printf("  --<s>erver   : mysql server\n");
	printf("  --data<b>ase : mysql database\n");
	printf("  --<t>able    : mysql table\n");
#endif
	printf("  --<h>elp\n");
	printf("<?> indicates the equivalent short option.\n");
	printf("Short options are prefixed by \"-\" instead of by \"--\".\n");
	printf("Example :\n");
	printf( "teleinfo -m s -d /dev/teleinfo\nstart teleinfo as a daemon to continuously send over the network the frame received on port /dev/teleinfo\n\n");
	printf( "teleinfo -m r -v\nstart teleinfo to wait for a network frame, then display it and exit\n");
#ifdef USE_MYSQL
	printf( "teleinfo -m y -v\nstart teleinfo to wait for a network frame, then dispay SQL Query and execute it and exit\n");
#endif
}


/* ======================================================================
Function: parse_args
Purpose : parse argument passed to the program
Input 	: -
Output	: -
Comments: 
====================================================================== */
void parse_args(int argc, char *argv[])
{
	static struct option longOptions[] =
	{
		{"nolock", 	no_argument, 			0, 'l'},
		{"verbose", no_argument,	  	0, 'v'},
		{"port", 		required_argument,0, 'p'},
		{"mode", 		required_argument,0, 'm'},
		{"daemon", 	no_argument, 			0, 'd'},
#ifdef USE_MYSQL
		{"user", 	  required_argument,0, 'u'},
		{"password",required_argument,0, 'w'},
		{"server"	 ,required_argument,0, 's'},
		{"database",required_argument,0, 'b'},
		{"table"	 ,required_argument,0, 't'},
#endif
		{"help", 		no_argument, 			0, 'h'},
		{0, 0, 0, 0}
	};

	int optionIndex = 0;
	int c;

	while (1) 
	{

		/* no default error messages printed. */
		opterr = 0;

#ifdef USE_MYSQL
		c = getopt_long(argc, argv, "lvm:hp:du:w:s:b:t:",	longOptions, &optionIndex);
#else
                c = getopt_long(argc, argv, "lvm:hp:d",       longOptions, &optionIndex);
#endif

		if (c < 0)
			break;

		switch (c) 
		{
			case 'l':
				opts.nolock = true;
			break;

			case 'v':
				opts.verbose = true;
			break;

			case 'd':
				opts.daemon = true;
			break;

			case 'm':
				switch (optarg[0]) 
				{
					case 's':
					case 'S':
						opts.mode = MODE_SEND;
						opts.mode_str = "send";
					break;
					case 't':
					case 'T':
						opts.mode = MODE_TEST;
						opts.mode_str = "test";
					break;
					case 'r':
					case 'R':
						opts.mode = MODE_RECEIVE;
						opts.mode_str = "receive";
					break;
#ifdef USE_MYSQL
					case 'y':
					case 'Y':
						opts.mode = MODE_MYSQL;
						opts.mode_str = "mysql";
					break;
#endif
					default:
						fprintf(stderr, "--mode '%c' ignored.\n", optarg[0]);
						fprintf(stderr, "--mode can be one off: 's', 'r', or 'y'\n");
					break;
				}
			break;

			case 'p':
				opts.netport = (int) atoi(optarg);
				
				if (opts.netport < 1024 || opts.netport > 65534)
				{
						fprintf(stderr, "--port '%d' ignored.\n", opts.netport);
						fprintf(stderr, "--port must be between 1024 and 65534\n");
						opts.netport = TELEINFO_PORT;
				}
			break;
#ifdef USE_MYSQL

			case 's':
				strcpy(opts.server, optarg );
			break;

			case 'b':
				strcpy(opts.database, optarg );
			break;

			case 'u':
				strcpy(opts.user, optarg );
			break;

			case 'w':
				strcpy(opts.password, optarg);
			break;
				
			case 't':
				strcpy(opts.table, optarg );
			break;
#endif

			case 'h':
				usage(argv[0]);
				exit(EXIT_SUCCESS);
			case '?':
			default:
				fprintf(stderr, "Unrecognized option.\n");
				fprintf(stderr, "Run with '--help'.\n");
				exit(EXIT_FAILURE);
		}
	} /* while */

	if ( opts.mode == MODE_SEND )
	{ 
		if ( (argc - optind) < 1 && opts.mode == MODE_SEND ) 
		{
			fprintf(stderr, "No port given\n");
			exit(EXIT_FAILURE);
		}
		
		strncpy(opts.port, argv[optind], sizeof(opts.port) - 1);
		opts.port[sizeof(opts.port) - 1] = '\0';
	}
	
	if (opts.daemon && opts.mode != MODE_SEND)
	{
			fprintf(stderr, "--daemon ignored.\n");
			fprintf(stderr, "--daemon must be used only in mode send\n");
			opts.daemon = false;
	}

	
	printf("teleinfo v%s\n", PRG_VERSION);
	
	if (opts.verbose)
	{
		printf("-- Serial Stuff -- \n");
		printf("port is        : %s\n", opts.port);
		printf("flowcontrol    : %s\n", opts.flow_str);
		printf("baudrate is    : %d\n", opts.baud);
		printf("parity is      : %s\n", opts.parity_str);
		printf("databits are   : %d\n", opts.databits);
#ifdef USE_MYSQL
		printf("-- MySql Stuff -- \n");
		printf("Server is      : %s\n", opts.server);
		printf("Database is    : %s\n", opts.database);
		printf("Login is       : %s\n", opts.user);
		printf("Password is    : %s\n", opts.password);
		printf("Table is       : %s\n", opts.table);
#endif
		printf("-- Other Stuff -- \n");
		printf("udp port is    : %d\n", opts.netport);
		printf("mode is        : %s\n", opts.mode_str);
		printf("nolock is      : %s\n", opts.nolock ? "yes" : "no");
		printf("verbose is     : %s\n", opts.verbose? "yes" : "no");
		printf("\n");
	}	
}

/* ======================================================================
Function: main
Purpose : Main entry Point
Input 	: -
Output	: -
Comments: 
====================================================================== */
int main(int argc, char **argv)
{
	struct sockaddr_in server,client;
	struct sigaction exit_action;
 	fd_set rdset, wrset;
	int err_no;  
	int key;
	int mode;
	int length, flags;
	int broadcast; 
  int r, n;
	unsigned char c;
	char	rcv_buff[TELEINFO_BUFSIZE];
	int		rcv_idx;
  char time_str[200];
  time_t t;
  struct tm *tmp;

	rcv_idx = 0;
	g_fd_teleinfo = 0; 
	g_tlf_sock = 0;
	g_exit_pgm = false;
	
	bzero(rcv_buff, TELEINFO_BUFSIZE);

	parse_args(argc, argv);

	// Set up the structure to specify the exit action.
	exit_action.sa_handler = isr_handler;
	sigemptyset (&exit_action.sa_mask);
	exit_action.sa_flags = 0;
	sigaction (SIGTERM, &exit_action, NULL);
	sigaction (SIGINT, &exit_action, NULL); 

	// Init Sockets
	g_tlf_sock=socket(AF_INET, SOCK_DGRAM, 0);
	
	if (g_tlf_sock < 0) 
		fatal( "Error Opening Socket %d: %s\n",g_tlf_sock, strerror (errno));
	else
	{
		if (opts.verbose)
			log_syslog(stderr, "Opened Socket\n");
	}

	if (opts.mode == MODE_RECEIVE || opts.mode == MODE_MYSQL )
	{
		flags = fcntl(g_tlf_sock,F_GETFL,0);
		fcntl(g_tlf_sock, F_SETFL, flags | O_NONBLOCK);

		length = sizeof(server);
		bzero(&server,length);
		
		server.sin_family=AF_INET;
		server.sin_addr.s_addr=INADDR_ANY;
		server.sin_port=htons(opts.netport);
		
		if ( bind(g_tlf_sock,(struct sockaddr *)&server,length) < 0 ) 
			fatal("Error Binding Socket %d : %s\n", g_tlf_sock, strerror (errno));
		else
		{
			if (opts.verbose)
				log_syslog(stderr, "Binded on port %d\n",opts.netport);
		}

			
		if (opts.verbose)
		 	log_syslog(stdout, "Inits succeded, waiting network frame\n");
	 	
	 	//while (!g_exit_pgm)
		length =  tlf_get_frame(true);
		
		if (!length)
			clean_exit( EXIT_FAILURE );
		else	 	
			clean_exit( EXIT_SUCCESS );
	}

	// Send mode need to broadcast serial frame, test mode just display
	if ( opts.mode == MODE_SEND || opts.mode == MODE_TEST )
	{
		g_fd_teleinfo = tlf_init_serial();

			// Test mode to need send frame
		if ( opts.mode == MODE_SEND )
		{
			broadcast = 1; //might need '1' and char type
			
			if (setsockopt(g_tlf_sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) == -1)
				log_syslog(stderr, "Could not allow broadcasting\n");
			else 
				log_syslog(stderr, "Broadcast routing enabled\n");
		
			client.sin_family = AF_INET;     // host byte order
			client.sin_port = htons(opts.netport); 
			client.sin_addr.s_addr = inet_addr(TELEINFO_NETWORK);
			memset(client.sin_zero, '\0', sizeof (client.sin_zero));
		}
 		
	}
	
 	log_syslog(stdout, "Inits succeded, entering Main loop\n");
 	
  if (opts.daemon)
  {
	 	log_syslog(stdout, "Starting as a daemon\n");
  	daemonize();
  }

	// Do while not end
	while ( ! g_exit_pgm ) 
	{
		// Read from serial port
		n = read(g_fd_teleinfo, &c, 1);
	
		if (n == 0)
			fatal("nothing to read");
		else if (errno == EINTR  )
			break;
		else if ( n < 0 )
			fatal("read failed: %s", strerror(errno));

  	//log_syslog(stdout, "%c", c);


		// What we received ?
		switch (c)
		{
			// start of frame ???
			case  STX:
				// Clear buffer, begin to store in it
				rcv_idx = 0;
				bzero(rcv_buff, TELEINFO_BUFSIZE);
				rcv_buff[rcv_idx++]=c;
			break;
				
			// End of frame ?
			case  ETX:
				// We had STX ?
				if ( rcv_idx )
				{
					// Store in buffer and proceed
					rcv_buff[rcv_idx++]=c;
					
					// clear the end of buffer (paranoia inside)
					bzero(&rcv_buff[rcv_idx], TELEINFO_BUFSIZE-rcv_idx);

					// need to send frame, no check, it will be done on the other side
					// by the receive mode of this program
					if (opts.mode == MODE_SEND)
					{
						if (opts.verbose)
							log_syslog(stderr, "Sending frame size %d to network\n", rcv_idx);
							
						sendto(g_tlf_sock, rcv_buff, rcv_idx, 0, (struct sockaddr *) &client,  sizeof(struct sockaddr));
			  		client.sin_port = htons(opts.netport+1); 
						sendto(g_tlf_sock, rcv_buff, rcv_idx, 0, (struct sockaddr *) &client,  sizeof(struct sockaddr));
			 			client.sin_port = htons(opts.netport); 
					}
					else
					{
						// If frame  ok
						if ( (length = tlf_check_frame(rcv_buff)) > 0 )
						{
							t = time(NULL);
							tmp = localtime(&t);
							if (tmp) 
							{
								if (strftime(time_str, sizeof(time_str), "%a, %d %b %Y %T" , tmp) == 0) 
								{
									strcpy( time_str, "No Time");
								}
							}

							// good full frame received, do whatever you want here
							fprintf(stdout, "==========================\nTeleinfo Frame of %d char\n%s\n==========================%s\n",
															strlen(rcv_buff), time_str, rcv_buff );

							// ..
							// ..
							// ..
						}
					}
				}
				// May be begin of the program or other problem.
				else
				{
					rcv_idx = 0;
					bzero(rcv_buff, TELEINFO_BUFSIZE);
				}
			break;
			
			// other char ?
			default:
			{
				// If we are in a frame, store data recceived
				if (rcv_idx)
				{
					// If buffer is not full
					if ( rcv_idx < TELEINFO_BUFSIZE)
					{
						// Store data recceived
						rcv_buff[rcv_idx++]=c;
					}
					else
					{
						// clear buffer & restart
						rcv_idx=0;
						bzero(rcv_buff, TELEINFO_BUFSIZE);
					}
				}
			}
			break;
		}
		
		// We want to display results ?
		if (opts.verbose)
		{
			if ( (n = write(STDOUT_FILENO, &c, 1)) <= 0 )
				fatal("write to stdout failed: %s", strerror(errno));
		}
	}
	

  log_syslog(stderr, "Program terminated\n");
  
  clean_exit(EXIT_SUCCESS);
  
  // avoid compiler warning
  return (0);
}
