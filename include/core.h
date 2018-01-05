/**
 * Peer Wire Protocol core class.
 *
 */

#ifndef _CORE_H_
#define _CORE_H_

#include <server.h>        /* TCP server */
#include <metainfo.h>      /* metainfo file handle */
#include <tracker_agent.h> /* remote tracker handle */
#include <timer.h>         /* count down timer */
#include <types.h>         /* PWP message types helper functions */
#include <receiver.h>      /* downloader */
#include <sender.h>        /* uploader */

class core
{
  public:
    friend class receiver;
    friend class sender;

    static const int ALIVE_PERD_ = 120;   /* period in sec sending keep alive message */

    /* constructor */
    core(server* serv, metainfo* mi, 
         tracker_agent* agent);

    /* destructor */
    ~core();

    /* command show */
    void do_show();

    /* command status */
    void do_status();

    /* retrieve current locl bitfield */
    string get_bf();

    /* update downloaded bytes */
    void update_dwn(long long bytes);

    /* update uploading bytes */
    void update_upl(long long bytes);

    /* check whether downloading is done */
    bool full_downloaded();

    /* modify temporary file name to target file */
    void name_target();

  private:
    Role role_;            /* client role: seeder or leecher */
    int fd_;               /* file descriptor of temporary|target file */
    unsigned char* file_;  /* pointer to memory mapped temporary|target file */
    
    server* server_;       /* TCP server */
    metainfo* mi_;         /* metainfo handler */
    tracker_agent* agent_; /* tracker handler */
    timer* timer_;         /* protocol timer */
    peer* opp_;            /* optimistic unchoked peer */

    pthread_rwlock_t bflock_; /* reader writer lock to access bitfield */
    pthread_rwlock_t pclock_; /* reader writer lock to access piece count array */
    pthread_rwlock_t rmlock_; /* reader writer lock to access peer hash map */
    pthread_rwlock_t smlock_; /* reader writer lock to access peer hash map */
    mutex rslock_;            /* lock to access receiver set */
    mutex sslock_;            /* lock to access sender set */
    mutex cklock_;            /* lock to access unchoked peer set */
    mutex relock_;            /* lock to access requesting piece set */

    char* bitfield_;       /* pieces bitfield */
    int* pcount_;          /* array of count for each piece */
    bool finish_;          /* flag to show whether downloading finished */

    uint32_t pnum_;        /* number of pieces */
    uint32_t lplen_;       /* length of last piece */
    uint32_t plen_;        /* length per piece */
    int bflen_;            /* bytes of bitfield */
    int spare_offset_;     /* start position of spare bits in bitfield */
    int actime_;           /* accumulative time */
    string local_addr_;    /* client local address ip:port */

    addr_set pset_;        /* IP set of current peers */
    piece_set req_set;     /* set of requested pieces */
    peer_set unchoked_;    /* set of peers unchoked by client */
    recv_map rmap_;        /* hash map <peer_id, receiver> */
    send_map smap_;        /* hash map <peer_id, sender> */
    recv_set receivers_;   /* receiver set */
    sender_set senders_;   /* sender set */

    vector<uint32_t> progress_;     /* downloaded size for each piece */

    static const unsigned int RECIP_ = 4; /* number of total unchoking peers */
    static const int RE_UNCHK_ = 3;       /* number of regular unchoking peers */
    static const int OU_PERD_ = 30;       /* period in sec performing optimistic unchoke */ 
    static const int TO_UNIT_ = 10;       /* timeout unit in sec */

    /* a worker thread updating peer's address set */
    void peer_updater();

    /* allcate temporary file */
    void temp_alloc();

    /* connect to other peers */
    void conn_peers();

    /* dispatch incomming request */
    void dispatch();

    /* interface to update piece count */
    bool update_pcount(char* pbf);

    /* interface to update local bitfield */
    bool update_bf(uint32_t index);

    /* set interest to peer containing the rarest piece */
    void rarest_first();
    
    /* perform regular unchoke */
    void re_unchoke();

    /* perform optimistic unchoke */
    void op_unchoke();

    /* timeout handler */
    void timeout();

    /* helper function to init rw lock */
    void rwlock_init();

    /* helper function to destroy rw lock */
    void rwlock_destroy();

    /* memory map file into memroy */
    void map_file(string file);

    /* unmap memory mapped file */
    void unmap_file();

    /* helper function to set a bitfield reference */
    void set_bitfield(char* bf);

    /* display helper */
    void show_peers();
    void show_bf(char* bf);
    void display_peer(peer* pr, peer* ps);
};
#endif
