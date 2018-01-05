/**
 * Implementation of class tracker_agent.
 *
 * The declaration of tracker_agent is located at "../include/tracker_agent.h".
 * The communication performed by class is undertaken by libcurl.
 *
 */

#include <tracker_agent.h>
#include <arpa/inet.h>     /* ntohl() and ntohs() */

/********** Constants **********/
static const string PARA_INFO = "?info_hash=";   /* parameter key info_hash */
static const string PARA_ID = "&peer_id=";       /* parameter key peer_id */
static const string PARA_PORT = "&port=";        /* parameter key port */
static const string PARA_UPLD = "&uploaded=";    /* parameter key upload */
static const string PARA_DWLD = "&downloaded=";  /* parameter key downloaded */
static const string PARA_LEFT = "&left=";        /* parameter key left */
static const string PARA_CMPAT ="&compact=";     /* parameter key compact */
static const string PARA_EVNT = "&event=";       /* parameter key event */
static const string BAR_CMP = "complete | ";     /* table bar cell 1 */
static const string BAR_DWN = "downloaded | ";   /* table bar cell 2 */
static const string BAR_ICP = "incomplete | ";   /* table bar cell 3 */
static const string BAR_ITV = "interval | ";     /* table bar cell 4 */
static const string BAR_MIV = "min interval | "; /* table bar cell 5 */
static const char* FAIL = "failure reason";      /* resposne failure reason field key */
static const char* WARNING = "warning message";  /* response warning message field key */
static const char* INTERV = "interval";          /* response interval field key */
static const char* MIN_INTERV = "min interval";  /* response min interval field key */
static const char* TKID = "tracker id";          /* response tracker id field key */
static const char* CMPT = "complete";            /* response complete field key */
static const char* INCMPT = "incomplete";        /* response incomplete field key */
static const char* PEERS = "peers";              /* response peers field key */
static const char* DELIM = "\r";                 /* HTTP response line delimiter */
static const char* SEP = "| ";                   /* table cell delimiter */
static const int PEER_LEN = 6;                   /* bytes represent single peer in response */
static const int PEER_WIDTH = 31;                /* peers list table width */
static const int IP_LEN = 4;                     /* length of IPv4 address */
static const int IP_WIDTH = 17;                  /* IP cell length */

/****** Global Variables ******/
static char error_msg[CURL_ERROR_SIZE];          /* curl error message buffer */
static CURLcode status;                          /* curl status code */

/***** Internal Functions *****/
static void
be_node_parser(be_node* node, char* key, 
               tracker_agent::Message* resp);

static string convert_order(char* bytes);

/**
 * Constructor - initialize class members and notify the tracker
 * which specified in metainfo file by sending an initial request.
 * Start timer which on time out event will send request to tracker.
 *
 * @mi: metainfo object
 */
tracker_agent::tracker_agent(metainfo* mi) : mi_(mi)
{
  char* encoded_hash; //urlencoded info_hash
  char* encoded_id;   //urlencoded peer_id

  //setup curl global environment
  if(curl_global_init(CURL_GLOBAL_ALL)) {
    error_handle(ERR_CURL);
  }

  //initialize curl easy session
  this->handle_ = curl_easy_init();

  //urlencode info hash
  encoded_hash = 
  curl_easy_escape(this->handle_,
                   this->mi_->get_infohash().c_str(), 
                   0);

  //urlencode peer_id
  encoded_id = 
  curl_easy_escape(this->handle_,
                   this->mi_->get_peerid().c_str(), 
                   0);

  //set callback getting response headers
  curl_easy_setopt(this->handle_, CURLOPT_HEADERFUNCTION, 
                   &tracker_agent::get_header);

  //store headers in callback
  curl_easy_setopt(this->handle_, CURLOPT_HEADERDATA, &(this->headers_));


  //set response callback function
  curl_easy_setopt(this->handle_, CURLOPT_WRITEFUNCTION,
                   &tracker_agent::_receive);

  //pass Response struct to callback
  curl_easy_setopt(this->handle_, CURLOPT_WRITEDATA, &(this->mesg_));

  //set curl handle perform HTTP GET
  status = 
  curl_easy_setopt(this->handle_, CURLOPT_HTTPGET, 1L);

  if(status != CURLE_OK)
    error_handle(ERR_CURL);

  //set error message buffer
  curl_easy_setopt(this->handle_, CURLOPT_ERRORBUFFER, error_msg);

  //the URL part of GET request
  this->static_info_ += this->mi_->get_announce();

  //parameter info_hash
  this->static_info_ += PARA_INFO;
  this->static_info_ += string(encoded_hash);
  
  //parameter peer_id
  this->static_info_ += PARA_ID;
  this->static_info_ += string(encoded_id);
  
  //parameter port
  this->static_info_ += PARA_PORT;
  this->static_info_ += this->mi_->get_port();

  //init other members
  this->upload_ = 0;
  this->download_ = 0;
  this->event_ = tracker_agent::EVNT_START;
  this->filename_ = this->mi_->get_filename();

  //send initial request
  this->_send(this->compose_request());

  //reset event to regular request
  this->event_ = tracker_agent::EVNT_EMPTY;

  //register a timer for periodical tracking
  this->timer_ 
  = new timer(&tracker_agent::run_service, this);

  //start tracking timer
  timer_->start(min(this->mesg_.interv, 
                    this->mesg_.min_interv));

  //free allocated string
  curl_free(encoded_hash);
  curl_free(encoded_id);
}

/**
 * Destructor - delete timer object and
 * cleanup curl easy session and environment.
 */
tracker_agent::~tracker_agent()
{
  delete this->timer_;
  
  //clean curl easy session
  curl_easy_cleanup(this->handle_);

  //clean curl environment
  curl_global_cleanup();
}

/**
 * Interface to explicitly perform a HTTP GET request to tracker.
 * And print out status returned by tracker.
 */
void tracker_agent::do_announce()
{
  //stop tracking timer
  this->timer_->stop();

  //reset headers
  this->headers_.clear();

  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  //reset peers
  this->mesg_.peers.clear();

  //request tracker
  this->_send(this->compose_request());

  //get response status from header
  char *header_buff = new char[this->headers_.size()+1]();
  memcpy(header_buff, this->headers_.c_str(), this->headers_.size());
  char* status = strtok(header_buff, DELIM);
  if(!status)
    error_handle(ERR_RESP);

  //print out status
  cout << "\tTracker responsed: " << status << endl;
  this->show_info(false);

  //restart tracking timer
  this->timer_->start(min(this->mesg_.interv, 
                          this->mesg_.min_interv));
  delete[] header_buff;
}

/**
 * Print out information contained in tracker response.
 *
 * @exclu: mutual exclusion is guaranteed set to true,
 *         otherwise, there might data race.
 */
void tracker_agent::show_info(bool exclu)
{
  /* table bar length */
  static int bar_size = BAR_CMP.size() +
                        BAR_DWN.size() +
                        BAR_ICP.size() +
                        BAR_ITV.size() +
                        BAR_MIV.size() + 1;

  int sepos;  //position of ':' in string

  //check if mutual exclusion is needed
  if (exclu)
    lock_guard<mutex> lock(this->mesg_lock_);

  //display table bar
  cout << "\t" << BAR_CMP << BAR_DWN << BAR_ICP <<
       BAR_ITV << BAR_MIV << endl;
  cout << setfill('-') << setw(bar_size) <<
       left << "\t" << endl;

  //display info value
  cout << setfill(' ') << "\t";
  cout << setw(BAR_CMP.size()-2) << left <<
       this->mesg_.cmpt << SEP;
  cout << setw(BAR_DWN.size()-2) << left <<
       this->download_ << SEP;
  cout << setw(BAR_ICP.size()-2) << left <<
       this->mesg_.incmpt << SEP;
  cout << setw(BAR_ITV.size()-2) << left <<
       this->mesg_.interv << SEP;
  cout << setw(BAR_MIV.size()-2) << left <<
       this->mesg_.min_interv << SEP;
  cout << endl;

  //display peers list
  cout << "\tPeer List (self included):\n";
  cout << "\t\tIP               | Port\t\n";
  cout << setfill('-') << setw(PEER_WIDTH) << left <<
       "\t\t" << endl;
  cout << setfill(' ');

  for (unsigned int i = 0; i < this->mesg_.peers.size(); i++) {
    sepos = this->mesg_.peers[i].find(":");

    //print out ip and port
    cout << "\t\t";
    cout << setw(IP_WIDTH) << left <<
         this->mesg_.peers[i].substr(0,sepos) << SEP;
    cout << this->mesg_.peers[i].substr(sepos+1);
    cout << endl;
  }
  cout << flush;
}

/**
 * Inform tracker client's downloading is completed
 */
void
tracker_agent::complete()
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  //reset peers
  this->mesg_.peers.clear();

  //set event to complete
  this->event_ = tracker_agent::EVNT_COMP;

  //send message to tracker
  this->_send(this->compose_request());

  //reset event
  this->event_ = tracker_agent::EVNT_EMPTY;
}

/**
 * Terminate communication with tracker
 */
void
tracker_agent::terminate()
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  //set event to stopped
  this->event_ = tracker_agent::EVNT_STOP;

  //send message to trakcer
  this->_send(this->compose_request());
}

/**
 * Block waiting for peer's renew.
 */
void
tracker_agent::waiting_peer_update()
{
  //wait for conditional variable
  unique_lock<mutex> lock(this->pslk_);
  this->cv_.wait(lock);
}

/**
 * notify thread waiting peer list to wake up
 */
void
tracker_agent::do_notify()
{
  unique_lock<mutex> lock(this->pslk_);
  this->cv_.notify_one();
}

/**
 * Interface for updating number of uploaded bytes.
 * Thread safe
 */
void 
tracker_agent::update_upload(long long bytes)
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  this->upload_ += bytes;
}

/**
 * Interface for updating number of downloaded bytes.
 * Thread safe.
 */
void 
tracker_agent::update_download(long long bytes)
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  this->download_ += bytes;
}

/**
 * Interface retrieving local IP address
 */
string tracker_agent::get_ip()
{
  return this->ip_;
}

/**
 * Find how many bytes left to be downloaded.
 *
 * Return: bytes of file need to be downloaded. 
 * If the peer is seeder, or the peer has completed 
 * downloading the function will return 0.
 *
 * Return: file size in bytes remain to be downloaded
 */
long long 
tracker_agent::get_left()
{
  struct stat buff = {}; //zero initialized file info buffer

  if (stat(this->filename_.c_str(), &buff) != 0) {
    //target file doesn't exist
    if (errno != ENOENT)
      error_handle(ERR_SYS);
    return this->mi_->get_size()-this->download_;
  }
  return 0;
}

/**
 * Interface to retrieve how many bytes uploaded
 */
long long tracker_agent::get_uploaded()
{
  return this->upload_;
}

/**
 * Interface to retrieve how many bytes downloaded
 */
long long tracker_agent::get_downloaded()
{
  return this->download_;
}

/**
 * Retrieve peers in torrent. The access to peers list
 * is thread safe by mutual exclusion.
 *
 * Return: a vector containing peer address and port
 * in format: ip:port
 */
vector<string> tracker_agent::get_peers()
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  return this->mesg_.peers;
}

/**
 * Send a HTTP GET request to remote tracker.
 *
 * The function can be triggerd either by main thread
 * via user command or thread perform periodical re-
 * questing. Thus, a mutual exclusion for accessing Me-
 * ssage is necessary.
 *
 * By sending, the function will also probe local
 * IP address and store it without data race.
 *
 * @request: the HTTP GET request string
 */
void tracker_agent::_send(string request)
{
  //set destination URL
  status = 
  curl_easy_setopt(this->handle_, CURLOPT_URL, request.c_str());

  if (status != CURLE_OK)
    error_handle(ERR_CURL);

  //perform request
  status = 
  curl_easy_perform(this->handle_);

  if (status != CURLE_OK) {
    cerr << error_msg << endl;
    error_handle(ERR_TRACK);
  }

  //notify thread waiting on peer list
  this->do_notify();

  //store local IP
  status =
  curl_easy_getinfo(this->handle_, CURLINFO_LOCAL_IP, &this->ip_);

  if(status != CURLE_OK && !this->ip_)
    error_handle(ERR_IP);
}

/**
 * Generate HTTP GET request string by appending parameters to the 
 * static URL string
 *
 * Return: URL with full GET parameters
 */
string tracker_agent::compose_request()
{
  string request;      //full GET request URL

  //compose request string
  request = static_info_ + PARA_UPLD;
  request += to_string(this->upload_);
  request += PARA_DWLD;
  request += to_string(this->download_);
  request += PARA_LEFT;
  request += to_string(this->get_left());
  request += PARA_CMPAT;
  request += string(tracker_agent::CMPAT_);

  //return if event is not specified
  if (this->event_ == tracker_agent::EVNT_EMPTY) {
    return request;
  }
  request += PARA_EVNT;

  //check event
  switch(this->event_) {
    case tracker_agent::EVNT_START:
      //event start
      request += tracker_agent::START_;
      break;

    case tracker_agent::EVNT_COMP:
      //event completed
      request += tracker_agent::COMP_;
      break;

    case tracker_agent::EVNT_STOP:
      //event stopped
      request += tracker_agent::STOP_;
      break;

    default:
      //shouldn't reach here
      break;
  }
  return request;
}

/**
 * Peridocally request tracker to update local status,
 * the request interval is determined by last response
 * from tracker.
 *
 * NOTE: this member function is invoked by a timer
 */
void tracker_agent::run_service()
{
  //acquire message lock
  lock_guard<mutex> lock(this->mesg_lock_);

  //reset peers
  this->mesg_.peers.clear();

  //perform a request
  this->_send(this->compose_request());

  //start a new tracking timer
  this->timer_->start(min(this->mesg_.interv, 
                          this->mesg_.min_interv));
}

/**
 * Callback function for receiving response header.
 *
 * @buffer: ptr to response
 * @size: size of one data item
 * @nmemb: number of data items
 * @usrp: passed string to store headers
 *
 * Return: bytes processed.
 */
size_t
tracker_agent::get_header(char *buffer, size_t size,
                          size_t nitems, void *usrp)
{
  int len = size*nitems;            //length of header
  string* headers = (string*) usrp; //wrap passed pointer to string

  //collect header
  headers->append(buffer, len);

  return len;
}

/**
 * Callback function for receiving reponse body.
 * The response message are bencoded.
 *
 * @buffer: ptr to response
 * @size: size of one data item
 * @nmemb: number of data items
 * @message: passed tracker_agent::Message pointer
 *
 * Return: bytes processed.
 */
size_t 
tracker_agent::_receive(void *buffer, size_t size, 
                        size_t nmemb, void *message)
{
  size_t len = size*nmemb;  //response length
  be_node* node;            //bencode nodes

  //decode resposne
  node = be_decoden((char*)buffer, (long long)len);
  if (!node)
    return len;

  //check response format
  if (node->type != BE_DICT)
    error_handle(ERR_RESP);

  be_node_parser(node, nullptr, (Message*) message);

  //clean up nodes
  be_free(node);
  return len;
}

/**
 * Traverse through be_node and extract bencoded value
 *
 * @node: be_node pointer
 * @key: be_node dictionary key
 * @resp: pointer to tracker_agent::Message
 * @peer_ptr: pointer to a peer, used by internal recursive
 */
static void
be_node_parser(be_node* node, char* key,
               tracker_agent::Message* resp)
{
  string msg;       //be_node string message

  switch(node->type) {
    case BE_STR:
      //get peers, tracker id, failure, warning message
      msg = string(node->val.s, (size_t)be_str_len(node));
     
      if (!strcmp(key, PEERS)) {  //tracker reply binary mode
        //validate peer field
        if (msg.size()%PEER_LEN)
          error_handle(ERR_RESP);

        //store each peer in vector
        while (!msg.empty()) {
          resp->peers.push_back(
              convert_order(
                const_cast<char*>(msg.c_str())
              ));
                               
          msg = msg.substr(PEER_LEN);
        }
      }
      else if (!strcmp(key, TKID)) {
        //the tracker returned a tracker id,
        //we will use it for next announces
        resp->track_id = msg;
      }
      else if (!strcmp(key, FAIL)) {
        cerr << "error: " << msg << endl;
      }
      else if (!strcmp(key, WARNING)) {
        cout << "warning: " << msg << endl << flush;
      }
      break;

    case BE_INT:
      //get interval, min interval, complete, incomplete and port
      if (!strcmp(key, INTERV)) {
        resp->interv = node->val.i;
      }
      else if (!strcmp(key, MIN_INTERV)) {
        resp->min_interv = node->val.i;
      }
      else if (!strcmp(key, CMPT)) {
        resp->cmpt = node->val.i;
      }
      else if (!strcmp(key, INCMPT)) {
        resp->incmpt = node->val.i;
      }
      break;

    case BE_LIST:
      //should never reach here
      error_handle(ERR_RESP);

    case BE_DICT:
      //iterate through bencode dictionary
      for (int i = 0; node->val.d[i].val; ++i)
        be_node_parser(node->val.d[i].val, 
                       node->val.d[i].key, 
                       resp);
      break;
  }
}

/**
 * Convert received peer bytes into IP address string
 * and convert port with local ordering
 *
 * @bytes: peer bytes which contains IP and port in big endian
 *
 * Return: a string formatted as x.x.x.x:port
 */
static string convert_order(char* bytes) 
{
  string retval;         //return value int ip:port format
  uint16_t port;         //local ordering port
  unsigned char ip_byte; //ip byte
  
  //convert IP to string
  for (int i = 0; i < IP_LEN; i++) {
    ip_byte = 
      reinterpret_cast<const unsigned char&>(bytes[i]);
    
    retval += to_string((unsigned int) ip_byte);
    if(i+1 != IP_LEN)
      retval += ".";
  }

  //convert network ordering to local ordering
  port = ntohs(*((uint16_t*) (bytes+IP_LEN)));
  retval += (":"+to_string((unsigned int) port));

  return retval;
}
