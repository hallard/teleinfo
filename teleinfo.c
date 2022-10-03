/* ======================================================================
Program : teleinfo
Version : 1.0.8
Purpose : send/recevice teleinformation from severals devices then can 
          - write to MySql
          - write to Emoncms
          - send UDP frame over network

Author  : (c) Charles-Henri Hallard
          http://hallard.me
Comments: some code grabbed from picocom and other from teleinfo
  : You can use or distribute this code unless you leave this comment
  : too see this code correctly indented, please use Tab values of 2
  06/09/2013 : Added EMONCMS feature
  12/09/2013 : Added Linked List for only post real time changed values
  30/03/2014 : Added Emoncms real time post (only changed values)
  15/04/2014 : Added configuration parameters with also config file
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
#ifdef USE_EMONCMS
#include <curl/curl.h>
#endif

#include <netinet/in.h>
#include <getopt.h>

// ----------------
// Constants
// ----------------
#define true 1
#define false 0

// Directory where we are when in daemon mode
#define PRG_DIR             "/usr/local/bin" 
#define PRG_NAME            "teleinfo"
#define PRG_VERSION_NUMBER  "1.0.8"
#define PRG_CFG             "/etc/teleinfo.conf"

#if (defined USE_EMONCS && defined USE_MYSQL)
  #define PRG_VERSION    PRG_VERSION_NUMBER " (with mysql and emoncms)"
#elseif defined USE_EMONCS
  #define PRG_VERSION    PRG_VERSION_NUMBER " (with emoncms)"
#elseif defined USE_MYSQL
  #define PRG_VERSION    PRG_VERSION_NUMBER " (with mysql)"
#else
  #define PRG_VERSION    PRG_VERSION_NUMBER 
#endif

// Define teleinfo mode, device or network
#define TELEINFO_DEVICE   ""
#define TELEINFO_PORT     1200          // Port used to send frame over Network
#define TELEINFO_NETWORK  "10.10.0.0"   // Broadcast or specific IP where to send frame

#define UUCP_LOCK_DIR "/var/lock"

// Local File for teleinfo
//#define TELEINFO_DATALOG  "/tmp/teleinfo.log"
//#define TELEINFO_TRAMELOG "/tmp/teleinfotrame."
#define TELEINFO_BUFSIZE  512

// Teleinfo start and end of frame characters
#define STX 0x02
#define ETX 0x03 

#ifdef USE_MYSQL
  // Define mysql
  #define MYSQL_HOST  "localhost"
  #define MYSQL_LOGIN ""
  #define MYSQL_PASS  ""
  #define MYSQL_DB    ""
  #define MYSQL_TABLE "DbiTeleinfo"
  #define MYSQL_PORT  3306
#endif

#ifdef USE_EMONCMS
  // Define emoncms
  #define EMONCMS_URL    ""
  #define EMONCMS_APIKEY ""
  #define EMONCMS_NODE   0
  
  #define HTTP_APIKEY_SIZE  64    // Max size of apikey
  #define HTTP_URL_SIZE     128   // Max size of url (url only, not containing posted data)
  #define HTTP_BUFFER_SIZE  1024  // Where http returned data will be filled
#endif

// Some enum
enum parity_e     {  P_NONE,  P_EVEN,    P_ODD };
enum flowcntrl_e  { FC_NONE,  FC_RTSCTS, FC_XONXOFF };
enum mode_e       { MODE_NONE, MODE_SEND,   MODE_RECEIVE, MODE_TEST };
enum value_e      { VALUE_NOTHING, VALUE_ADDED, VALUE_EXIST, VALUE_CHANGED};

// Configuration structure
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
  char value_str[32];
  char network[32];
#ifdef USE_MYSQL
  int  mysql;
  char server[32];
  char user[32];
  char password[32];
  char database[32];
  char table[32];
  unsigned int serverport;
#endif
#ifdef USE_EMONCMS
  int emoncms;
  int node;
  char url[HTTP_URL_SIZE];
  char apikey[HTTP_APIKEY_SIZE];
#endif
  int daemon;
// Configuration structure defaults values
} opts = {
  .port = "",
  .baud = 1200,
  .flow = FC_NONE,
  .flow_str = "none",
  .parity = P_EVEN,
  .parity_str = "even",
  .databits = 7,
  .nolock = false,
  .mode = MODE_NONE,
  .mode_str = "receive",
  .netport = TELEINFO_PORT,
  .verbose = false,
  .value_str = "",
  .network = TELEINFO_NETWORK,
#ifdef USE_MYSQL
  .mysql = false,
  .server = MYSQL_HOST,
  .user = MYSQL_LOGIN,
  .password = MYSQL_PASS,
  .database = MYSQL_DB,
  .table = MYSQL_TABLE,
  .serverport = MYSQL_PORT, 
#endif
#ifdef USE_EMONCMS
  .emoncms = false,
  .url = EMONCMS_URL,
  .apikey = EMONCMS_APIKEY,
  .node = EMONCMS_NODE,
#endif
  .daemon = false
};


// Statistics and error structure
struct 
{
  unsigned long framesent;
  unsigned long frame;
  unsigned long badchecksum;
  unsigned long frameformaterror;
  unsigned long frameok;
  unsigned long framesizeerror;
#ifdef USE_MYSQL
  unsigned long mysqlinitok;
  unsigned long mysqliniterror;
  unsigned long mysqlconnectok;
  unsigned long mysqlconnecterror;
  unsigned long mysqlqueryerror;
  unsigned long mysqlqueryok;
#endif
#ifdef USE_EMONCMS
  unsigned long curl_post;
  unsigned long curl_postok;
  unsigned long curl_posterror;
  unsigned long curl_timeout;
#endif
} stats;

// Linked list structure containing all values
// Used to only update changed values to emoncms
typedef struct _ValueList ValueList;
struct _ValueList 
{
  ValueList *next;
  char  *name;  // LABEL of value name
  char  *value; // value 
};

// ======================================================================
// Global vars 
// ======================================================================
int   g_fd_teleinfo;          // teleinfo serial handle
struct termios g_oldtermios ; // old serial config
int   g_tlf_sock;             // teleinfo socket 
int   g_exit_pgm;             // indicate en of the program
char  g_lockname[256] = "";   // Lock filename
#ifdef USE_EMONCMS
  CURL *g_pcurl;
  char  http_buffer[HTTP_BUFFER_SIZE];  // Where http returned data will be filled
#endif

ValueList g_valueslist;   // Linked list of teleinfo values
ValueList *p_valueslist;  // pointer on linked list of teleinfo values;

// ======================================================================
// some func declaration
// ======================================================================
void tlf_close_serial(int);

/* ======================================================================
Function: log_syslog
Purpose : write event to syslog
Input   : stream to write if needed
          string to write in printf format
          printf other arguments
Output  : -
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
  syslog(LOG_INFO, "%s", tmpbuff);
  closelog();
  
  // stream passed ? write also to it
  if (stream && opts.verbose && !opts.daemon ) 
  {
    fprintf(stream, "%s", tmpbuff);
    //fprintf(stream, "\n");
    fflush(stream);
  }
}

/* ======================================================================
Function: show_stats
Purpose : display program statistics 
Input   : -
Output  : -
Comments: 
====================================================================== */
void show_stats(void)
{
  // Print stats
  int old_opt = opts.daemon;
  int old_verb = opts.verbose;
  
  // Fake daemon/verbose mode to display info
  // We'll restore it after
  opts.daemon = false;
  opts.verbose = true;
  
  log_syslog(stderr, "\n"PRG_NAME" "PRG_VERSION_NUMBER" Statistics\n");
  log_syslog(stderr, "==========================\n");
  log_syslog(stderr, "Frames Sent         : %ld\n", stats.framesent);
  log_syslog(stderr, "Frames checked      : %ld\n", stats.frame);
  log_syslog(stderr, "Frames OK           : %ld\n", stats.frameok);
  log_syslog(stderr, "Checksum errors     : %ld\n", stats.badchecksum);
  log_syslog(stderr, "Frame format Errors : %ld\n", stats.frameformaterror);
  log_syslog(stderr, "Frame size Errors   : %ld\n", stats.framesizeerror);
#ifdef USE_MYSQL
  log_syslog(stderr, "MySQL init OK       : %ld\n", stats.mysqlinitok);
  log_syslog(stderr, "MySQL init errors   : %ld\n", stats.mysqliniterror);
  log_syslog(stderr, "MySQL connect OK    : %ld\n", stats.mysqlconnectok);
  log_syslog(stderr, "MySQL connect errors: %ld\n", stats.mysqlconnecterror);
  log_syslog(stderr, "MySQL queries OK    : %ld\n", stats.mysqlqueryok);
  log_syslog(stderr, "MySQL queries errors: %ld\n", stats.mysqlqueryerror);
#endif
#ifdef USE_EMONCMS
  log_syslog(stderr, "EmonCMS total post  : %ld\n", stats.curl_post);
  log_syslog(stderr, "EmonCMS post OK     : %ld\n", stats.curl_postok);
  log_syslog(stderr, "EmonCMS post errors : %ld\n", stats.curl_posterror);
  log_syslog(stderr, "EmonCMS timeout     : %ld\n", stats.curl_timeout);
#endif
  log_syslog(stderr, "--------------------------\n");
  
  opts.verbose = old_verb;
  opts.daemon  = old_opt;
}

/* ======================================================================
Function: clean_exit
Purpose : exit program 
Input   : exit code
Output  : -
Comments: 
====================================================================== */
void clean_exit (int exit_code)
{
  int r;

  // free up linked list
  valuelist_delete(p_valueslist);

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

#ifdef USE_EMONCMS  
  // cleanup if this part was initialized
  if (g_pcurl)
    curl_easy_cleanup(g_pcurl);
    
  // always cleanup this part
  curl_global_cleanup();
#endif

  // display statistics
  show_stats();
    
  if ( exit_code == EXIT_SUCCESS)
      log_syslog(stdout, "Succeded to do my job\n");
  else
      log_syslog(stdout, "Closing teleinfo due to error\n");
  
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
Input   : string to write in printf format
          printf other arguments
Output  : -
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
  syslog(LOG_INFO, "%s", tmpbuff);
  closelog();

  fprintf(stderr,"\r\nFATAL: %s \r\n", tmpbuff );
  fflush(stderr);

  clean_exit(EXIT_FAILURE);
}

/* ======================================================================
Function: daemonize
Purpose : daemonize the pocess
Input   : -
Output  : -
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
  
  // if verbose mode, allow display on stdout
  if (!opts.verbose)
    close(STDOUT_FILENO);
    
  // Always display errors on stderr
  //close(STDERR_FILENO);
}

/* ======================================================================
Function  : valuelist_add
Purpose   : Add element to the Linked List of values
Input     : Pointer to the label name
            pointer to the value
            size of the label
            size of the value
            state of the label (filled by function)
Output    :  pointer to the new node (or founded one)
          :  state of the label changed by the function
====================================================================== */
ValueList * valuelist_add (ValueList * me, char * name, char * value, int lgname, int lgvalue,  enum value_e  * valuestate)
{
  // Got one
  if (me)
  {
    // Create pointer on the new node
    ValueList *newNode = NULL;
    ValueList *parNode = NULL ;

    // By default we done nothing
    *valuestate = VALUE_NOTHING;

    // Loop thru the node
    while (me->next)
    {
      // save parent node
      parNode = me ;

      // go to next node
      me = me->next;

      // Check if we already have this LABEL
      if (strncmp(me->name, name, lgname) == 0)
      {
        // Already got also this value, return US
        if (strncmp(me->value, value, lgvalue) == 0)
        {
          *valuestate = VALUE_EXIST;
          return ( me );
        }
        else
        {
          // We changed the value
          *valuestate = VALUE_CHANGED;
          // Do we have enought space to hold new value ?
          if (strlen(me->value) >= lgvalue )
          {
            // Copy it
            strncpy(me->value, value , lgvalue );

            // That's all
            return (me);
          }
          else
          {
            // indicate parent node next following node instead of me
            parNode->next = me->next;

            // Return to parent (that will now point on next node and not us)
            me = parNode;

            // free up this node
            free (me);
          }
        }
      }
    }

    // Create new node with size to store strings
    if ((newNode = (ValueList  *) calloc(1, sizeof(ValueList) + lgname + 1 + lgvalue + 1 ) ) == NULL)
    {
      return ( (ValueList *) NULL );
    }
    else
    {
      // We not changed this node ?
      if (*valuestate != VALUE_CHANGED)
      {
        // so we added this node !
        *valuestate = VALUE_ADDED ;
      }
    }

    // Put the new node on the list
    me->next = newNode;

    // First String located after last struct element
    // Second String located after the First + \0
    newNode->name = (char *)  newNode + sizeof(ValueList);
    newNode->value = (char *) newNode->name + lgname + 1;

    // Copy the string
    memcpy(newNode->name , name  , lgname );
    memcpy(newNode->value, value , lgvalue );

    // return pointer on the new node
    return (newNode);
  }

  // Error or Already Exists
  return ( (ValueList *) NULL);
}

/* ======================================================================
Function: values_count
Purpose : Count the number of element in the values list
Input   : Pointer to the linked list
Output  : element numbers
====================================================================== */
int valuelist_count (ValueList * me)
{
  int count = 0;
  if (me)
    while ((me = me->next))
      count++;

  return (count);
}

/* ======================================================================
Function: valuelist_delete
Purpose : Delete a Linked List 
Input   : Pointer to the linked list
Output  : True if Ok False Otherwise
====================================================================== */
int valuelist_delete (ValueList * me)
{
  // Got a pointer
  if (me) 
  {
    ValueList *current;

    // For each linked list
    while ((current = me->next)) 
    {
      // Get the next
      me->next =  current->next;

      // Free the current
      free(current);
    }

    // Free the top element
    me->next = NULL ;

    // Ok
    return (true);
  }

  return (false);
}


/* ======================================================================
Function: uucp_lockname
Purpose : create the lock filename
Input   : 
Output  : -
Comments: Routine taken from picocom source code
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
Input   : -
Output  : -
Comments: Routine taken from picocom source code
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
Input   : -
Output  : -
Comments: Routine taken from picocom source code
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
Input   : pointer to mysql structure
Output  : EXIT_SUCCESS if ok else EXIT_FAILURE
Comments: -
===================================================================== */
int db_open( MYSQL * pmysql) 
{
  MYSQL *mysql_connection;

  // Open MySQL Database and read timestamp of the last record written
  if(!mysql_init(pmysql))
  {
    log_syslog(stderr, "Cannot initialize MySQL");
    stats.mysqliniterror++;
  }
  else
  {
    stats.mysqlinitok++;

    // connect to database
    mysql_connection = mysql_real_connect(pmysql, opts.server, opts.user, opts.password, opts.database, opts.serverport, NULL, 0);

    if(mysql_connection == NULL)
    {
      log_syslog(stderr, "%d: %s \n",   mysql_errno(pmysql), mysql_error(pmysql));
      stats.mysqlconnecterror++;
      return(EXIT_FAILURE);
    }
    else
    {
      stats.mysqlconnectok++;
    }
  }

  return (EXIT_SUCCESS);
}

/* ======================================================================
Function: db_close
Purpose : close database connexion
Input   : pointer to mysql structure
Output  : -
Comments: -
===================================================================== */
void db_close( MYSQL * pmysql) 
{
  // close MySQL Database 
  mysql_close(pmysql);
}
#endif

#ifdef USE_EMONCMS
/* ======================================================================
Function: http_write
Purpose : callback function when curl write return data
Input   : see curl API
Output  : -
Comments: -
===================================================================== */
size_t http_write(char *ptr, size_t size, size_t nmemb, void *userdata)
{
  // clean up our own receive buffer
  bzero(&http_buffer, HTTP_BUFFER_SIZE); 

  /* Copy curl's received data into our own buffer */
  if (size*nmemb < HTTP_BUFFER_SIZE - 1 )
  {
    memcpy(&http_buffer, ptr, size*nmemb);
    return (size*nmemb);
  }
  else
  {
    memcpy(&http_buffer, ptr, HTTP_BUFFER_SIZE - 1);
    return (HTTP_BUFFER_SIZE);
  }
}

/* ======================================================================
Function: http_post
Purpose : post data to emoncms
Input   : full url to post data
Output  : true if emoncms returned ok, false otherwise
Comments: we don't exit if not working, neither mind, take it next time
===================================================================== */
int http_post( char * str_url )
{
  CURLcode res;
  int retcode = false;

  // New post
  stats.curl_post++;

  // Set curl URL 
  if ( curl_easy_setopt(g_pcurl, CURLOPT_URL, str_url) != CURLE_OK )
    log_syslog(stderr, "Error while setting curl url %s : %s", str_url, curl_easy_strerror(res));
  else
  {
    // Perform the request, res will get the return code 
    if( (res = curl_easy_perform(g_pcurl)) != CURLE_OK)
    {
      log_syslog(stderr, "Error on http request %s : %s", str_url, curl_easy_strerror(res));
      stats.curl_posterror++;
      if (res == CURLE_OPERATION_TIMEDOUT)
        stats.curl_timeout++;
    }
    else
    { 
      // return data received 
      if (opts.verbose)
        log_syslog(stdout, "http_post %s ==> '%s'\n", str_url, http_buffer);  
        
      // emoncms returned string "ok", all went fine
      if (strcmp(http_buffer, "ok") == 0 )
      {
        retcode = true;
        stats.curl_postok++;
      }
    }
  }
  return retcode;
}
#endif

/* ======================================================================
Function: signal_handler
Purpose : Interrupt routine Code for signal
Input   : signal received
Output  : -
Comments: 
====================================================================== */
void signal_handler (int signum)
{
  // Does we received CTRL-C ?
  if ( signum==SIGINT )
  {
    // Indicate we want to quit
    g_exit_pgm = true;
    log_syslog(stdout, "\nReceived SIGINT\n");
  }
  else if ( signum==SIGTERM )
  {
    // Indicate we want to quit
    g_exit_pgm = true;
    log_syslog(stdout, "\nReceived SIGTERM\n");
  }
  else if (signum == SIGUSR1)
  {
    log_syslog(stdout, "\nReceived SIGUSR1");
    show_stats();
  }
  else if (signum == SIGHUP)
  {
    log_syslog(stdout, "\nReceived SIGHUP");
    // Reload configuration would be a good job;
    // To DO
  }
}

/* ======================================================================
Function: tlf_init_serial
Purpose : initialize serial port for receiving teleinfo
Input   : -
Output  : Serial Port Handle
Comments: -
====================================================================== */
int tlf_init_serial(void)
{
  int tty_fd, r ;
  struct termios  termios ;

  // Open serial device
  if ( (tty_fd = open(opts.port, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK)) < 0 ) 
    fatal( "tlf_init_serial %s: %s", opts.port, strerror(errno));
  else
    log_syslog( stdout, "'%s' opened.\n",opts.port);
    
  // Set descriptor status flags
  fcntl (tty_fd, F_SETFL, O_RDWR ) ;

  // lock serial  ?
  if ( !opts.nolock )
  {
    uucp_lockname(UUCP_LOCK_DIR, opts.port);
    
    if ( uucp_lock() < 0 )
      fatal("cannot lock %s: %s", opts.port, strerror(errno));
  }
  
  // Get current parameters for saving
  if (  (r = tcgetattr(tty_fd, &g_oldtermios)) < 0 )
    log_syslog(stderr, "cannot get current parameters %s: %s",  opts.port, strerror(errno));
    
  // copy current parameters and change for our own
  memcpy( &termios, &g_oldtermios, sizeof(termios)); 
  
  // raw mode
  cfmakeraw(&termios);
  
  // Set serial speed to 1200 bps
  if (cfsetospeed(&termios, B1200) < 0 || cfsetispeed(&termios, B1200) < 0 )
    log_syslog(stderr, "cannot set serial speed to 1200 bps: %s",  strerror(errno));
    
  // Parity Even
  termios.c_cflag &= ~PARODD;
  termios.c_cflag |= PARENB;    
  
  // 7 databits
  termios.c_cflag = (termios.c_cflag & ~CSIZE) | CS7;
  
  // No Flow Control
  termios.c_cflag &= ~(CRTSCTS);
  termios.c_iflag &= ~(IXON | IXOFF | IXANY);
  
  // Local
  termios.c_cflag |= CLOCAL;

  // No minimal char but 5 sec timeout
  termios.c_cc [VMIN]  =  0 ;
  termios.c_cc [VTIME] = 50 ; 

  // now setup the whole parameters
  if ( tcsetattr (tty_fd, TCSANOW | TCSAFLUSH, &termios) <0) 
    log_syslog(stderr, "cannot set current parameters %s: %s",  opts.port, strerror(errno));
    
  // Sleep 50ms
  // trust me don't forget this one, it will remove you some
  // headache to find why serial is not working
  usleep(50000);

  return tty_fd ;
}

/* ======================================================================
Function: tlf_close_serial
Purpose : close serial port for receiving teleinfo
Input   : Serial Port Handle
Output  : -
Comments: 
====================================================================== */
void tlf_close_serial(int device)
{
  if (device)
  { 
    // flush and restore old settings
    tcsetattr(device, TCSANOW | TCSAFLUSH, &g_oldtermios);
      
    close(device) ;
  }
}

/* ======================================================================
Function: tlf_checksum_ok
Purpose : check if teleinfo frame checksum is correct
Input   : -
Output  : true or false
Comments: 
====================================================================== */
int tlf_checksum_ok(char *etiquette, char *valeur, char checksum) 
{
  int i ;
  unsigned char sum = 32 ;    // Somme des codes ASCII du message + un espace

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
Function: tlf_treat_label
Purpose : do action when received a correct label / value + checksum line
Input   : plabel : pointer to string containing the label
        : pvalue : pointer to string containing the associated value
Output  : 
Comments: 
====================================================================== */
void tlf_treat_label( char * plabel, char * pvalue) 
{
  // emoncms need only numeric values
  if (opts.emoncms)
  {
    if (strcmp(plabel, "OPTARIF")==0 )
    {
      // L'option tarifaire choisie (Groupe "OPTARIF") est codée sur 4 caractères alphanumériques 
      /* J'ai pris un nombre arbitraire codé dans l'ordre ci-dessous
      je mets le 4eme char à 0, trop de possibilités
      BASE => Option Base. 
      HC.. => Option Heures Creuses. 
      EJP. => Option EJP. 
      BBRx => Option Tempo
      */
      pvalue[3] = '\0';
        
           if (strcmp(pvalue, "BAS")==0 ) strcpy (pvalue, "1");
      else if (strcmp(pvalue, "HC.")==0 ) strcpy (pvalue, "2");
      else if (strcmp(pvalue, "EJP")==0 ) strcpy (pvalue, "3");
      else if (strcmp(pvalue, "BBR")==0 ) strcpy (pvalue, "4");
      else strcpy (pvalue, "0");
    }
    else if (strcmp(plabel, "HHPHC")==0 )
    {
      // L'horaire heures pleines/heures creuses (Groupe "HHPHC") est codé par un caractère A à Y 
      // J'ai choisi de prendre son code ASCII
      int code = *pvalue;
      sprintf(pvalue, "%d", code);
    }
    else if (strcmp(plabel, "PTEC")==0 )
    {
      // La période tarifaire en cours (Groupe "PTEC"), est codée sur 4 caractères
      /* J'ai pris un nombre arbitraire codé dans l'ordre ci-dessous
      TH.. => Toutes les Heures. 
      HC.. => Heures Creuses. 
      HP.. => Heures Pleines. 
      HN.. => Heures Normales. 
      PM.. => Heures de Pointe Mobile. 
      HCJB => Heures Creuses Jours Bleus. 
      HCJW => Heures Creuses Jours Blancs (White). 
      HCJR => Heures Creuses Jours Rouges. 
      HPJB => Heures Pleines Jours Bleus. 
      HPJW => Heures Pleines Jours Blancs (White). 
      HPJR => Heures Pleines Jours Rouges. 
      */
           if (strcmp(pvalue, "TH..")==0 ) strcpy (pvalue, "1");
      else if (strcmp(pvalue, "HC..")==0 ) strcpy (pvalue, "2");
      else if (strcmp(pvalue, "HP..")==0 ) strcpy (pvalue, "3");
      else if (strcmp(pvalue, "HN..")==0 ) strcpy (pvalue, "4");
      else if (strcmp(pvalue, "PM..")==0 ) strcpy (pvalue, "5");
      else if (strcmp(pvalue, "HCJB")==0 ) strcpy (pvalue, "6");
      else if (strcmp(pvalue, "HCJW")==0 ) strcpy (pvalue, "7");
      else if (strcmp(pvalue, "HCJR")==0 ) strcpy (pvalue, "8");
      else if (strcmp(pvalue, "HPJB")==0 ) strcpy (pvalue, "9");
      else if (strcmp(pvalue, "HPJW")==0 ) strcpy (pvalue, "10");
      else if (strcmp(pvalue, "HPJR")==0 ) strcpy (pvalue, "11");
      else strcpy (pvalue, "0");
      
    }
  }
  
    // Do we need to get specific value ?
  if (*opts.value_str)
  {
    if (strcmp(plabel, opts.value_str)==0 )
    {
      //fprintf(stdout, "%s\n", pvalue);
    }
  }
  else
  {
    // Display current Power
    if (strcmp(plabel, "PAPP")==0 )
    {
      int power = atoi ( pvalue);
      //fprintf(stdout, "Power = %d W\n", power);
    }
    // Display Index
    if (strcmp(plabel, "HCHC")==0 || strcmp(plabel, "HCHP")==0)
    {
      long index = atol ( pvalue);
      //fprintf(stdout, "%s = %ld KWh\n", plabel, index);
    }
  }
}


/* ======================================================================
Function: tlf_check_frame
Purpose : check for teleinfo frame
Input   : -
Output  : the length of valid buffer, 0 otherwhile
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
  char  buff[TELEINFO_BUFSIZE];
  enum value_e  value_state;


#ifdef USE_MYSQL
  MYSQL  mysql; 
  static char mysql_field[ 512];
  static char mysql_value[ 512];
  static char mysql_job[1024];
#endif
#ifdef USE_EMONCMS
  static char emoncms_url[1024];
#endif

  int i, frame_err , len;
  
  // New frame
  stats.frame++;

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
  if (opts.mysql)
  {
    // First SQL Fields, use date time from mysql server using NOW()
    strcpy(mysql_field, "DATE");
    strcpy(mysql_value, "NOW()");
  }
#endif

#ifdef USE_EMONCMS
  // need to put in emoncms
  if (opts.emoncms)
  {
    // Prepare emoncms post
    if ( opts.node )
      sprintf(emoncms_url, "%s?node=%d&apikey=%s&json={", opts.url, opts.node, opts.apikey);
    else
      sprintf(emoncms_url, "%s?apikey=%s&json={", opts.url, opts.apikey);

    //if ( opts.verbose )
    //  log_syslog(stdout, "\n%s\n", emoncms_url);
  }
#endif

  // just one verification, buffer should be at least 100 Char
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
            
            // In case we need to do things
            tlf_treat_label(ptok, pvalue);
            
            // remove undefined values
+           if (strcmp(pvalue, "----") == 0)
+           {
+               strcpy (pvalue, "0");
+           }

            // Add value to linked lists of values
            valuelist_add(p_valueslist, ptok, pvalue, strlen(ptok), strlen(pvalue), &value_state);

             #ifdef USE_MYSQL           
            // need to send to my sql ?
            if (opts.mysql )
            {
              strcat(mysql_field, ",");
              strcat(mysql_field, ptok);
              
              // Compteur Monophasé IINST et IMAX doivent être reliés
              // IINST1 et IMAX1 dans la base
              if (strcmp(ptok, "IINST")==0 || strcmp(ptok, "IMAX")==0)
                strcat(mysql_field, "1");

              strcat(mysql_value, ",'");
              strcat(mysql_value, pvalue);
              strcat(mysql_value, "'");
            }
             #endif
             
             #ifdef USE_EMONCMS
            // need to send to emoncms ?
            if (opts.emoncms )
            {
              // Be sure to only send new values
              if ( value_state==VALUE_CHANGED || value_state==VALUE_ADDED )
              {
                strcat(emoncms_url, ptok);
                
                // Compteur Monophasé IINST et IMAX doivent être reliés
                // IINST1 et IMAX1 
                if (strcmp(ptok, "IINST")==0 || strcmp(ptok, "IMAX")==0)
                  strcat(emoncms_url, "1");

                strcat(emoncms_url, ":");
                strcat(emoncms_url, pvalue);
                strcat(emoncms_url, ",");
              }
            }
           #endif
 
          }
          else
          {
            frame_err = true;
            //fprintf(stdout, "%s=%s BAD",ptok, pvalue);
            log_syslog(stderr, "tlf_checksum_ok('%s','%s','%c') error\n",ptok, pvalue, *pcheck);
            stats.badchecksum++;
          }
        }
        else
        {
          frame_err = true;
          log_syslog(stderr, "tlf_check_frame() no correct frame '%s'\n",ptok);
          stats.frameformaterror++;
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
      stats.frameok++;
      if (opts.verbose)
        fprintf(stdout, "Frame OK\n", len);
#ifdef USE_MYSQL
      if (opts.mysql)
      {
        MYSQL mysql;
        
        // Ecrit données dans base MySql.
        sprintf(mysql_job, "INSERT INTO %s\n  (%s)\nVALUES\n  (%s);\n", opts.table, mysql_field, mysql_value);
        
        //if (opts.verbose)
          //printf(mysql_job);
        
        if (opts.verbose)
          fprintf(stdout, "%s", mysql_job); 

        if ( db_open(&mysql) != EXIT_SUCCESS )
          log_syslog(stderr, "%d: %s \n", mysql_errno(&mysql), mysql_error(&mysql));
        else
        {
            // execute SQL query
          if (mysql_query(&mysql,mysql_job))
          {
            stats.mysqlqueryerror++;
            log_syslog(stderr, "%d: %s \n", mysql_errno(&mysql), mysql_error(&mysql));
          }
          else
            stats.mysqlqueryok++;

          db_close(&mysql);
        }
      }
#endif
#ifdef USE_EMONCMS
      if (opts.emoncms)
      {
        // last check, last char
        pcheck = &emoncms_url[strlen(emoncms_url)-1];
        
        // replace last comma by json end code
        if (*pcheck == ',')
          *pcheck = '}';
        
        // Seems we added nothing, this means that nothing changed
        if (*pcheck != '{')
        {
          // Send data to emoncms
          if (!http_post( emoncms_url ))
          {
            log_syslog(stderr, "tlf_check_frame() emoncms post error\n");
          }
        }

      }
#endif
      //debug_dump_sensors(0);
      return (len);
    }
  } 
  else
  {
    log_syslog(stderr, "tlf_check_frame() No correct frame found\n");
    stats.framesizeerror++;
  }

  return (0);

}

/* ======================================================================
Function: tlf_get_frame
Purpose : check for teleinfo frame on network
Input   : true if we need to wait for frame, false if async (take if any)
Output  : true if frame ok, else false
Comments: 
====================================================================== */
int tlf_get_frame(char block) 
{
  struct sockaddr_in tlf_from;
  int fromlen = sizeof(tlf_from);
  int n = 0;
  int timeout = 100; // (10 * 100ms)
  int ret  =false;
  char  rcv_buff[TELEINFO_BUFSIZE];

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
Input   : program name
Output  : -
Comments: 
====================================================================== */
int usage( char * name)
{

  printf("%s %s\n", PRG_NAME, PRG_VERSION);
  printf("Usage is: %s --mode s|r|t [options]\n", PRG_NAME);
  printf("  --<m>mode  :  s (=send) | r (=receive) | t (=test)\n");
  printf("                send    : read from serial and send the teleinfo frame to network\n");
  printf("                          preconized mode for daemon\n");
  printf("                receive : receive teleinfo frame from network\n");
  printf("                          need another daemon in send mode\n");
  printf("                test    : display teleinfo data received from serial\n");
  printf("Options are:\n");
  printf("  --tt<y> dev  : open serial dev name\n");
  printf("  --no<l>ock   : do not create serial lock file\n");
  printf("  --<v>erbose  : speak more to user\n");
  printf("  --<p>ort n   : send/receive on network port n (and n+1 on send mode)\n");
  printf("  --<d>aemon   : daemonize the process\n");
  printf("  --<g>et LABEL: get LABEL value from teleinfo (only possible with mode receive\n");
#ifdef USE_MYSQL
  printf("  --mys<q>l    : send data to MySQL database\n");
  printf("  --<u>ser     : mysql user login\n");
  printf("  --pass<w>ord : mysql user password\n");
  printf("  --<s>erver   : mysql server\n");
  printf("  --data<b>ase : mysql database\n");
  printf("  --<t>able    : mysql table\n");
#endif
#ifdef USE_EMONCMS
  printf("  --<e>moncms  : send data to emoncms\n");
  printf("  --u<r>l      : emoncms url\n");
  printf("  --api<k>ey   : emoncms apikey\n");
  printf("  --<n>ode     : emoncms node\n");
#endif  
  printf("  --<h>elp\n");
  printf("<?> indicates the equivalent short option.\n");
  printf("Short options are prefixed by \"-\" instead of by \"--\".\n");
  printf("Example :\n");
  printf( "teleinfo -m s -d -y /dev/teleinfo\n\tstart teleinfo as a daemon to continuously send over the network the frame received on port /dev/teleinfo\n\n");
  printf( "teleinfo -m s -y /dev/teleinfo\n\tstart teleinfo display the frame received from serial port /dev/teleinfo\n\n");
  printf( "teleinfo -m r -v\n\tstart teleinfo to wait for a network frame, then display it and exit\n");
  printf( "teleinfo -m r -g ADCO\n\tstart teleinfo to wait for a frame, then display ADCO field value and exit\n");
#ifdef USE_MYSQL
  printf( "teleinfo -m r -q -v\n\tstart teleinfo to wait for a network frame, then display SQL Query and execute it and exit\n");
#endif
#ifdef USE_EMONCMS
  printf( "teleinfo -m e -v\n\tstart teleinfo to wait for a network frame, then display EMONCMS post URL, execute it and exit\n");
#endif
}


/* ======================================================================
Function: trim
Purpose : remove leading en ending space char from a string
Input   : string pointer
Output  : string pointer
Comments: 
====================================================================== */
char* trim(char* pstr)
{
  char* end = 0;

  // Skip leading space
  while(isspace(*pstr)) 
    pstr++;

  // End of string ?
  if (!*pstr)
    return pstr;

  // Get end of string
  end = pstr + strlen(pstr) - 1;

  // Skip ending space  
  while(isspace(*end)) 
    end--;

  // Note the end of string
  *(end + 1) = '\0';

  return pstr;
}

/* ======================================================================
Function: parse_parameter
Purpose : parse option parameter
Input   : short name option
          pointer to option data
Output  : false if unknown parameter option
Comments: 
====================================================================== */
int parse_parameter(char c, char * optarg)
{
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
        
        default:
          fprintf(stderr, "--mode '%c' ignored.\n", optarg[0]);
          fprintf(stderr, "please select at least mode send, receive or test\n");
          fprintf(stderr, "--mode can be one off: 's', 'r', or 't'\n");
        break;

      }
    break;

    case 'g':
      strncpy(opts.value_str, optarg, sizeof(opts.value_str)-1 );
      opts.value_str[sizeof(opts.value_str) - 1] = '\0';
    break;
    
    case 'y':
      strncpy(opts.port, optarg, sizeof(opts.port) - 1);
      opts.port[sizeof(opts.port) - 1] = '\0';
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
    
    case 'q':
      opts.mysql = true;
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
#ifdef USE_EMONCMS
    case 'e':
      opts.emoncms = true;
    break;

    case 'r':
      strcpy(opts.url, optarg );
    break;

    case 'k':
      strcpy(opts.apikey, optarg );
    break;

    case 'n':
      opts.node= (int) atoi(optarg);
      
      if (opts.node < 0 || opts.node > 32)
      {
          fprintf(stderr, "--node '%d' ignored.\n", opts.node);
          fprintf(stderr, "--node must be between 0 and 32\n");
          opts.node = 0;
      }
    break;
#endif

    default:
      return false;
    break;
  }
  
  return true;
}

/* ======================================================================
Function: read_config
Purpose : read configuration from config file and/or command line
Input   : -
Output  : -
Comments: Config is read from config file then from command line params
          this means that command line parameters always override config
          file parameters 
====================================================================== */
void read_config(int argc, char *argv[])
{
  static struct option longOptions[] =
  {
    {"nolock",  no_argument,      0, 'l'},
    {"verbose", no_argument,      0, 'v'},
    {"port",    required_argument,0, 'p'},
    {"mode",    required_argument,0, 'm'},
    {"daemon",  no_argument,      0, 'd'},
    {"get"     ,required_argument,0, 'g'},
    {"tty"     ,required_argument,0, 'y'},
#ifdef USE_MYSQL
    {"mysql",   no_argument,      0, 'q'},
    {"user",    required_argument,0, 'u'},
    {"password",required_argument,0, 'w'},
    {"server"  ,required_argument,0, 's'},
    {"database",required_argument,0, 'b'},
    {"table"   ,required_argument,0, 't'},
#endif
#ifdef USE_EMONCMS
    {"emoncms", no_argument,      0, 'e'},
    {"url",     required_argument,0, 'r'},
    {"apikey"  ,required_argument,0, 'k'},
    {"node"    ,required_argument,0, 'n'},
#endif
    {"help",    no_argument,      0, 'h'},
    {0, 0, 0, 0}
  };

  int optionIndex = 0;
  int c;
  char str_opt[64];
  FILE* fd = NULL;
  
  char buffer[512];

  char* bufp = NULL;
  char* opt = NULL;
  char* optdata = NULL;

  // We start reading configuration from config file
  // After we take parameter given onto cmd line
  if ((fd = fopen(PRG_CFG, "r")) == NULL)
  {
    fprintf(stdout, "Skipping configuration %s\n", PRG_CFG);
  }
  else
  {
    // Scan all lines
    while (!feof(fd))
    {
      // Get line content
      if (fgets(buffer, sizeof(buffer), fd))
      {
        // remove unneeded spaces
        bufp = trim(buffer);

        //fprintf(stdout, "buffer = '%s'\n", buffer);

        // skip comments and section headers
        if( *bufp=='#' || *bufp==';' || *bufp=='[')
          continue;

        // Search the = sign for splitting
        opt = strtok(bufp, "=");
        
        // Found one ?
        if (opt)
        {
          // remove unneeded spaces
          opt = trim(opt);
          optdata = strtok(NULL, "=");

          // We have all we need to work ?
          if (optdata)
          {
            optdata = trim(optdata);
            
            optionIndex = 0;
            
            // Special options not available on command line
            if (!strcmp(opt, "network"))
            {
              strncpy(opts.network, optdata, sizeof(opts.network) - 1);
              opts.network[sizeof(opts.network) - 1] = '\0';
            }
            #ifdef USE_MYSQL
            // Special options not available on command line
            else if (!strcmp(opt, "mysql_port"))
            {
              opts.serverport = atoi(optdata);
            }
            #endif
            else
            {
              
              // Loop thru all commons options we have on command line
              while ( longOptions[optionIndex].name )
              {
                // if we find long name, just get option value
                if (!strcmp(opt, longOptions[optionIndex].name))
                {
                  // If option with no argument, on configuration file
                  // value need to be = 1 or = true
                  if  (longOptions[optionIndex].has_arg == no_argument )
                  {
                    // if not set ignore it
                    if (atoi(optdata)==1 || !strcmp(optdata, "true" ))
                      parse_parameter( longOptions[optionIndex].val, NULL);
                  }
                  else
                  {
                    parse_parameter( longOptions[optionIndex].val, optdata);
                  }
                  
                  // we found the one, no need to continue
                  break;
                }
                
                // next
                optionIndex++;
                
              } // While options
            }
          }
        }
      }
    }

    fclose(fd);
  }
  
  // Reset our option index
  optionIndex = 0;
  
  // default options
  strcpy( str_opt, "lvhdm:p:g:y:");
  #ifdef USE_MYSQL
    strcat(str_opt, "qu:w:s:b:t:");
  #endif
  #ifdef USE_EMONCMS
    strcat(str_opt,  "er:k:n:");
  #endif

  // We will scan all options given on command line.
  while (1) 
  {
    // no default error messages printed.
    opterr = 0;
    
    // Get option 
    c = getopt_long(argc, argv, str_opt,  longOptions, &optionIndex);

    // Last one ?
    if (c < 0)
      break;

    // These ones exit direct
    if (c =='h' || c == '?')
    {
      usage(argv[0]);
      exit(EXIT_SUCCESS);
    }
      
    // Parse option parameter, if unknown indicate it
    if ( parse_parameter(c, optarg) == false )
    {
      fprintf(stderr, "Unrecognized option.\n");
      fprintf(stderr, "Run with '--help'.\n");
      exit(EXIT_FAILURE);
    }
  } 

  // be sure to have a known mode
  if ( opts.mode == MODE_NONE )
  {
    fprintf(stderr, "No mode given\n");
    fprintf(stderr, "please select at least one mode such send or receive\n");
    exit(EXIT_FAILURE);
  }
  
  if ( (opts.mode == MODE_SEND || opts.mode == MODE_TEST) && !opts.port)
  { 
    fprintf(stderr, "No tty device given\n");
    fprintf(stderr, "please select at least tty device such as /dev/ttyS0\n");
    exit(EXIT_FAILURE);
  }

  // if we need to get a value, force mode to receive mode
  if (*opts.value_str && opts.mode != MODE_RECEIVE)
  {
    opts.mode = MODE_RECEIVE;
    opts.mode_str = "receive";
  }

  #ifdef USE_EMONCMS
  if (opts.daemon && opts.mode != MODE_SEND && !opts.emoncms)
  {
      fprintf(stderr, "--daemon ignored.\n");
      fprintf(stderr, "--daemon must be used only in mode send or emoncms\n");
      opts.daemon = false;
  }
  #else
  if (opts.daemon && opts.mode != MODE_SEND )
  {
      fprintf(stderr, "--daemon ignored.\n");
      fprintf(stderr, "--daemon must be used only in mode send\n");
      opts.daemon = false;
  }
  #endif

  if (opts.verbose)
  {
    printf("teleinfo v%s\n", PRG_VERSION);

    printf("-- Serial Stuff -- \n");
    printf("tty device     : %s\n", opts.port);
    printf("flowcontrol    : %s\n", opts.flow_str);
    printf("baudrate is    : %d\n", opts.baud);
    printf("parity is      : %s\n", opts.parity_str);
    printf("databits are   : %d\n", opts.databits);
#ifdef USE_MYSQL
    if (opts.mysql)
    {
      printf("-- MySql Stuff -- \n");
      printf("mySQL insert   : %s\n", opts.mysql ? "Enabled" : "Disabled");
      printf("Server is      : %s:%d\n", opts.server, opts.serverport);
      printf("Database is    : %s\n", opts.database);
      printf("Login is       : %s\n", opts.user);
      printf("Password is    : %s\n", opts.password);
      printf("Table is       : %s\n", opts.table);
    }
#endif
#ifdef USE_EMONCMS
    if (opts.emoncms)
    {
      printf("-- Emoncms    -- \n");
      printf("Emoncms post   : %s\n", opts.emoncms ? "Enabled" : "Disabled");
      printf("Server url is  : %s\n", opts.url);
      printf("APIKEY is      : %s\n", opts.apikey);
      printf("Node is        : %d\n", opts.node);
    }
#endif

    printf("-- Other Stuff -- \n");
    printf("network is     : %s\n", opts.network);
    printf("udp port is    : %d\n", opts.netport);
    printf("mode is        : %s\n", opts.mode_str);
    printf("fetch value is : %s\n", opts.value_str);
    printf("nolock is      : %s\n", opts.nolock ? "yes" : "no");
    printf("verbose is     : %s\n", opts.verbose? "yes" : "no");
    printf("\n");
  } 
}

/* ======================================================================
Function: main
Purpose : Main entry Point
Input   : -
Output  : -
Comments: 
====================================================================== */
int main(int argc, char **argv)
{
  struct sockaddr_in server,client;
  struct sigaction sa;
  fd_set rdset, wrset;
  int err_no;  
  int key;
  int mode;
  int length, flags;
  int broadcast; 
  int r, n;
  unsigned char c;
  char  rcv_buff[TELEINFO_BUFSIZE];
  int   rcv_idx;
  char time_str[200];
  time_t t;
  struct tm *tmp;
  #ifdef USE_EMONCMS
  CURLcode res;
  #endif
  
  rcv_idx = 0;
  g_fd_teleinfo = 0; 
  g_tlf_sock = 0;
  g_exit_pgm = false;
  
  // clean up our buffer
  bzero(rcv_buff, TELEINFO_BUFSIZE);
  
  // Init of our linked list
  bzero( &g_valueslist, sizeof(g_valueslist));
  p_valueslist = &g_valueslist;

  // get configuration
  read_config(argc, argv);

  // Set up the structure to specify the exit action.
  sa.sa_handler = signal_handler;
  sa.sa_flags = SA_RESTART;
  sigfillset(&sa.sa_mask);
  //sigemptyset(&sa.sa_mask);
  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGUSR1, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  sigaction (SIGINT, &sa, NULL); 

  // Init Sockets
  g_tlf_sock=socket(AF_INET, SOCK_DGRAM, 0);
  
  if (g_tlf_sock < 0) 
    fatal( "Error Opening Socket %d: %s\n",g_tlf_sock, strerror (errno));
  else
  {
    if (opts.verbose)
      log_syslog(stderr, "Opened Socket\n");
  }
  
#ifdef USE_EMONCMS
  // Initialize curl library
  if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK ) 
    fatal("Error initializing Global Curl");
  else
  {
    // Basic curl init
    g_pcurl = curl_easy_init();

    // If init was not OK
    if(!g_pcurl)
      fatal("Error initializing easy Curl");
    else
    {
      // Set curl write callback (to receive http stream response)
      if (  (res = curl_easy_setopt(g_pcurl, CURLOPT_WRITEFUNCTION, &http_write)) != CURLE_OK )
        fatal("Error initializing Curl CURLOPT_WRITEFUNCTION options : %s", curl_easy_strerror(res));
        
      // set Curl transfer timeout to 5 seconds
      else if ( (res = curl_easy_setopt(g_pcurl, CURLOPT_TIMEOUT, 5L)) != CURLE_OK )
        fatal("Error initializing Curl CURLOPT_TIMEOUT options : %s", curl_easy_strerror(res));
        
      // set Curl server connection  to 2 seconds
      else if ( (res = curl_easy_setopt(g_pcurl, CURLOPT_CONNECTTIMEOUT, 2L)) != CURLE_OK )
        fatal("Error initializing Curl CURLOPT_CONNECTTIMEOUT options : %s", curl_easy_strerror(res));
        
      // We check certificate is signed by one of the certs in the CA bundle (uncomment line with CURLOPT_SSL_VERIFYPEER to uncheck)
//      else if ( (res = curl_easy_setopt(g_pcurl, CURLOPT_SSL_VERIFYPEER, 0L)) != CURLE_OK )
//        fatal("Error initializing Curl CURLOPT_SSL_VERIFYPEER options : %s", curl_easy_strerror(res));

      // We check host name on the certificate (uncomment line with CURLOPT_SSL_VERIFYHOST to uncheck)
//      else if ( (res = curl_easy_setopt(g_pcurl, CURLOPT_SSL_VERIFYHOST, 0L)) != CURLE_OK )
//        fatal("Error initializing Curl CURLOPT_SSL_VERIFYHOST options : %s", curl_easy_strerror(res));

      else
      {
        if (opts.verbose)
          log_syslog(stderr, "Curl Initialized\n");
      }
    }
  }
#endif
  
  // Receive frame from teleinfo server, so init network
  if ( opts.mode == MODE_RECEIVE )
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
        log_syslog(stdout, "Binded on port %d\n",opts.netport);
    }
  }
  
  // Just want to receive 1 frame then exit
  if (!opts.daemon && opts.mode == MODE_RECEIVE )
  {
    if (opts.verbose)
      log_syslog(stdout, "Inits succeded, waiting network frame\n");
    
    // Get one frame
    length = tlf_get_frame(true);
    
    // exit
    if (!length)
      clean_exit( EXIT_FAILURE );
    else    
      clean_exit( EXIT_SUCCESS );
  }

  // Send mode need to broadcast serial frame, test mode just display
  // so these modes open the serial port to get the frames
  if ( opts.mode != MODE_RECEIVE )
  {
    // Open serial port
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
      client.sin_addr.s_addr = inet_addr(opts.network);
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
    // Receive mode
    if ( opts.mode == MODE_RECEIVE )
    {
      // Wait until we got one frame
      tlf_get_frame(true);
    }
    else
    {
      // Read from serial port
      n = read(g_fd_teleinfo, &c, 1);
    
      if (n < 0)
        fatal("read failed: %s", strerror(errno));

      // Test mode display received char ?
      if (opts.mode == MODE_TEST)
      {
        fprintf(stdout, "%c", c);
        fflush(stdout);
      }

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

              // Update stats
              stats.framesent++;

              // Do we need to do other job ?
              #if defined USE_EMONCMS && defined USE_MYSQL
                if (opts.emoncms || opts.mysql)
                  tlf_check_frame(rcv_buff);
              #elif defined USE_EMONCMS
                if (opts.emoncms )
                  tlf_check_frame(rcv_buff);
              #elif defined USE_MYSQL
                if (opts.mysql)
                  tlf_check_frame(rcv_buff);
              #endif
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
                                (int) strlen(rcv_buff), time_str, rcv_buff );

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
          // If we are in a frame, store data received
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
      
    }
    
  } // while not exit program

  log_syslog(stderr, "Program terminated\n");
  
  clean_exit(EXIT_SUCCESS);
  
  // avoid compiler warning
  return (0);
}
