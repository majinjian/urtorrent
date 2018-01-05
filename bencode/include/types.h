/**
 * Definition of Peer Wire Protocol types.
 * -message type and number
 * -client role
 * -peer struct
 * -peer_map and peer set
 */

#ifndef _TYPES_H_
#define _TYPES_H_

#include <string>        /* std::string */
#include <unordered_map> /* std::unordered_map */
#include <unordered_set> /* std::unordered_set */
#include <pthread.h>     /* for multiple readers single writer lock */
#include <arpa/inet.h>   /* ntohl() and htonl() */

using namespace std;

/**** Protocol Request Number ****/
const uint32_t KEEP_ALIVE = 0;
const char CHOKE = 0;
const char UNCHOKE = 1;
const char INTERESTED = 2;
const char NO_INTERESTED = 3;
const char HAVE = 4;
const char BIT_FIELD = 5;
const char REQUEST = 6;
const char PIECE = 7;
const char CANCEL = 8;   //should not be used

/***** URTorrent Signature *****/
const char* const HANDSHAKE = "URTorrent protocol";  /* handshake signature */
const int HS_LEN = 67;                               /* handshake message length */
const int VERSION_LEN = 18;                          /* version length */
const int VERSION_OFFSET = 1;                        /* version offset */
const int HS_RESV = 8;                               /* handshake reserved length */
const int HASH_OFFSET = 27;                          /* offset to info hash */

/*** Common Constants ***/
const int PF_LEN = 4;        /* length prefix size */
const int IBL_LEN= 4;        /* length of index, begin, length */
const int ID_LEN = 1;        /* message ID size */
const int HD_LEN = 5;        /* message header size */
const int BYTE_LEN = 8;      /* length of byte in bits */
const uint32_t COMM_LEN = 1; /* value for usual length prefix */
const uint32_t REQ_LEN = 13; /* length of request for blocks */
const uint32_t PIC_LEN = 9;  /* fixed length of piece message */
const uint32_t HAV_LEN = 5;  /* length of have message */
const uint32_t BLOCK_SIZE = 16384; /* size of piece block in bytes */
const int MIC_PER_SEC = 1000000;   /* microseconds per second */

/***** Client Role *****/
enum Role {
  P_SEEDER,  /* P2P seeder, has entire file */
  P_LEECHER  /* P2P leecher, has no entire file */
};

/***** Peer Struct *****/
struct peer {
  uint32_t ip;      /* peer's ip address */
  uint32_t rate;    /* bit rate for downloading from */
  bool choking;     /* uploading choked by client */
  bool interested;  /* client interested in piece hold by peer */
  char* bitfield;   /* peer bitfield */
  string id;        /* peer id */

  /* default constructor */
  peer();
  /* constructor with bitfield initialized */
  peer(int bflen);

  /* destructor */
  ~peer();
};

/******** Type Definition ********/
class receiver;
class sender;
typedef unordered_set<string> addr_set;            /* peer's ip:port set */
typedef unordered_set<int> piece_set;              /* set of sequence of pieces */
typedef unordered_set<peer*> peer_set;             /* set of peers */
typedef unordered_set<receiver*> recv_set;         /* set of receivers */
typedef unordered_set<sender*> sender_set;         /* set of senders */
typedef unordered_map<string, receiver*> recv_map; /* <peer id, receiver> hash map */
typedef unordered_map<string, sender*> send_map;   /* <peer id, sender> hash map */

/***** Utility Functions *****/
void hs_message(char* buff, string info_hash, string id );
void send_have(int sock, uint32_t index);
void update_pbf(uint32_t* index_ptr, char* bf);
bool acquire_reader(pthread_rwlock_t *lock);
bool acquire_writer(pthread_rwlock_t *lock);
bool release_rwlock(pthread_rwlock_t *lock);
#endif
