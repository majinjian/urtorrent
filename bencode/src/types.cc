/**
 * Peer struct constructor and destructor
 *
 * Utility function implementation
 *
 * See defination '../include/types.h'
 *
 */

#include <types.h>
#include <mutex>          /* std::mutex */
#include <cstring>        /* strlen() */
#include <unistd.h>       /* close() */
#include <error_handle.h> /* fail_handle */

/**
 * peer default constructor - setup
 * communication status
 */
peer::peer()
{
  rate = 0;
  choking = true;
  interested = false;
  bitfield = nullptr;
}

/**
 * peer constructor - initiate choke, interested
 * status and zero-initialize bitfields.
 * @bflen: bifield bytes
 */
peer::peer(int bflen) : peer()
{
  bitfield = new char[bflen]();
}

/**
 * peer Destructor - clean bitfield.
 */
peer::~peer()
{
  if (bitfield)
    delete[] bitfield;
}

/**
 * Construct a handshake message.
 * message format:
 *   (pstrlen)(pstr)(reserved)(info_hash)(peer_id)  
 *
 * @buff: buffer to store handshake message,
 *        should have at least 67 bytes
 * @info_hash: metainfo info hash
 * @id: client id
 */
void hs_message(char* buff, string info_hash, string id)
{
  int offset = 0;         //buffer offset

  //byte 0 (pstrlen)
  memset(buff, VERSION_LEN, VERSION_OFFSET);
  offset += VERSION_OFFSET;

  //bytes 18:1 signature
  memcpy(buff+offset, HANDSHAKE, VERSION_LEN);
  offset += VERSION_LEN;

  //bytes 26:19 reserved
  memset(buff+offset, 0, HS_RESV);
  offset += HS_RESV;

  //bytes 46:27 info hash
  memcpy(buff+offset, info_hash.c_str(), info_hash.size());
  offset += info_hash.size();

  //bytes 66:47 peer id
  memcpy(buff+offset, id.c_str(), id.size());
}

/**
 * Sending have request to update
 * peer's knowledge of this client's piece
 * @sock: socket to send message
 * @index: piece index to claim have, in local byte order
 */
void send_have(int sock, uint32_t index)
{
  uint32_t req_size;  //request size
  int offset = 0;     //offset in request buffer
  char buff[PF_LEN+HAV_LEN] = {};  //request buffer

  //convert integers to network order
  req_size = htonl(HAV_LEN);
  index = htonl(index);

  //bytes: 3:0 length prefix
  memcpy(buff, &req_size, PF_LEN);
  offset += PF_LEN;

  //byte: 4 request id
  memset(buff+offset, HAVE, ID_LEN);
  offset += ID_LEN;

  //bytes 8:5 piece index
  memcpy(buff+offset, &index, IBL_LEN);

  //send request
  if (write(sock, buff, PF_LEN+HAV_LEN) < 0)
    fail_handle(FAL_SYS);
}

/**
 * Update peer's bitfield
 * @index_ptr: piece index, in network byte order
 * @bf: pointer to peer's bitfield
 */
void update_pbf(uint32_t* index_ptr, char* bf)
{
  uint32_t index = *index_ptr;

  //convert index to local order integer
  index = ntohl(index);

  //set bitfield of peer
  bf[index/BYTE_LEN] |= (1 << (BYTE_LEN-index%BYTE_LEN-1));
}


/**
 * Acqurie reader locker, if
 * the lock is obtained by a writer thread
 * the function will spinning waiting.
 * Return: true if reader lock acquired, false if
 *         error occured.
 */
bool acquire_reader(pthread_rwlock_t *lock)
{
  int res;   //return value from pthread_rwlock_rdlock()
  do {
    res = pthread_rwlock_rdlock(lock);
  } while (res == EBUSY);

  if (res != 0)
    return false;
  return true;
}

/**
 * Acquire writer locker, if one or
 * more threads is holding the lock,
 * the function will spinning wait.
 * Return: true if writer lock acquire, false if
 *         error occured.
 */
bool acquire_writer(pthread_rwlock_t *lock)
{
  int res;   //return value from pthread_rwlock_wrlock()
  do {
    res = pthread_rwlock_wrlock(lock);
  } while(res == EBUSY);

  if (res != 0)
    return false;
  return true;  
}

/**
 * Release reader lock
 * Return: true if lock release without error,
 *         otherwise return false.
 */
bool release_rwlock(pthread_rwlock_t *lock)
{
  if (pthread_rwlock_unlock(lock))
    return false;
  return true;
}
