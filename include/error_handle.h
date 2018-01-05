/**
 * Definition of Error types and handle program error
 * by display error message and terminate program.
 *
 * Definition of Fail types and print out fail message.
 *
 */

#ifndef _ERROR_HANDLE_H_
#define _ERROR_HANDLE_H_

#include <iostream>
#include <string>         /* std::string */
#include <cstring>        /* strerror() and strcmp() */

using namespace std;

/* Error types 
 * These types will cause program termination
 */
enum Error {
  ERR_USAGE,   /* invalid program argument */
  ERR_CURL,    /* curl setup error */
  ERR_SYS,     /* system error */
  ERR_SIZE,    /* file exceed limited size */
  ERR_PARSE,   /* malformatted metainfo file */
  ERR_BIND,    /* port binding error */
  ERR_TRACK,   /* error on communication with tracker */
  ERR_IP,      /* error on finding local address */
  ERR_RESP,    /* response message not valid */
  ERR_CREATE   /* error on creating temporary file */
};

/* Fail types 
 * These types will not terminate program,
 * a infomation message will print out.
 */
enum Fail {
  FAL_IHASH, /* info hash not valid */
  FAL_SYS,   /* systematic failure */
  FAL_ADDR,  /* invalid peer address */
  FAL_CONN,  /* cannot connect to peer */
  FAL_HS,    /* handshake failed */
  FAL_BIT    /* bitfield invalid */
};

/*** Handle Functions ***/
void error_handle(Error error);
void fail_handle(Fail fail);
void fail_handle(Fail fail, string info);
void help();
#endif
