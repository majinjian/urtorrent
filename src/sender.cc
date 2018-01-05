/**
 * Implementation of Peer Wire Protocol sender.
 *
 */

#include <sender.h>
#include <core.h>       /* class core */

/**
 * Constructor - initiate members and launch
 * a thread communicating with peer
 * @sock: client socket
 * @remote: client ip string
 * @core: core component
 */
sender::sender(int sock, string remote, 
               core* core) : sock_(sock),
                             ip_(remote),
                             core_(core)
{
  //init members
  this->mi_ = this->core_->mi_;
  this->piece_ = 0;
  this->begin_ = 0;
  this->size_ = 0;
  this->running_ = false;

  //set up kepp alive time interval
  this->tv_.tv_sec = core::ALIVE_PERD_;
  this->tv_.tv_usec = 0;

  //set up fd set
  FD_ZERO(&this->rfds_);
  FD_SET(this->sock_, &this->rfds_);
  this->nfds_ = this->sock_+1;

  //init a timer for sending keep alive
  this->timer_ = new timer(&sender::keep_alive, this);

  //launch sender thread
  thread t_send(&sender::run, this);
  t_send.detach();
}

/**
 * Destructor
 */
sender::~sender()
{
  close(this->sock_);
}

/**
 * Sender thread job, control communication
 * with peer
 */
void sender::run()
{
  //set status
  this->running_ = true;

  //waiting handshake
  if (!this->recv_handshake())
    goto _EXIT;

  //send bitfield message
  if (!this->send_bitfield())
    goto _EXIT;

  while (this->running_) {
    req_handler();
  }

_EXIT:  //thread termination
  close(this->sock_);
  this->terminate();
}

/**
 * Interface to get sender's peer
 */
peer* sender::get_peer()
{
  return this->peer_;
}

/**
 * Interface to get peer's IP
 */
string sender::get_ip()
{
  return this->ip_;
}

/**
 * Send unchoke message to peer
 */
void sender::send_unchoke()
{
  uint32_t len_prefix;           //length prefix  
  char buff[PF_LEN+ID_LEN] = {}; //unchoke message buffer

  //convert length prefix to network order
  len_prefix = htonl(COMM_LEN);

  //bytes: 3:0 length prefix
  memcpy(buff, &len_prefix, PF_LEN);

  //byte: 4, message ID
  memset(buff+PF_LEN, UNCHOKE, ID_LEN);

  //send request to peer
  if (write(this->sock_, buff, PF_LEN+ID_LEN) < 0)
    fail_handle(FAL_SYS);
}

/**
 * Interface to send have message to peer
 */
void sender::do_send_have(uint32_t index)
{
  send_have(this->sock_, index);
}


/**
 * Handle handshake request.
 * Receive incomming handshake and send back
 * a handshake message.
 * Return: true if handshake succeed, otherwise false
 */
bool sender::recv_handshake()
{
  char hs_req[HS_LEN] = {};  //handshake request buffer
  char hs_mesg[HS_LEN] = {}; //return handshake buffer
  int rdsz;                  //data read size
  string info_hash;          //info hash received
  string version;            //peer's version
  string peer_id;            //requesting peer's id

  //fetch handshake message
  if ((rdsz = read(this->sock_, hs_req, HS_LEN)) < 0) {
    fail_handle(FAL_SYS);
    goto _FAIL;
  }

  //connection closed by peer
  if (!rdsz)
    goto _FAIL;

  //check peer version
  version = string(hs_req+VERSION_OFFSET, VERSION_LEN);
  if (version != string(HANDSHAKE)) {
    fail_handle(FAL_HS);
    goto _FAIL;
  }

  //skip 28 bytes to retrieve 20 bytes info hash
  info_hash = string(hs_req+HASH_OFFSET, SHA_DIGEST_LENGTH);

  //identify peer
  if (info_hash != this->mi_->get_infohash()) {
    fail_handle(FAL_IHASH);
    goto _FAIL;
  }

  //retrieve peer id in last 20 bytes
  peer_id = string(hs_req+HASH_OFFSET+SHA_DIGEST_LENGTH);

  //create peer
  this->peer_ = new peer(this->core_->bflen_);
  this->peer_->id = peer_id;
  
  //add entry in sender hash map
  if (!this->create_peer(peer_id))
    goto _FAIL;

  //generate return handshake
  hs_message(hs_mesg, this->mi_->get_infohash(), 
             this->mi_->get_peerid());

  //send return handshake
  if (write(this->sock_, hs_mesg, HS_LEN) < 0) {
    close(this->sock_);
    goto _FAIL;
  }

  return true;

_FAIL: //return for failed handshake
  return false;
}

/**
 * Block waiting peer's request, the handle
 * of request is based on request types
 * defined in '../include/types.h'.
 */
void sender::req_handler()
{
  uint32_t req_size;   //buffer size for request
  int rdsz;            //data read size
  int retval;          //return value from select()
  char* req_buff;      //request buffer

  //start keep alive count down
  this->timer_->start(core::ALIVE_PERD_);

  retval = select(this->nfds_, &this->rfds_, nullptr, 
                  nullptr, &this->tv_);

  //stop timer
  this->timer_->stop();

  if (retval == -1) {  //select syscall failed
    this->running_ = false;
    fail_handle(FAL_SYS);
    return;
  }

  if (!retval) {  //timeout
    this->running_ = false;
    return;
  }

  //fetch request size
  if ((rdsz = read(this->sock_, &req_size, PF_LEN)) < 0) {   
    fail_handle(FAL_SYS);
    this->running_ = false;
    return;
  }

  //connection closed by peer
  if (!rdsz) {
    this->running_ = false;
    return;
  }

  //convert request size to local order
  req_size = ntohl(req_size);

  //check if is keep-alive request
  if (req_size == KEEP_ALIVE)
    return;

  //allocate request buffer
  req_buff = new char[req_size]();

  //fetch request
  if (read(this->sock_, req_buff, req_size) < 0) {
    fail_handle(FAL_SYS);
    this->running_ = false;
    goto _EXIT;
  }

  if (*req_buff == INTERESTED) {  //get interested request
    //set peer interested
    this->peer_->interested = true;

    //check if unchoke message is need to sent
    if (!this->need_unchoke()) goto _EXIT;

    //send unchoke message
    this->send_unchoke();
  }
  else if (*req_buff == NO_INTERESTED) {  //get not interested request
    //acquire choked set lock
    this->core_->cklock_.lock();

    //remove peer from unchoked set
    this->core_->unchoked_.erase(this->peer_);

    //set peer status to not interested and choked
    this->peer_->interested = false;
    this->peer_->choking = true;

    //release choked set lock
    this->core_->cklock_.unlock();

    //choke peer
    this->send_choke();
  }
  else if (*req_buff == REQUEST) {  //get block request
    //prepare sender to upload
    this->prepare_upload(req_buff+ID_LEN);

    //upload block to peer
    this->upload();

    //check if peer needs to be choked
    if (this->peer_->choking)
      this->send_choke();
  }
  else if (*req_buff == HAVE) { //get have request
    //update peer's bitfield
    update_pbf((uint32_t*) (req_buff+ID_LEN), 
               this->peer_->bitfield);
  }

_EXIT:
  //clean up memory
  delete[] req_buff;
}

/**
 * Create an entry in <peer_id, sender> hash map
 * @id: peer id.
 * Return: true on success, otherwise false
 */
bool sender::create_peer(string id)
{
  //acquire hash map writer lock
  if (!acquire_writer(&this->core_->smlock_))
    goto _FAIL;

  //add entry in hash map
  this->core_->smap_[id] = this;

  //release hash map reader lock
  if (!release_rwlock(&this->core_->smlock_)) {
    //delete entry
    this->core_->smap_.erase(id);

    goto _FAIL;
  }

  return true;
_FAIL:
  return false;
}

/**
 * Tell peer which pieces this client has.
 * If the client doesn't have any piece,
 * no message will be sent
 * Return: true if communication succeed, otherwise fasle.
 */
bool sender::send_bitfield()
{
  string bit_str;    //bitfield string
  char mesg_buff[HD_LEN+this->core_->bflen_] = {};  //zero-initialize message buffer
  
  //retrieve local bitfield
  bit_str = this->core_->get_bf();
  if (bit_str.empty()) {
    goto _FAIL;
  }

  //don't send bitfield if no bit is set
  if (bit_str == string(this->core_->pnum_, 0))
    goto _SUCC;

  //compose bitfield message
  compose_bfmesg(mesg_buff);

  //send bitfield message to peer
  if (write(this->sock_, mesg_buff, 
            HD_LEN+this->core_->bflen_) < 0) {
    fail_handle(FAL_SYS);
    goto _FAIL;
  }
    
_SUCC:  //return for succeed communication
  return true;

_FAIL:  //return for failed communication
  return false;
}

/**
 * Generate bitfield message
 * @buff: buffer to store bitfield message
 */
void sender::compose_bfmesg(char* buff)
{
  uint32_t mesg_len;    //length of message
  int offset = 0;       //offset in buffer

  //get network order length prefix
  mesg_len = htonl(ID_LEN+this->core_->bflen_);

  //bytes: 3:0 length prefix
  memcpy(buff, &mesg_len, PF_LEN);
  offset += PF_LEN;

  //byte: 4 message ID
  memset(buff+offset, (int) BIT_FIELD, ID_LEN);
  offset += ID_LEN;

  //bytes: B:5 bitfield
  memcpy(buff+offset, this->core_->bitfield_,
         this->core_->bflen_);
}

/**
 * Test if sender needs to send a unchoke message.
 * A sender is allowed to unchoke when
 *  -unchoked sender is below 4
 *
 * When sender is regular or optimistic unchoked,
 * a unchoke message is unnecessary as the message
 * had been sent by thread did unchoke.
 *
 * Return: true if unchoke is allowed, otherwise
 *         false.
 */
bool sender::need_unchoke()
{
  //acquire lock to access unchoke peer array
  lock_guard<mutex> lock(this->core_->cklock_);

  //unchoke is allowed when active sender belows maximum uploader
  if (this->core_->unchoked_.size() < core::RECIP_) {
    //set peer status
    this->peer_->choking = false;

    //add peer into unchoked set
    this->core_->unchoked_.insert(this->peer_);
    return true;
  }

  //check if unchoked by regular unchoke or optimistic unchoke
  return this->peer_->choking;
}

/**
 * Compose and send choke message to peer.
 */
void sender::send_choke()
{
  uint32_t mesg_size;                 //message size in network order
  char mesg_buff[PF_LEN+ID_LEN] = {}; //message buffer

  //convert length prefix to network order
  mesg_size = htonl(COMM_LEN);

  //bytes 3:0 length prefix
  memcpy(mesg_buff, &mesg_size, PF_LEN);

  //byte 4 message id
  memset(mesg_buff+PF_LEN, CHOKE, ID_LEN);

  //send message to peer
  if (write(this->sock_, mesg_buff, PF_LEN+ID_LEN) < 0)
    fail_handle(FAL_SYS);
}

/**
 * Set sender status for requested piece
 * @buff: block request buffer
 */
void sender::prepare_upload(char* buff)
{
  int offset = 0;   //offset in request buffer

  //retrieve piece index
  memcpy(&this->piece_, buff, IBL_LEN);
  offset += IBL_LEN;

  //convert piece index to locl order
  this->piece_ = ntohl(this->piece_);

  //retrieve block offset
  memcpy(&this->begin_, buff+offset, IBL_LEN);
  offset += IBL_LEN;

  //convert block offset to local order
  this->begin_ = ntohl(this->begin_);

  //retrieve block size
  memcpy(&this->size_, buff+offset, IBL_LEN);

  //convert block size to local order
  this->size_ = ntohl(this->size_);

  //update upload progress
  this->core_->update_upl(this->size_);
}

/**
 * Upload a requested block to peer by
 * sending piece request.
 */
void sender::upload()
{
  uint32_t mesg_size = 0; //size of message
  uint32_t index = 0;     //piece index
  uint32_t begin = 0;     //block offset   
  char* buff = nullptr;   //message buffer
  unsigned char* block = nullptr;  //pointer to block data
  int offset = 0;         //offset in message buffer

  time_point<steady_clock> epoch; //upload start time
  microseconds dura;              //upload duration

  //compute message size
  mesg_size = PF_LEN + PIC_LEN + this->size_;

  //allocate message buffer
  buff = new char[mesg_size]();

  //find block data
  block = this->find_block(this->begin_);

  //convert integers to network order
  mesg_size = htonl(mesg_size);
  index = htonl(this->piece_);
  begin = htonl(this->begin_);

  //bytes: 3:0 prefix length
  memcpy(buff, &mesg_size, PF_LEN);
  offset += PF_LEN;

  //byte: 4 message id
  memset(buff+offset, PIECE, ID_LEN);
  offset += ID_LEN;

  //bytes: 8:5 piece index
  memcpy(buff+offset, &index, IBL_LEN);
  offset += IBL_LEN;

  //bytes: 12:9 block offset
  memcpy(buff+offset, &begin, IBL_LEN);
  offset += IBL_LEN;

  //bytes: L:13 block data
  memcpy(buff+offset, block, this->size_);
  offset += this->size_;

  //record upload start time
  epoch = steady_clock::now();

  //send block to peer
  if (write(this->sock_, buff, PF_LEN + PIC_LEN + this->size_) < 0)
    fail_handle(FAL_SYS);

  //compute upload duration
  dura = duration_cast<microseconds>(steady_clock::now()-epoch);

  //get rate
  this->peer_->rate = ((PF_LEN + PIC_LEN + this->size_)/
                       (double)dura.count()) * MIC_PER_SEC;

  //clean memory
  delete[] buff;
}

/**
 * Find location of block data
 * @offset: block offset in piece
 * Return: pointer to block
 */
unsigned char* sender::find_block(uint32_t offset)
{
  unsigned char* block = nullptr;  //pointer to block

  //adding offset to block
  block = this->core_->file_ + 
          this->mi_->get_piece_size() * 
          this->piece_ + offset;

  return block;
}

/**
 * Sending keep alive message to peer
 * @sock: socket to send message
 * Return: true when communication succeed, otherwise false.
 */
void sender::keep_alive()
{
  //send message
  write(this->sock_, &KEEP_ALIVE, PF_LEN);

  //start new countdown
  this->timer_->start(core::ALIVE_PERD_);
}

/**
 * terminating sender by clean associated objects
 */
void sender::terminate()
{
  delete this->timer_;
 
  //acquire sender lock
  if (!acquire_writer(&this->core_->smlock_))
    return;

  //remove self from sender map
  this->core_->smap_.erase(this->peer_->id);

  //clean memory
  delete this->peer_;

  //relase lock
  if (!release_rwlock(&this->core_->smlock_))
    return;

  //acquire lock to access sender set
  lock_guard<mutex>(this->core_->sslock_);

  //remove entry from sender set
  this->core_->senders_.erase(this);
}
