/**
 * Implementation of timer.
 * std::chrono::steady_clock has been used to keep program
 * time in monotonic.
 *
 * The time unit is std::chrono::seconds.
 *
 * The actually timer is tracked by a dedicated thread when a
 * timer is triggered to start
 *
 * Note: the constructor is defined in '../include/timer.h'.
 */

#include <timer.h>

/**
 * Destructor - wait until all timer thread terminate
 */
timer::~timer()
{
  //spinning wait thread to terminate
  do {
    this->stop();
  } while (this->running_);
}

/**
 * Start a timer by setting a epoch and end timepoint.
 * A dedicated tracking thread will be created
 *
 * @dura: duration for which this timer will count down
 */
void timer::start(int dura)
{
  seconds wait_t(dura); //timeout duration in seconds

  //set current time as epoch
  this->epoch_ = steady_clock::now();
  //set timeout timepoint
  this->end_ = epoch_+wait_t;
  //reset start status
  this->started_ = true;

  //launch a thread to count down timer
  thread t_job(&timer::countdown, this);
  t_job.detach();
}

/**
 * Inform timer to stop
 */
void timer::stop()
{
  unique_lock<mutex> lk(this->t_lock_);
  this->started_ = false;
  this->cv_.notify_one();
}

/**
 * Continuously checking current timepoint against the
 * end timepoint.
 * On timeout, a handler setted in previous will be invoked
 */
void timer::countdown()
{
  //indicate a running thread
  this->running_ = true;

  //check timer not stopped
  if (!this->started_) {
    //indicate thread termination
    this->running_ = false;
    return;
  }

  //check time between each second
  while (steady_clock::now() < this->end_) {
    //terminate coutdown thread when stopped
    if (!this->started_) {
      //indicate thread termination
      this->running_ = false;
      return;
    }

    //wait for a while
    unique_lock<mutex> lk(this->t_lock_);
    this->cv_.wait_for(lk, this->interv_);
  }

  //trigger timeout event here
  this->handler_();
}
