/**
 * Implementation of metainfo class
 *
 * Current implementation used bendecode written by
 * Mike Frysinger <vapier@gmail.com>.
 *               
 * See "../include/metainfo.h" for declaration.
 *
 */

#include <metainfo.h>  /* metainfo class */
#include <ctime>       /* srand(), rand() and time() */

/************* Constants *************/
static const char* ANNOUNCE = "announce";  /* metainfo announce field key */
static const char* LENGTH = "length";      /* metainfo length field key */
static const char* NAME = "name";          /* metainfo name field key */
static const char* PLEN = "piece length";  /* metainfo piece length field key */
static const char* PIECE = "pieces";       /* metainfo pieces field key */
static const char* INFO = "4:infod6:";     /* anchor for info directory */
static const int EXTRA_LEN = 6;            /* bytes before info dictionary */
static const int MAX_ASCII = 256;          /* upper bound of ascii (exclusive) */
static const int BYTE_DIGIT = 2;           /* digits of hexdecimal per byte */
static const int MIN_ALIGNMENT = 5;        /* display alignment shift */

/********** Internal Function **********/
static void print_binary(string str);

/**
 * Constructor - Read metainfo file and parse metainfo.
 *
 * @file: metainfo file
 * @port: port this peer listen on
 */
metainfo::metainfo(string file, string port)
{
  this->metafile_ = file;
  this->port_ = port;

  //retrieve local info
  this->generate_peerid();

  //parse metainfo file
  this->Parser();
}

/**
 * Print out metainfo message.
 *
 * Note:  ID is in hexdecimal format
 *
 * @ip: local IP address
 */
void metainfo::show_meta(string ip)
{
  int plen =
  (to_string(this->piece_hash_.size()).size());   //maximum piece sequence display size
  
  int aligment =
  (plen > MIN_ALIGNMENT) ? plen : MIN_ALIGNMENT;  //display alignment

  cout << "\tIP/port\t: " <<
       ip << "/" << this->port_ << endl;
  
  cout << "\tID\t: ";
  print_binary(this->peer_id_);
  
  cout << "\tmetainfo file : " <<
       this->metafile_ << endl;
  
  cout << "\tinfo hash\t: ";
  print_binary(this->info_hash_);
  
  cout << "\tfile name\t: " <<
       this->filename_ << endl;
  
  cout << "\tpiece length\t: " <<
       this->piece_length_ << endl;
  
  cout << "\tfile size\t: " <<
       this->file_size_ << " (" <<
       this->file_size_/this->piece_length_ <<
       " * [piece length] + " <<
       this->last_size_ <<
       ")" << endl;
  
  cout << "\tannounce URL\t: " <<
       this->announce_ << endl;

  cout << "\tpieces' hashes : " << endl;
  for (unsigned int i = 0; i < this->piece_hash_.size(); i++) {
    cout << "\t" << setfill(' ') << setw(aligment) << i << ":  ";
    print_binary(this->piece_hash_[i]);
  }
  cout << flush;
}

/**
 *  Interface for retrieve tracker URL
 */
string metainfo::get_announce()
{
  return this->announce_;
}

/**
 * Interface for retrieving target filename
 */
string metainfo::get_filename()
{
  return this->filename_;
}

/**
 * Interface for retrieving local port
 */
string metainfo::get_port()
{
  return this->port_;
}

/**
 * Interface for retrieving random generated peer id
 */
string metainfo::get_peerid()
{
  return this->peer_id_;
}

/**
 * Interface for retrieving metafile info SHA1 hash
 */
string metainfo::get_infohash()
{
  return this->info_hash_;
}

/**
 * Interface to get hash of specific piece
 * @index: piece index
 */
string metainfo::get_piecehash(int index)
{
  return this->piece_hash_[index];
}

/**
 * Interface for retrieving temporary file name
 */
string metainfo::get_tmpfile()
{
  return this->filename_+metainfo::TMP_SUFFIX_;
}

/**
 * Interface for retrieving target file size
 */
long long metainfo::get_size()
{
  return this->file_size_;
}

/**
 * Interface to get the each piece's size
 */
long long metainfo::get_piece_size()
{
  return this->piece_length_;
}

/**
 * Interface to retrieve size of the last piece
 */
long long metainfo::get_last_psize()
{
  return this->last_size_;
}

/**
 * Interface for retrieving total number of pieces
 */
size_t metainfo::get_piece_num()
{
  return this->piece_hash_.size();
}

/**
 * Generate 20 bytes peer_id, following the convention
 * specified by <http://bittorrent.org>.
 */
void metainfo::generate_peerid()
{
  string uid;   //peer id to be generated
  int bcount;   //random bytes count

  //generate random seed
  srand(time(NULL));

  //append client version at the beginning
  uid = string(metainfo::VERSION_);

  //generate random bytes
  bcount = metainfo::ID_SIZE_ - uid.size();
  while (bcount-- > 0) {
    uid += rand()%MAX_ASCII;
  }
  this->peer_id_ = uid;
}

/**
 * Read and parse metainfo file.
 * The read operation is helped by memory mapping,
 * the assumed largest metainfo is no bigger than
 * 8KB. 
 * Decoding is done by invoking C bendecoder.
 */
void metainfo::Parser()
{
  int fd;                //metainfo file descriptor
  size_t size;           //metainfo file size
  char* map_region;      //memory mapped region
  be_node* node;         //bedecoder node
  struct stat buff = {}; //zero initialized file info buffer

  this->get_tmpfile();

  //check file size
  if (stat(this->metafile_.c_str(), &buff) != 0)
    error_handle(ERR_SYS);
  if (buff.st_size > metainfo::MAX_SIZE_)
    error_handle(ERR_SIZE);
  size = (size_t)buff.st_size;

  //open metainfo file
  fd = open(this->metafile_.c_str(), O_RDONLY);
  if (fd < 0) {
    error_handle(ERR_SYS);
  }

  //map metainfo file into memory with read only permission
  map_region = (char*)mmap(nullptr, size, PROT_READ, 
                           MAP_PRIVATE, fd, 0);

  if (map_region < (char*) 0) {
    error_handle(ERR_SYS);
  }

  //generate metainfo be_node
  node = be_decoden(map_region, (long long)size);
  if (!node || node->type != BE_DICT) {
    error_handle(ERR_PARSE);
  }

  //parse metainfo from be_node
  this->_dump_be_node(node, nullptr);

  //hash info dictionary
  this->_hash_info(map_region, size);

  //compute the size of the last piece
  this->last_size_ = 
  this->file_size_%this->piece_length_;

  //clean be_node memory
  be_free(node);

  //unmap file
  if (munmap(map_region, size) < 0) {
    error_handle(ERR_SYS);
  }

  //close metainfo file
  if (close(fd) < 0) {
    error_handle(ERR_SYS);
  }
}

/**
 * Extracting metainfo by traverse be_node.
 *
 * @node: be_node to parse.
 * @key: field of metainfo, could be nullptr if not applicable.
 */
void metainfo::_dump_be_node(be_node* node, char* key)
{
  string strval;  //bencode string value

  switch (node->type) {
    case BE_STR:
      //extract announce, filename, piece hash
      strval = string(node->val.s, (size_t)be_str_len(node));
      if(!strcmp(key, ANNOUNCE)) {
        this->announce_ = strval;
      }
      else if (!strcmp(key, NAME)) {
        this->filename_ = strval;
      }
      else if (!strcmp(key, PIECE)) {
        while (strval.size()/SHA_DIGEST_LENGTH) {
          this->piece_hash_.push_back(
                 strval.substr(0, SHA_DIGEST_LENGTH));
          strval = strval.substr(SHA_DIGEST_LENGTH);
        }
      }
      break;

    case BE_INT:
      //extract file length, piece length
      if (!strcmp(key, LENGTH)) {
        this->file_size_ = node->val.i;
      }
      else if (!strcmp(key, PLEN)) {
        this->piece_length_ = node->val.i;
      }
      break;

    case BE_LIST:
      //shouldn't contain any list
      error_handle(ERR_PARSE);

    case BE_DICT:
      //iterate through bencode dictionary
      for (int i = 0; node->val.d[i].val; ++i)
        _dump_be_node(node->val.d[i].val, node->val.d[i].key);
      break;
  }
}

/**
 * Using SHA1 hash info dictionary.
 *
 * @meta_str: metainfo string.
 * @meta_len: length of metainfo string.
 */
void metainfo::_hash_info(char* meta_str, size_t meta_len)
{
  char* info_str;                            //info field string
  unsigned char hash_res[SHA_DIGEST_LENGTH]; //array of hash result
  size_t info_len = 0;                       //info dictionary length
  
  //find position of info key
  info_str = strstr(meta_str, INFO);
  info_str += EXTRA_LEN;

  //compute length of info dictionary
  //subtract preceding bytes and trailing 'e'
  info_len = (meta_len - (info_str - meta_str))/sizeof(char);

  //perform SHA1 hash on info dictionary
  SHA1((unsigned char*)info_str, info_len, hash_res);
  this->info_hash_ = string(reinterpret_cast<char*>(hash_res),
                            SHA_DIGEST_LENGTH);
}

/**
 * Print out string as 2 digits hexdecimal value per byte.
 *
 * @str: string to print out.
 */
static void print_binary(string str)
{
  //string to unsigned char* conversion
  const unsigned char* cstr =
         reinterpret_cast<const unsigned char*>(str.c_str());

  //print 2 digits hexdecimal value for each 20 byte hash byte
  for(unsigned int i = 0; i < str.size(); i++) {
    cout << setfill('0') << setw(BYTE_DIGIT) <<
         hex << ((unsigned int)(cstr[i]));
  }
  cout << dec << endl;
}
