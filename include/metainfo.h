/**
 * metainfo file manipulator, the class reads the metainfo file
 * and stores metainfo into its private variables. User can di-
 * -splay parsed info plus some global system info by invoking 
 * show().
 *
 * Used for handle metainfo file (.torrent). The metainfo 
 * are benencoded, we use bendecoder written by Mike Frysinger 
 * <vapier@gmail.com>.
 *              
 */

#ifndef _METAINFO_H_
#define _METAINFO_H_

#include <string>         /* std::string */
#include <vector>         /* std:vector */
#include <iomanip>        /* setfill() and setw() */
#include <sys/stat.h>     /* stat() and struct stat */
#include <fcntl.h>        /* open() */
#include <unistd.h>       /* close() */
#include <sys/mman.h>     /* mmap() */
#include <openssl/sha.h>  /* SHA1() */
#include <bencode.h>      /* struct be_node, be_decoden(), be_free() and be_str_len() */
#include <error_handle.h> /* error_handle() */

using namespace std;

/**
 * Handling metainfo file, parse metainfo and display information.
 */
class metainfo
{
  public:
    /* constructor */
    metainfo(string file, string port);
    /* print out metainfo */
    void show_meta(string ip);
    /* getters */
    string get_announce();
    string get_filename();
    string get_port();
    string get_peerid();
    string get_infohash();
    string get_piecehash(int index);
    string get_tmpfile();
    long long get_size();
    long long get_piece_size();
    long long get_last_psize();
    size_t get_piece_num();

  private:
    string metafile_;           /* metainfo file */
  	string announce_;           /* tracker's URL */
  	string filename_;           /* target file name */
  	string port_;               /* local port */
  	string peer_id_;            /* unique ID of peer */
  	string info_hash_;          /* metainfo hash */
  	long long piece_length_;    /* length for piece */
    long long last_size_;       /* size of the last piece */
  	long long file_size_;       /* target file size */
    int bflen_;                 /* bytes needed to construct bitfield */
  	vector<string> piece_hash_; /* hash for each piece */

  	static const int MAX_SIZE_ = 8192; /* maximum metainfo file size, 8KB */
  	static const int ID_SIZE_ = 20;    /* size of peer id in byte*/

    static constexpr const char* VERSION_ = "-UR1010-"; /* version number for peer id */
    static constexpr const char* TMP_SUFFIX_ = ".tmp";  /* temporary file suffix */

  	/* generate peer ID */
  	void generate_peerid();
  	/* metainfo file parser */
  	void Parser();
  	/* extract metainfo from be_node */
  	void _dump_be_node(be_node* node, char* key);
  	/* SHA1 hash function for info dictionary */
  	void _hash_info(char* meta_str, size_t meta_len);
};
#endif
