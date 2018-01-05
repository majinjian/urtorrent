/**
 * Peer Wire Protocol sender.
 *
 */

#ifndef _SENDER_H_
#define _SENDER_H_

#include <thread>        /* std::thread */
#include <mutex>         /* std::mutex */
#include <metainfo.h>    /* metainfo handle */
#include <timer.h>       /* countdown timer */
#include <types.h>       /* PWP message types, helper functions */

class core;   //urtorrent core component class

class sender
{
  public:
    /* constructor */
    sender(int sock, string remote, core* core_);

    /* destructor */
    ~sender();

    /* URTorrent sender main logic */
    void run();

    /* get sender's peer */
    peer* get_peer();

    /* get peer's ip */
    string get_ip();

    /* send unchoked message */
    void send_unchoke();

    /* send have message to peer */
    void do_send_have(uint32_t index);

  private:
    int sock_;          /* socket with remote peer */
    bool running_;      /* sender executing status */

    string ip_;         /* peer's ip */

    core* core_;        /* urtorrent core component */
    metainfo* mi_;      /* metainfo handle */
    peer* peer_;        /* remote peer status */

    uint32_t piece_;    /* peer requested piece index */
    uint32_t begin_;    /* block offset in piece */
    uint32_t size_;     /* size of block requested */

    fd_set rfds_;       /* file descriptor set for read */
    int nfds_;          /* file descriptor number + 1, for select() */
    struct timeval tv_; /* time interval for keeping alive */

    timer *timer_;      /* timer for keep alive */

    /* handle handshake */
    bool recv_handshake();

    /* handle incomming message */
    void req_handler();

    /* create peer for sender */
    bool create_peer(string id);

    /* inform peer of local pieces */
    bool send_bitfield();
    
    /* generate bitfied message */
    void compose_bfmesg(char* buff);

    /* check if sender can unchoke */
    bool need_unchoke();

    /* send choke message to peer */
    void send_choke();

    /* prepare sender to upload */
    void prepare_upload(char* buff);

    /* upload block */
    void upload();

    /* retrieve block data */
    unsigned char* find_block(uint32_t offset);

    /* sending keep alive message */
    void keep_alive();

    /* terminate sender */
    void terminate();
};
#endif
