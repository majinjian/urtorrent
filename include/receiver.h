/**
 * Peer Wire Protocol receiver.
 * Communicate with one peer.
 * Most of the functionality are undertaken by a dedicated thread,
 * 
 *
 */

#ifndef _RECEIVER_H_
#define _RECEIVER_H_

#include <thread>      /* std::thread */
#include <mutex>       /* std::mutex */
#include <metainfo.h>  /* metainfo handle */
#include <timer.h>     /* countdown timer */
#include <types.h>     /* PWP message types, helper functions */

using namespace std;

class core;   //urtorrent core component class

class receiver
{
  public:
    /* constructor */
    receiver(string remote, core* core);

    /* destructor */
    ~receiver();

    /* URTorrent receiver main logic */
    void run();

    /* get receiver's peer */
    peer* get_peer();

    /* get peer's ip */
    string get_ip();

    /* set piece to request */
    void set_piece(uint32_t p);

    /* send interested request to peer */
    void send_interested();

  private:
    int sock_;          /* socket with remote peer */
    bool running_;      /* receiver executing status */

    string ip_;         /* ip of remote peer */
    string port_;       /* port of remote peer */

    core* core_;        /* urtorrent core component */
    metainfo* mi_;      /* metainfo handle */

    peer* peer_;        /* remote peer status */

    uint32_t piece_;    /* sequence of piece client interested */
    uint32_t size_;     /* requested block size */

    fd_set rfds_;       /* file descriptor set for read */
    int nfds_;          /* file descriptor number + 1, for select() */
    struct timeval tv_; /* time interval for keeping alive */

    timer *timer_;      /* timer to trigger keep alive request */

    /* connect peer */
    void peer_connect();

    /* handshake with peer */
    bool send_handshake();

    /* create peer for receiver */
    bool create_peer(string id);

    /* handle incomming message */
    bool mesg_handle();

    /* receive bitfield of peer */
    bool recv_bitfield(uint32_t size);

    /* send request for blocks to peer */
    void send_request();

    /* downloader thread */
    bool download(uint32_t size);

    /* add piece to requesting set */
    bool add_request_piece();

    /* remove piece from requesting set */
    void remove_request_piece();

    /* send not interested request to peer */
    void send_uninterested();

    /* retrieve block region */
    unsigned char* find_block(uint32_t offset);

    /* check if piece has been fully downloaded */
    bool complete_piece();

    /* broadcast have message to other peer receivers */
    void broadcast_have();

    /* handle have message */
    void do_update_pbf();

    /* validate piece */
    bool validate_piece();

    /* sending keep alive message */
    void keep_alive();

    /* terminate receiver */
    void terminate();

    void send_bf();
};
#endif
