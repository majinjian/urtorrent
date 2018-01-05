/**
 * A count down timer used to keep track with a time duration.
 * A timer can be registered via constructor by setting a time-
 * out handler with the prototype: 
 *       void (*) ()
 * The resolution for this timer is 1 second.
 */

#ifndef _TIMER_H_
#define _TIMER_H_

#include <chrono>             /* std::chrono::seconds and std::chrono::steady_clock */
#include <thread>             /* std::thread and sleep_for()*/
#include <functional>         /* std::function */
#include <utility>            /* bind(), make_pair() */
#include <atomic>             /* atomic<bool> */
#include <mutex>              /* std::mutex and std::lock_guard */
#include <condition_variable> /* std::condition_variable */

using namespace std;
using namespace std::chrono;

/*** Constants ***/
static const int INTV_ = 1;      /* count down interval value in seconds */

class timer
{
  public:
    /* remove default constructor */
    timer() = delete;

    /**
     * Constructor - initiate timer status and
     * set timeout handler.
     */ 
    template<typename F, typename OBJ>
    explicit timer(F&& f, OBJ&& o)
    {
      //init class members
      this->started_ = false;
      this->running_ = false;
      interv_ = seconds(INTV_);

      //set timeout handler
      this->handler_ = bind(forward<F>(f), 
                            forward<OBJ>(o));
    }
    /* destructor */
    ~timer();
    /* start the timer */
    void start(int dura);
    /* stop the timer */
    void stop();

  private:
    bool started_;                   /* timer start status */
    steady_clock::time_point epoch_; /* start time of a duration */
    steady_clock::time_point end_;   /* end time of a duration */
    seconds interv_;                 /* timer count down interval */
    atomic<bool> running_;           /* flag to show a existing timer is running */
    condition_variable cv_;          /* timer condition variable */
    mutex t_lock_;                   /* lock to notify timer */
    function<void ()> handler_;      /* timeout handle funtion */
    
    /* countdown timer */
    void countdown();
};
#endif
