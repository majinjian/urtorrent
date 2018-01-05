/**
 * Establish a persistent connection with tracker. Periodically request 
 * tracker. To get the knowledge of peer who has the file portion missing
 * in local, and informs the tracker for which parts of file this client
 * can share. All the communication are through HTTP GET.
 *
 * NOTE: The base class "../include/client.h" has setup connection properly 
 * The session handler is assigned to a variable called handle_ 
 * which is inherited by tracker_agent.
 *
 */

#ifndef _TRACKER_AGENT_H_
#define _TRACKER_AGENT_H_

#include <mutex>         /* std::mutex and std::lock_guard */
#include <metainfo.h>    /* metainfo handle */
#include <curl/curl.h>   /* curl_* functions */
#include <timer.h>       /* class timer */
#include <unordered_map>
#include <condition_variable> /* std::condition_variable */

/**
 * Handling requests and response with remote P2P tracker.
 * Use the inherited  constructor and destructor to setup
 * and clean connection session.
 * The connection handler is also inherited from base
 */
class tracker_agent
{
  public:
    //Request event types
    enum Event {
      EVNT_START,  /* peer started */
      EVNT_COMP,   /* downloading completed */
      EVNT_STOP,   /* peer terminated */
      EVNT_EMPTY   /* not specified event */
    };

    //Tracker response message
    struct Message {
      int cmpt;              /* number of seeders */
      int incmpt;            /* number of leechers */
      int interv;            /* suggest request intervals */
      int min_interv;        /* minimum request intervals */
      string track_id;       /* optionally tracker id */
      vector<string> peers;  /* vector of 6 bytes peers */
    };

    /* constructor */
    tracker_agent(metainfo* mi);
    /* destructor */
    ~tracker_agent();
    /* trigger request to tracker */
    void do_announce();
    /* print out response info */
    void show_info(bool exclu);
    /* inform tracker client's completation */
    void complete();
    /* end communication with tracker */
    void terminate();
    /* waiting peer list */
    void waiting_peer_update();
    /* notify thread waiting peer list to wake up */
    void do_notify();

    /* setters */
    void update_upload(long long bytes);
    void update_download(long long bytes);

    /* getters */
    string get_ip();
    long long get_left();
    long long get_uploaded();
    long long get_downloaded();
    vector<string> get_peers();

  private:
    CURL* handle_;          /* curl easy handle */
    metainfo* mi_;          /* metainfo handler */
    string static_info_;    /* request static portion */
    string headers_;        /* response headers */
    string filename_;       /* target filename */
    char* ip_;              /* local IP address */
    long long upload_;      /* size of bytes uploaded */
    long long download_;    /* number of bytes downloaded since start */
    Event event_;           /* request event */
    Message mesg_ = {};     /* response from tracker */
    mutex mesg_lock_;       /* lock for accessing mesg_ */
    mutex pslk_;            /* lock for updating peer address set */
    condition_variable cv_; /* condition variable to update peer address set*/
    timer* timer_;          /* countdown timer */

    static constexpr const char* CMPAT_ = "1";        /* always accept compact reply */
    static constexpr const char* START_ = "started";  /* option for event start */
    static constexpr const char* COMP_ = "completed"; /* option for event complete */
    static constexpr const char* STOP_ = "stopped";   /* option for event stop */

    /* send request to tracker */
    void _send(string request);
    /* generate HTTP GET request */
    string compose_request();
    /* update information periodically */
    void run_service();
    
    /* callback getting response status */
    static size_t
    get_header(char *buffer, size_t size,
               size_t nitems, void *userp);
    /* callback getting response body  */
    static size_t
    _receive(void *buffer, size_t size, 
             size_t nmemb, void *userp);
};
#endif
