/**
 * Implementation of Peer Wire Protocol.
 * Definition see '../include/core.h'.
 *
 */

#include <core.h>
#include <cmath>     /* ceil() */
#include <fstream>   /* std::ofstream */
#include <climits>   /* INT_MAX */
#include <ctime>     /* srand() and rand() */
#include <algorithm> /* sort() */

/****** Global Variables ******/
extern string port;      /* client port, assigned by user */



/**
 * Constructor - set components: server, tracker_agent 
 * and metainfo.
 *
 * Acknowledge current peers in torrent.
 *
 * Determine client role: seeder or leecher, initiate 
 * class members.
 *
 * Allocate disk space for temporary file if role is leecher.
 *
 * Launch a thread dedicated to dispatch peer's request.
 *
 * For each peer create a receiver which initiate thread for
 * dedicated peer.
 *
 * Register and start a timer which timeout in 10s.
 * The timer will actually timeout in every 10s due
 * to recursive calling by core::timeout.
 *
 * @serv: server object which establish TCP server
 * @mi: metainfo handler
 * @agent: remote tracker agent
 */
core::core(server* serv, metainfo* mi, 
           tracker_agent* agent) : server_(serv),
                                   mi_(mi),
                                   agent_(agent)
{
  vector<string> peers;  //vector of peers in torrent

  //retrieve current peers self-included
  peers = this->agent_->get_peers();

  //find self address
  this->local_addr_ = this->agent_->get_ip()+":"+port;

  //add peers into set
  for (unsigned int i = 0; i < peers.size(); i++) {
    //skip local address
    if (this->local_addr_ == peers[i]) continue;

    //store peer address into set
    this->pset_.insert(peers[i]);
  }

  //retrieve pieces number via metainfo
  this->pnum_ = this->mi_->get_piece_num();

  //compute total bytes needed to build bitfield
  this->bflen_ = ceil((float)this->pnum_/(float)BYTE_LEN);

  //allocate bitfield
  this->bitfield_ = new char[this->bflen_]();

  //determine spare bits position
  this->spare_offset_ = BYTE_LEN*this->bflen_ -
                        this->pnum_;

  //figure the size of the last piece
  this->plen_ = this->mi_->get_piece_size();
  this->lplen_ = this->mi_->get_last_psize();

  //init accumulative time
  this->actime_ = 0;

  //empty optimistic peer
  this->opp_ = nullptr;

  //init bitfield reader writer lock
  this->rwlock_init();

  //determine client role via inspecting local file size
  if (this->agent_->get_left()) {
    this->role_ = P_LEECHER;
    this->finish_ = false;

    //allocate temporary file
    this->temp_alloc();

    //initialize pieces count with 0
    this->pcount_ = new int[this->pnum_]();

    //initialize piece downloading progress vector to 0
    this->progress_.assign(this->pnum_, 0);

    //map file into memory
    this->map_file(this->mi_->get_tmpfile());

    //launch peer updater
    thread updater(&core::peer_updater, this);
    updater.detach();

  }
  else {
    this->role_ = P_SEEDER;
    this->finish_ = true;

    //fill bitfield
    memset(this->bitfield_, ~0, this->bflen_);
    
    //unset spare bits
    this->bitfield_[this->bflen_-1] &= 
      ((~0) << this->spare_offset_);

    //won't use piece count
    this->pcount_ = nullptr;
    
    //map file into memory
    this->map_file(this->mi_->get_filename());
  }

  //launch a dispatcher thread
  thread dispatcher(&core::dispatch, this);
  dispatcher.detach();

  //fire receiver threads to download from peers
  this->conn_peers();

  //register a timer with core::timeout as handler
  this->timer_ = new timer(&core::timeout, this);

  //startup timer that timeout every 10s.
  this->timer_->start(core::TO_UNIT_);
}

/**
 * Destructor - clean up memory
 */
core::~core()
{
  //set downloading finish
  this->finish_ = true;

  //infor updater thread to terminate
  this->agent_->do_notify();

  //clean memory allocated in this object
  delete[] this->bitfield_;
  delete this->timer_;

  if (this->pcount_)
    delete[] this->pcount_;

  //deallocate receivers
  for (auto it = this->receivers_.begin();
       it != this->receivers_.end(); it++)
    delete *it;

  //deallocate senders
  for (auto it = this->senders_.begin();
       it != this->senders_.end(); it++)
    delete *it;

  //destory reader writer locks
  this->rwlock_destroy();

  //unmap file
  this->unmap_file();

  //delete temporary file
  remove(this->mi_->get_tmpfile().c_str());

  //inform tracker of client's termination
  this->agent_->terminate();
}

/**
 * Retrieve current local bitfield.
 * Thread safe.
 * Return: string of local bitfield if succeed,
 *         otherwise an empty string is returned.
 */
string core::get_bf()
{
  string retval;  //return value

  //acquire bitfield reader lock
  if (!acquire_reader(&this->bflock_))
    goto _EXIT;

  //wrap bitfield to string
  retval = string(this->bitfield_, this->pnum_);

  //release bitfield reader lock
  if (!release_rwlock(&this->bflock_))
    retval.clear();

_EXIT:
  return retval; 
}

/**
 * Update downloaded bytes
 */
void core::update_dwn(long long bytes)
{
  this->agent_->update_download(bytes);
}

/**
 * Update uploaded bytes
 */
void core::update_upl(long long bytes)
{
  this->agent_->update_upload(bytes);
}

/**
 * Interface to check whether downloading is done
 */
bool core::full_downloaded()
{
  return this->finish_;
}

/**
 * Change temporary file name to target file name
 * when all the pieces of target file has been downloaded.
 * Also informs tracker of client's completation.
 */
void core::name_target()
{
  if (rename(this->mi_->get_tmpfile().c_str(), 
             this->mi_->get_filename().c_str()))
    error_handle(ERR_SYS);

  this->agent_->complete();
}

/**
 * A worker thread which waiting for peer list's
 * update. Once the client get a new peer list
 * from tracker, the thread will also updating
 * peer address set and launch new receiver when
 * it is appropriate.
 */
void core::peer_updater()
{
  vector<string> peers;  //vector of peers in torrent

  while (!this->finish_) {
    //block waiting for peer's update
    this->agent_->waiting_peer_update();
  
    //do nothing when downloading finished
    if (this->finish_) return;

    //perform updating
    peers = this->agent_->get_peers();

    //acquire locks to update peer
    lock_guard<mutex> lock(this->rslock_);

    for (unsigned int i = 0; i < peers.size(); i++) {
      //skip local address
      if (this->local_addr_ == peers[i]) continue;

      //skip peer already in set
      if (this->pset_.count(peers[i])) continue;

      //store peer address into set
      this->pset_.insert(peers[i]);

      //launch a receiver
      this->receivers_.insert(new receiver(peers[i], this));
    }
  }
}

/**
 * Allocate disk space to fit downloading file.
 * Create a file with size of target file and fill
 * it with raw bytes 0.
 */
void core::temp_alloc()
{
  //allocate raw bytes write to file
  char* raw_data = new char[this->mi_->get_size()]();

  //create an empty file
  ofstream tmp_file(this->mi_->get_tmpfile(), 
                    (ofstream::out|ofstream::binary));

  //fill raw bytes into file
  tmp_file.write(raw_data, this->mi_->get_size());

  //clean memory
  delete[] raw_data;
  tmp_file.close();

  //error check
  if (!tmp_file.good())
    error_handle(ERR_CREATE);
}

/**
 * setup connection with each peer by handshaking
 */
void core::conn_peers()
{
  //seeder doesn't need to receive any piece
  if (this->role_ != P_LEECHER) return;

  //for each peer launch a communicating thread
  for (auto it = this->pset_.begin(); 
       it != this->pset_.end(); it++) {
    //setup a receiver
    this->receivers_.insert(new receiver(*it, this));
  }
}

/**
 * Incoming peer's request dispatcher, this is
 * performed by a dedicated thread.
 * Once a request has come, a handle thread will be
 * launched to serve the request.
 */
void core::dispatch()
{
  int sock;    //socket with remote peer
  string ip;   //client ip;

  //continuously accept request
  while (true) {
    sock = this->server_->accept_peer(ip);

    //setup a sender thread
    this->senders_.insert(new sender(sock, ip, this));
  }
}

/**
 * Interface to update piece count.
 * Thread safe.
 * @pbf: bitfield bytes
 * Return: true if update succeed, otherwise return false.
 */
bool core::update_pcount(char* pbf)
{
  //acquire bitfield reader lock
  if (!acquire_reader(&this->bflock_))
    goto _ERROR;

  //acquire piece count writer lock
  if (!acquire_writer(&this->pclock_))
    goto _ERROR;

  //iterate though bitfield
  for (uint32_t i = 0; i < this->pnum_; i++) {
    //count of pieces already at local are set to INT_MAX, ignore them
    if (this->pcount_[i] == INT_MAX) continue;

    //increase piece count if piece bit is set
    if (*(pbf+i/BYTE_LEN) & (1<<(BYTE_LEN-i%BYTE_LEN-1)))
      this->pcount_[i]++;
  }

  //release piece count writer lock
  if (!release_rwlock(&this->pclock_))
    goto _ERROR;

  //release bitfield reader lock
  if (!release_rwlock(&this->bflock_))
    goto _ERROR;
  return true;

_ERROR:
  return false;
}

/**
 * Interface to update local bitfield
 * and set the rarity of setted piece index
 * to int max.
 * @index: piece index
 * Return: true if update succeed, otherwise false
 */
bool core::update_bf(uint32_t index)
{
  //acquire bitfield writer lock
  if (!acquire_writer(&this->bflock_))
    goto _ERROR;

  //acquire piece count writer lock
  if (!acquire_writer(&this->pclock_))
    goto _ERROR;

  //update local bitfield
  this->bitfield_[index/BYTE_LEN] |= (1 << (BYTE_LEN-index%BYTE_LEN-1));

  //set piece count of index to INT_MAX
  this->pcount_[index] = INT_MAX;

  //release piece count writer lock
  if (!release_rwlock(&this->pclock_))
    goto _ERROR;

  //release bitfield writer lock
  if (!release_rwlock(&this->bflock_))
    goto _ERROR;
  return true;

_ERROR:
  return false;
}

/**
 * Implementation of rarest first.
 * Setting interested to peer having the rarest piece.
 * Sending interested request to peer.
 *
 * The function will randomly pick one of the pieces
 * if there is a tie in number of the rarest piece.
 *
 * Thread safe.
 */
void core::rarest_first()
{
  //do nothing when all pieces are downloaded
  if (this->finish_) return;

  int rn = INT_MAX;      //rarest piece number
  uint32_t pseq;         //sequence of choosed piece
  vector<uint32_t> seqs; //sequences of the rarest pieces
  char* bf;              //pointer to peer btfield
  receiver* recv;        //pointer to receiver in hash map
  peer* pr;              //peer of receiver

  //acquire piece count reader lock
  if (!acquire_reader(&this->pclock_))
    return;

  //find the number of the rarest piece
  for (uint32_t i = 0; i < this->pnum_; i++) {
    if (this->pcount_[i] && this->pcount_[i] < rn)
      rn = this->pcount_[i];
  }

  //all pieces are downloaded, exit
  if (rn == INT_MAX) {
    release_rwlock(&this->pclock_);
    this->finish_ = true;
    return;
  }

  //find the sequences of pieces which are the rarest
  for (uint32_t i = 0; i < this->pnum_; i++) {
    if (this->pcount_[i] == rn)
      seqs.push_back(i);
  }

  //release piece count reader lock
  if (!release_rwlock(&this->pclock_))
    return;

  //randomly pick up one of the rarest piece
  srand(time(nullptr));
  pseq = seqs[rand()%seqs.size()];

  //acquire receiver hash map reader lock
  if (!acquire_reader(&this->rmlock_))
    return;

  //iterate through peer hash map,
  //determine which peer has the rarest piece
  for (auto it = this->rmap_.begin(); 
       it != this->rmap_.end(); it++) {
    recv = it->second;
    pr = recv->get_peer();

    //skip peer already interested
    if (pr->interested) continue;

    //get peer bitfield
    bf = pr->bitfield;

    //test bit of the rarest piece
    if (*(bf+pseq/BYTE_LEN) & (1<<(BYTE_LEN-pseq%BYTE_LEN-1))) {
      //inform receiver piece to interest
      recv->set_piece(pseq);
      pr->interested = true;

      //send interested request to peer
      recv->send_interested();
      break;
    }
  }

  //release receiver hash map reader lock
  if (!release_rwlock(&this->rmlock_))
    return;
}

/**
 * Regular unchoke peers which contribute top 3
 * downloading rate of this client.
 */
void core::re_unchoke()
{
  //no need to perform unchoke when sender belows 5
  if (this->smap_.size() <= core::RECIP_) return;

  receiver* recv;       //pointer to receiver in hash map
  peer* pr;             //sender's peer
  vector<peer*> drates; //array of pair <receiver, rate>
  peer_set top_three;   //set of peers with top 3 downloading rate

  //acquire receiver and sender hash map reader lock
  if (!acquire_reader(&this->rmlock_))
    return;

  if (!acquire_reader(&this->smlock_))
    return;

  //iterator through peer hash map
  for (auto it = this->rmap_.begin(); 
       it != this->rmap_.end(); it++) {
    recv = it->second;
    pr = recv->get_peer();

    //skip peer not connected
    if (!this->smap_.count(pr->id)) continue;

    //skip uninterested peer
    if (!pr->interested) continue;

    //record peer
    drates.push_back(pr);
  }

  //release receiver lock
  if (!release_rwlock(&this->rmlock_)) {
    release_rwlock(&this->smlock_);
    return;
  }

  //sort downloading rate in descending order
  sort(drates.begin(), drates.end(), 
    [](peer* p1, peer* p2) 
    {return p1->rate > p2->rate;});

  //find top 3 peers
  for (int i = 0; i < core::RE_UNCHK_; i++)
    top_three.insert(drates[i]);

  //acquire lock to access unchoked peer set
  lock_guard<mutex> lock(this->cklock_);

  //choke peers unchoked but neither in top 3
  //nor optimistic unchoked
  for (auto it = this->unchoked_.begin(); 
       it != this->unchoked_.end(); it++) {
    if (!top_three.count(*it) && this->opp_ != *it) {
       //choke peer
      (*it)->choking = true;

      //kick peer out of unchoked set
      this->unchoked_.erase(*it);
    }
  }

  //unchoke top 3 peers
  for (auto it = top_three.begin(); 
       it != top_three.end(); it++) {

    //check if peer is optimistic unchoked
    if (this->opp_ == *it)
      this->opp_ = nullptr;

    //ignore peer which already unchoked
    if (!(*it)->choking) continue;

    //set peer status
    (*it)->choking = false;

    //add peer into unchoke array
    this->unchoked_.insert(*it);

    //send unchoke message
    this->smap_[(*it)->id]->send_unchoke();
  }

  //release sender lock
  if (!release_rwlock(&this->smlock_))
    return;
}

/**
 * Optimistic unchoke - unchoke a random peer
 * which interested in this client
 */

void core::op_unchoke()
{
  //no need to perform unchoke when sender belows 5
  if (this->smap_.size() <= core::RECIP_) return;

  vector<sender*> pv; //vector of candidate senders
  int index;          //random index of candidate senders

  //choke previously optimistic unchoked peer
  if (this->opp_)
    this->opp_->choking = true;

  //acquire sender hash map reader lock
  if (!acquire_reader(&this->smlock_))
    return;

  //iterate through senders
  for (auto it = this->smap_.begin();
       it != smap_.end(); it++) {
    //skip peers that not interested
    if (!it->second->get_peer()->interested) continue;

    //skip peers that already unchoked
    if (!it->second->get_peer()->choking) continue;

    //record peer
    pv.push_back(it->second);
  }

  //randomly pick one peer
  srand(time(nullptr));
  index = rand()%pv.size();
  pv[index]->get_peer()->choking = false;

  //unchoke peer
  pv[index]->send_unchoke();

  //release sender hash map reader lock
  if (!release_rwlock(&this->smlock_))
    return;
}


/**
 * Timeout event handler, accumulate passed time and
 * decide which event should occur next.
 */
void core::timeout()
{
  //increase accumulative time
  this->actime_ += core::TO_UNIT_;

  //perform regular unchoke
  this->re_unchoke();

  //check if time to do optimistic choke
  if (!this->actime_%core::OU_PERD_) {
    this->op_unchoke();

    //restart accumulating time
    this->actime_ = 0;
  }

  //restart timer
  this->timer_->start(core::TO_UNIT_);
}

/**
 * Initialize reader writer locks.
 */
void core::rwlock_init()
{
  //lock for accessing bitfield_
  if (pthread_rwlock_init(&this->bflock_, nullptr))
    error_handle(ERR_SYS);

  //lock for accessing pcount_
  if (pthread_rwlock_init(&this->pclock_, nullptr))
    error_handle(ERR_SYS);

  //lock for accessing rmap_
  if (pthread_rwlock_init(&this->rmlock_, nullptr))
    error_handle(ERR_SYS);

  //lock for accessing smap_
  if (pthread_rwlock_init(&this->smlock_, nullptr))
    error_handle(ERR_SYS);
}

/**
 * Destroy reader writer locks.
 */
void core::rwlock_destroy()
{
  if (pthread_rwlock_destroy(&this->bflock_))
    error_handle(ERR_SYS);

  if (pthread_rwlock_destroy(&this->pclock_))
    error_handle(ERR_SYS);

  if (pthread_rwlock_destroy(&this->rmlock_))
    error_handle(ERR_SYS);
  
  if (pthread_rwlock_destroy(&this->smlock_))
    error_handle(ERR_SYS);
}

/**
 * Open file and map entire file into memory.
 * @file: path of file to map
 */
void core::map_file(string file)
{
  if (this->role_ == P_SEEDER)
    //seeder open file in read only mode
    this->fd_ = open(file.c_str(), O_RDONLY);
  else
    //leecher open file in read write mode
    this->fd_ = open(file.c_str(), O_RDWR);

  //error check
  if (this->fd_ < 0)
    error_handle(ERR_SYS);

  if (this->role_ == P_SEEDER)
    //seeder map file in read only mode
    this->file_ = (unsigned char*) mmap(nullptr, this->mi_->get_size(), 
                                        PROT_READ, MAP_PRIVATE|MAP_POPULATE, 
                                        this->fd_, 0);
  else
    //leecher map file in read write mode
    this->file_ = (unsigned char*) mmap(nullptr, this->mi_->get_size(),
                                        PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE,
                                        this->fd_, 0);
  //error check
  if (this->file_ < (unsigned char*) 0)
    error_handle(ERR_SYS); 
}

/**
 * Unmap file from memory and close file.
 */
void core::unmap_file()
{
  //unmap from memory
  if (munmap(this->file_, this->mi_->get_size()) < 0)
    error_handle(ERR_SYS);

  //close file
  if (close(this->fd_))
    error_handle(ERR_SYS);
}
