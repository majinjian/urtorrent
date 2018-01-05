/**
 * Implementation of Peer Wire Protocol receiver.
 * See class defination: '../include/receiver.h'
 *
 */

#include <receiver.h>
#include <sys/socket.h> /* socket syscalls*/
#include <netdb.h>      /* getaddrinfo() and struct addrinfo */
#include <core.h>       /* class core */

/*** Constants ***/
static const char* DELIM = ":";                   /* delimitor between ip and port */

/**
 * Constructor - initiate members and launch a
 * thread to communicate with peer
 *
 * @remote: ip:port of remote peer
 * @core: urtorrent core component
 */
receiver::receiver(string remote, core* core) : core_(core)
{
  char buff[remote.size()+1] = {};  //ip:port char buffer
  char* token;                      //buffer token pointer

  //fill address buff
  memcpy(buff, remote.c_str(), remote.size());

  //set peer ip
  token = strtok(buff, DELIM);
  if (!token)
    fail_handle(FAL_ADDR);
  this->ip_ = string(token);

  //set peer port
  token = strtok(nullptr, DELIM);
  if (!token)
    fail_handle(FAL_ADDR);
  this->port_ = string(token);

  //init other members
  this->mi_ = this->core_->mi_;
  this->piece_ = 0;
  this->size_ = 0;
  this->running_ = false;

  //set up kepp alive time interval
  this->tv_.tv_sec = core::ALIVE_PERD_;
  this->tv_.tv_usec = 0;

  //init a timer for sending keep alive
  this->timer_ = new timer(&receiver::keep_alive, this);

  //launch receiver thread
  thread t_recv(&receiver::run, this);
  t_recv.detach();
}

/**
 * Destructor - deallocate memory
 */
receiver::~receiver()
{
  close(this->sock_);
}

/**
 * Receiver thread job, controll communication with peer
 */
void receiver::run()
{
  //set status
  this->running_ = true;

  //establish TCP connection with peer
  this->peer_connect();

  //handshake with peer
  if (!this->send_handshake())
    goto _EXIT;

  while (this->running_) {
    
    if (!this->mesg_handle()) continue;

    if (!this->running_)
      goto _EXIT;

    //check if client interested to peer
    if (!this->peer_->interested) continue;

    //check if peer is choking client
    if (this->peer_->choking) continue;

    //client is interested in peer
    //peer is not choking client
    //we can download blocks of interested
    //piece now.
    this->send_request();
  }

_EXIT:
  close(this->sock_);
  this->terminate();
}

/**
 * Compose and send interested request to peer.
 */
void receiver::send_interested()
{
  uint32_t len_prefix;           //length prefix  
  char buff[PF_LEN+ID_LEN] = {}; //interested request buffer

  //convert length prefix to network order
  len_prefix = htonl(COMM_LEN);

  //bytes: 3:0 length prefix
  memcpy(buff, &len_prefix, PF_LEN);

  //byte: 4, message ID
  memset(buff+PF_LEN, INTERESTED, ID_LEN);

  //send request to peer
  if (write(this->sock_, buff, PF_LEN+ID_LEN) < 0)
    fail_handle(FAL_SYS);
}

/**
 * Interface to get receiver's peer
 */
peer* receiver::get_peer()
{
  return this->peer_;
}

/**
 * Interface to retrieve peer's IP
 */
string receiver::get_ip()
{
  return this->ip_;
}

/**
 * Interface to set piece client to request
 */
void receiver::set_piece(uint32_t p)
{
  this->piece_ = p;
}

/**
 * Establish a TCP connection with peer
 */
void receiver::peer_connect()
{
  struct addrinfo hint;           //address hint info
  struct addrinfo *result, *rp;   //result for getaddrinfo()
  int rv;                         //return val from getaddrinfo
  string dest = this->ip_ +       //remote ip:port     
                *DELIM +
                this->port_;

  memset(&hint, 0, sizeof(struct addrinfo));
  
  //set hint info
  hint.ai_family = AF_INET;       //set for IPv4
  hint.ai_socktype = SOCK_STREAM; //TCP connection
  hint.ai_flags = AI_ADDRCONFIG;  //IPv4 configuration
  hint.ai_protocol = 0;
  
  //retrieve peer address info to result
  rv = getaddrinfo(this->ip_.c_str(), this->port_.c_str(), 
                   &hint, &result);
  if (rv != 0)
    fail_handle(FAL_CONN, dest);
  
  //find address from result to establish connection
  for (rp = result; rp != nullptr; rp = rp->ai_next) {
    this->sock_ = socket(rp->ai_family, 
                         rp->ai_socktype, 
                         rp->ai_protocol);
    if(this->sock_ == -1) 
      continue;
    if(connect(this->sock_, rp->ai_addr, 
               rp->ai_addrlen) != -1)
      break;
    if(close(this->sock_) < 0) {
      fail_handle(FAL_SYS);
    }
  }

  if (rp == nullptr)
    fail_handle(FAL_CONN, dest);
  
  //free result objects
  freeaddrinfo(result);

  //set read fd set
  FD_ZERO(&this->rfds_);
  FD_SET(this->sock_, &this->rfds_);
  this->nfds_ = this->sock_+1;
}

/**
 * Handshake with remote peer.
 *
 * Return: if handshake succeed return true, otherwise
 *         return false
 */
bool receiver::send_handshake()
{
  char hs_mesg[HS_LEN] = {};  //handshake message buffer
  char rt_hs[HS_LEN] = {};    //return handshake buffer
  int rdsz;                   //data read size
  string info_hash;           //info hash return by peer
  string version;             //peer version
  string peer_id;             //remote peer id

  //construct handshake message
  hs_message(hs_mesg, this->mi_->get_infohash(), 
             this->mi_->get_peerid());

  //send handshake to peer
  if (write(this->sock_, hs_mesg, HS_LEN) < 0) {
    fail_handle(FAL_SYS);
    goto _FAIL;
  }

  //read return handshake
  if ((rdsz = read(this->sock_, rt_hs, HS_LEN)) < 0) {
    fail_handle(FAL_SYS);
    goto _FAIL;
  }

  //connection closed by peer
  if (!rdsz)
    goto _FAIL;

  //check peer's version
  version = string(rt_hs+VERSION_OFFSET, VERSION_LEN);
  if (version != string(HANDSHAKE)) {
    fail_handle(FAL_HS);
    goto _FAIL;
  }

  //check info hash
  info_hash = string(rt_hs+HASH_OFFSET, SHA_DIGEST_LENGTH);
  if (info_hash != this->mi_->get_infohash()) {
    fail_handle(FAL_IHASH);
    goto _FAIL;
  }

  //retrieve peer id in last 20 bytes
  peer_id = string(rt_hs+HASH_OFFSET+SHA_DIGEST_LENGTH);

  //allocate peer with bitfield initialized
  this->peer_ = new peer(this->core_->bflen_);
  this->peer_->id = peer_id;
 
  //create receiver hash map entry
  if (!this->create_peer(peer_id))
    goto _FAIL;

  return true;

_FAIL:  //handshake failure return
  return false;
}

/**
 * Create peer in <peer_id, receiver> hash map
 * @id: peer id;
 * Return true on success, otherwise false
 */
bool receiver::create_peer(string id)
{
  //acquire hash map writer lock
  if (!acquire_writer(&this->core_->rmlock_))
    goto _FAIL;

  //add entry into hash map
  this->core_->rmap_[id] = this;

  //release hash map writer lock
  if (!release_rwlock(&this->core_->rmlock_))
    goto _FAIL;

  return true;
_FAIL:
  return false;
}

/**
 * Handle incomming message sent by peer
 * Return: true if requesting a block is permitted,
 *         otherwise false.
 */
bool receiver::mesg_handle()
{
  uint32_t mesg_size = 0;  //message size
  int rdsz = 0;            //read size
  int retval;              //return value from select()
  char mesg_id = 0;        //buffer to store message id

  //start keep alive count down
  this->timer_->start(core::ALIVE_PERD_);

  retval = select(this->nfds_, &this->rfds_, nullptr,
                  nullptr, &this->tv_);

  //stop timer
  this->timer_->stop();

  if (retval == -1) {  //select syscall failed
    this->running_ = false;
    fail_handle(FAL_SYS);
    goto _EXIT;
  }

  if (!retval) {  //timeout
    this->running_ = false;
    goto _EXIT;
  }

  //fetch message size
  if ((rdsz = read(this->sock_, &mesg_size, PF_LEN)) < 0) {   
    fail_handle(FAL_SYS);
    this->running_ = false;
    goto _EXIT;
  }

  //connection closed by peer
  if (!rdsz) {
    this->running_ = false;
    goto _EXIT;
  }

  //convert message size to local oreder
  mesg_size = ntohl(mesg_size);

  //check if it is keep-alive message
  if (mesg_size == KEEP_ALIVE)
    goto _EXIT;

  //fetch message ID
  if (read(this->sock_, &mesg_id, ID_LEN) < 0) {
    fail_handle(FAL_SYS);
    this->running_ = false;
    goto _EXIT;
  }

  if (mesg_id == BIT_FIELD) {  //get bitfield message
    //get bitfield from peer
    if (!this->recv_bitfield(mesg_size-(uint32_t)ID_LEN))
      goto _EXIT;

    //perform the rarest first
    this->core_->rarest_first();
  }
  else if (mesg_id == UNCHOKE) {  //get unchoke message
    //set peer unchoked
    this->peer_->choking = false;

    //add piece to requesting set
    if (this->add_request_piece())
      goto _EXIT;

    //piece is being requested by another thread
    //this thread can uninterested to that piece
    this->send_uninterested();
  }
  else if (mesg_id == CHOKE) {  //get choke message
    //set peer choked
    this->peer_->choking = true;

    //remove requesting piece from set
    this->remove_request_piece();
  }
  else if (mesg_id == PIECE) {  //receive block
    //write block data to file
    if (!this->download(mesg_size-PIC_LEN))
      goto _EXIT;

    //check if piece has been completely downloaded
    if (!this->complete_piece())
      goto _EXIT;

    //validate downloaded piece
    if (!this->validate_piece())
      goto _EXIT;

    //update local bitfield
    if (!this->core_->update_bf(this->piece_))
      this->running_ = false;

    //sending have request to sender
    send_have(this->sock_, this->piece_);

    //done with piece, we are uninterested in peer for the moment.
    this->send_uninterested();

    //perform rarest first
    this->core_->rarest_first();

    //change temporary file name when finish
    if (this->core_->full_downloaded()) {
      this->core_->name_target();
      this->running_ = false;
    }
  }
  else if (mesg_id == HAVE) { //receive have message
    this->do_update_pbf();
  }

  //return and wait for message
  return false;

_EXIT: //return and to send requests
  return true;
}

/**
 * Get bitfield from peer. Update piece count
 * list.
 * @size: size to retrieve from peer
 * Return: true if correctly get bitfield,
 *         otherwise return false and connection
 *         is closed.
 */
bool receiver::recv_bitfield(uint32_t size)
{
  char* pbf = this->peer_->bitfield;  //peer bitfield buffer

  //retrieve bitfield from peer
  if (read(this->sock_, pbf, size) <= 0)
    goto _FAIL;

  //validate bitfield by checking spare bits
  if ((pbf[this->core_->bflen_-1] <<
       this->core_->spare_offset_) & 0) {
    fail_handle(FAL_BIT);
    goto _FAIL;
  }

  //update piece count list
  this->core_->update_pcount(pbf);
  return true;

_FAIL:
  this->running_ = false;
  return false;
}

/**
 * Send request for blocks to peer
 */
void receiver::send_request()
{
  uint32_t len_prefix;   //length prefix in network order
  uint32_t index;        //index in network order
  uint32_t begin;        //begin offset of block in network order
  uint32_t length;       //length to request in network order
  int offset = 0;        //offset in request buffer          
  char buff[REQ_LEN+PF_LEN] = {}; //request buffer

  //request block sequence after previously downloaded
  index = this->piece_;
  begin = this->core_->progress_[index];

  //determine block size
  if (index == this->core_->pnum_-1)
    length = min(BLOCK_SIZE, this->core_->lplen_-begin);
  else
    length = BLOCK_SIZE;
  this->size_ = length;

  //convert integers to network order
  len_prefix = htonl(REQ_LEN);
  index = htonl(index);
  begin = htonl(begin);
  length = htonl(length);

  //bytes: 3:0 length prefix
  memcpy(buff, &len_prefix, PF_LEN);
  offset += PF_LEN;

  //byte: 4 request ID
  memset(buff+offset, REQUEST, ID_LEN);
  offset += ID_LEN;

  //bytes: 8:5 piece index
  memcpy(buff+offset, &index, IBL_LEN);
  offset += IBL_LEN;

  //bytes: 12:9 block begin offset
  memcpy(buff+offset, &begin, IBL_LEN);
  offset += IBL_LEN;

  //bytes: 16:13 requested length
  memcpy(buff+offset, &length, IBL_LEN);
  offset += IBL_LEN;

  //send request
  if (write(this->sock_, buff, offset) < 0)
    fail_handle(FAL_SYS);
}

/**
 * Downloading block from peer, validate
 * piece and update progress array.
 * @size: size to download.
 * Return: true if block is valid, otherwise false.
 */
bool receiver::download(uint32_t size)
{ 
  unsigned char* block = nullptr; //block pointer in file mapped region
  uint32_t piece;                 //piece that block resides
  uint32_t begin;                 //offset of block
  time_point<steady_clock> epoch; //download start time
  microseconds dura;              //downloading duration

  //get piece from message
  if (read(this->sock_, &piece, IBL_LEN) <= 0)
    goto _FAIL;

  //get offset from message
  if (read(this->sock_, &begin, IBL_LEN) <= 0)
    goto _FAIL;

  //convert intergers to local order
  piece = ntohl(piece);
  begin = ntohl(begin);

  //retrieve block region
  block = find_block(begin);

  //record download start time
  epoch = steady_clock::now();

  //download block to file region
  if (read(this->sock_, block, size) <= 0)
    goto _FAIL;

  //compute download duration
  dura = duration_cast<microseconds>(steady_clock::now() - epoch);

  //get download rate
  this->peer_->rate = (size/(double)dura.count())*MIC_PER_SEC;

  //update progress
  this->core_->progress_[this->piece_] += this->size_;
  this->core_->update_dwn(this->size_);

  return true;

_FAIL:
  this->running_ = false;
  return false;
}

/**
 * Add piece to requesting set.
 * Thread safe.
 * Return: true if piece is added, false
 *         if piece is being requested by 
 *         another thread.
 */
bool receiver::add_request_piece()
{
  //acquire request set lock
  lock_guard<mutex> lock(this->core_->relock_);

  //check if piece is being requested
  if (this->core_->req_set.count(this->piece_))
    return false;

  //add interested piece into set
  this->core_->req_set.insert(this->piece_);
  return true;
}

/**
 * Remove requested piece from requesting set
 */
void receiver::remove_request_piece()
{
  //acquire request set lock
  lock_guard<mutex> lock(this->core_->relock_);

  //remove piece
  this->core_->req_set.erase(this->piece_);
}

/**
 * Compose and send not interested request to peer
 * set peer status interested to false.
 */
void receiver::send_uninterested()
{
  uint32_t req_size;             //size of request in network order
  char buff[PF_LEN+ID_LEN] = {}; //request buffer

  //convert length prefix to network order
  req_size = htonl(COMM_LEN);

  //bytes 3:0 length prefix
  memcpy(buff, &req_size, PF_LEN);

  //byte 4 message ID
  memset(buff+PF_LEN, NO_INTERESTED, ID_LEN);

  //send request to peer
  if (write(this->sock_, buff, PF_LEN+ID_LEN) < 0) {
    fail_handle(FAL_SYS);
    this->running_ = false;
  }

  this->peer_->interested = false;
}

/**
 * Retrieve block region in file mapped memory.
 * offset: block offset from beginning of residing piece
 * Return: pointer to the beginning of block region.
 */
unsigned char* receiver::find_block(uint32_t offset)
{
  unsigned char* block = nullptr; //pointer to the beginning of block

  block = this->core_->file_ +
          this->mi_->get_piece_size() * 
          this->piece_ + offset;

  return block;
}

/**
 * Check whether a piece has been fully downloaded
 * Return: true if piece has been downloaded, otherwise false
 */
bool receiver::complete_piece()
{
  if (this->piece_ == this->core_->pnum_-1) { //current piece is the last piece
    if (this->core_->progress_[this->piece_] !=
        this->core_->lplen_)
      return false;
  }
  else { //current piece is not the last piece
    if (this->core_->progress_[this->piece_] !=
        this->core_->plen_)
      return false;
  }
  return true;
}

/**
 * Broadcast have message to other receivers
 * via local sender.
 */
void receiver::broadcast_have()
{
  //acquire lock to sender hash map
  acquire_reader(&this->core_->smlock_);

  send_map* map = &this->core_->smap_;

  //iterate through all senders
  for (auto it = map->begin(); 
       it != map->end(); it++) {
    it->second->do_send_have(this->piece_);
  }

  //release lock
  release_rwlock(&this->core_->smlock_);
}

/**
 * Update receiver's peer's bitfield
 */
void receiver::do_update_pbf()
{
  uint32_t index;   //index to update

  //read index
  if (read(this->sock_, &index, IBL_LEN) <= 0)
    return;

  //perform update
  update_pbf(&index, this->peer_->bitfield);
}

/**
 * Validate piece just donwloaded.
 * If the content is invalid, then the piece
 * will be cleared.
 * Return: true if piece is integrited, otherwise false
 */
bool receiver::validate_piece()
{
  unsigned char hash[SHA_DIGEST_LENGTH];  //buffer store piece hash
  unsigned int offset = 0;                //piece offset
  unsigned int length = 0;                //length of piece

  //get piece offset and length
  offset = this->piece_*this->core_->plen_;
  length = (this->piece_ == this->core_->pnum_-1) ? 
            this->core_->lplen_ : this->core_->plen_;

  //compute SHA-1 hash of downloaded piece
  SHA1(this->core_->file_+offset, length, hash);

  //validate hash
  if (string(reinterpret_cast<char*>(hash), 
             SHA_DIGEST_LENGTH) == 
      this->mi_->get_piecehash(this->piece_))
    return true;

  //piece is invalid, clear downloaded piece
  memset(this->core_->file_+offset, 0, length);

  //reset progress
  this->core_->progress_[this->piece_] = 0;
  this->core_->update_dwn(-length);

  return false;
}

/**
 * Sending keep alive message to peer
 * @sock: socket to send message
 * Return: true when communication succeed, otherwise false.
 */
void receiver::keep_alive()
{
  //send message
  write(this->sock_, &KEEP_ALIVE, PF_LEN);

  //start new countdown
  this->timer_->start(core::ALIVE_PERD_);
}

/**
 * terminating receiver by clear related objects
 */
void receiver::terminate()
{
  //delete timer
  delete this->timer_;


  //acquire piece count writer lock
  if (!acquire_writer(&this->core_->pclock_))
    return;

  //decrease piece count
  for (uint32_t i = 0; i < this->core_->pnum_; i++) {
    if (this->core_->pcount_[i])
      this->core_->pcount_[i]--;
  }

  //acquire receiver lock
  if (!acquire_writer(&this->core_->rmlock_))
    return;

  //remove entry from receiver map
  this->core_->rmap_.erase(this->peer_->id);

  //delete peer
  delete this->peer_;

  //release lock
  if (!release_rwlock(&this->core_->rmlock_))
    return;

  //release piece count lock
  if (!release_rwlock(&this->core_->pclock_))
    return;

  //accquire peer address set lock
  lock_guard<mutex> lock(this->core_->rslock_);

  //remove record from peer address set
  this->core_->pset_.erase(this->ip_+":"+this->port_);

  //remove entry from receiver array
  this->core_->receivers_.erase(this);

  //keep downloading alive
  this->core_->rarest_first();
}
