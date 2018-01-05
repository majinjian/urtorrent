/**
 * Implementation of error_handle and fail_handle.
 *
 * The defination can be found at "../include/error_handle.h".
 *
 */

#include <cerrno>   /* errno */
#include <error_handle.h>

extern string port;  /* port number, defined in src/urtorrent.cc */

/***** Internal Used Function *****/
void help();

/**
 * Print out error message to stderr and terminate program 
 * with exit code -1.
 *
 * @error: error enumeration define in "../include/error_handle.h".
 */
void error_handle(Error error)
{
  int error_num;  //system error code

  switch (error) {
    case ERR_USAGE:
      cerr << "Usage: urtorrent <port number> <torrent>\n";
      break;

    case ERR_BIND:
      cerr << "cannot bind port: " << port << endl;
      break;

    case ERR_CURL:
      cerr << "client error: cannot setup client environment\n";
      break;

    case ERR_SYS:
      error_num = errno;
      cerr << "sys error: " << strerror(error_num) << endl;
      break;

    case ERR_SIZE:
      cerr << "big metainfo: the metainfo file should not beyond 8KB\n";
      break;
    
    case ERR_PARSE:
      cerr << "parse error: metainfo file are not well formatted\n";
      break;

    case ERR_IP:
      cerr << "IP error: unknown error finding local address\n";
      break;
    
    case ERR_RESP:
      cerr << "server error: malformatted response\n";
      break;

    case ERR_CREATE:
      cerr << "I/O error: cannot allocate temporary file on disk\n";
      break;

    default:
      //ERR_TRACK display error message in place
      break;
  }
  exit(EXIT_FAILURE);
}

/**
 * Display message for failure, the program will not
 * be terminated by this call.
 *
 * @fail: fail enumeration define in "../include/error_handle.h".
 */
void fail_handle(Fail fail)
{
  int error_num;   //store error code

  switch (fail) {
    case FAL_IHASH:
      cerr << "connection dropped: invalid info hash\n";
      break;

    case FAL_SYS:
      error_num = errno;
      if (errno == ECONNRESET) break; //ignore reset connection
      if (errno == EPIPE) break;      //ignore broken pipe
      cerr << "sys fail: " << strerror(error_num) << endl;
      break;

    case FAL_ADDR:
      cerr << "address fail: invalid peer address\n";
      break;

    case FAL_HS:
      cerr << "handshake failed\n";
      break;

    case FAL_BIT:
      cerr << "bitfield invalid\n";
      break;

    default:
      break;
  }
}

/**
 * Overload of fail_handle with additional thread message
 *
 * @fail: fail enumeration define in "../include/error_handle.h".
 * @info: additional info provided by thread caused failure.
 */
void fail_handle(Fail fail, string info)
{
  switch (fail) {
    case FAL_CONN:
      cerr << "connection fail: cannot connect to peer " <<
           info << endl;
      break;

    default:
      break;
  }
}

/**
 * Display command usage
 */
void help()
{
  cout << "NOT VALID: Please try any of the following instead\n";
  cout << "\tmetainfo : This will show all info about"
       << "the given metainfo file\n";
  cout << "\tannounce : Sends a GET request to the tracker" 
       << "and displays the response\n";
  cout << "\ttrackerinfo : This will display the output of" 
       << "the last successful tracker response\n";
  cout << "\tshow : This will display the list of our current" 
       << "peers and some stats about them\n";
  cout << "\tstatus : This will print out the status of our download\n";
  cout << flush;
}
