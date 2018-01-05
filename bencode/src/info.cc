/**
 * Implementation of show and status command.
 *
 *
 */

#include <core.h>

/************ Constants ***********/
static const int SHOW_WD = 58;    /* basic display width for show */
static const int STATUS_WD = 30;  /* basic display width for status */
static const int RATE_ALIGN = 9;  /* display alignment for rate */
static const int ID_ALIGN = 3;    /* display alignment for id */
static const int IP_ALIGN = 16;   /* display alignment for ip */
static const int DWN_ALIGN = 11;  /* display alignment for download */
static const int UPL_ALIGN = 9;   /* display alignment for upload */
static const int LEFT_ALIGN = 5;  /* display alignment for left */
static const char BON = '1';      /* display for bit is set */
static const char BOFF = '0';     /* display for bit is unset */

/***** Internal Used Functions *****/
void display_id_ip(int id, string ip);
void display_status(peer* p);

/**
 * Implementation of command show
 */
void core::do_show()
{
  int uline = this->pnum_+SHOW_WD;  //table width

  cout << "\t\tID | ";
  cout << "IP address      | ";
  cout << "Status | ";
  cout << setw(this->pnum_) << setfill(' ');
  cout << left << "Bitfield" << " | ";
  cout << "Down/s   | ";
  cout << "Up/s     |";
  cout << endl;
  cout << setw(uline) << setfill('-')
       << left << "\t\t";
  cout << endl;
  show_peers();
  cout << endl << flush;
}

/**
 * Implementation of command status
 */
void core::do_status()
{
  int uline = this->pnum_+STATUS_WD;  //table width

  //display bar
  cout << "\t\t"
       << "Downloaded | "
       << "Uploaded | "
       << " Left | "
       << setw(this->pnum_) << setfill(' ')
       << left << "My bit field"
       << endl;
  cout << setw(uline) << setfill('-')
       << left << "\t\t"
       << endl;

  //display statistics
  cout << "\t\t"
       << setw(DWN_ALIGN) << setfill(' ')
       << left << this->agent_->get_downloaded()
       << "| " << setw(UPL_ALIGN) << setfill(' ')
       << left << this->agent_->get_uploaded()
       << "| " << setw(LEFT_ALIGN) << setfill(' ')
       << left << this->agent_->get_left()
       << "| ";
  show_bf(this->bitfield_);
  cout << endl << flush;
}

/**
 * Display status of peers
 *
 */
void core::show_peers()
{
  peer* pr;         //peer associated with receiver
  peer* ps;         //peer associated with sender
  int id = 0;       //id column value

  //iterate through receiver map
  for (auto it = this->rmap_.begin(); 
       it != this->rmap_.end(); it++) {
    //get receiver's peer
    pr = it->second->get_peer();

    //check if peer has a sender
    if (this->smap_.count(it->first))
      //get sender's upload rate
      ps = this->smap_[it->first]->get_peer();
    else
      ps = nullptr;

    //display peer's info
    display_id_ip(id, it->second->get_ip());
    display_peer(pr, ps);
    id++;
  }

  pr = nullptr;

  //check if there is any missed sender
  for (auto it = this->smap_.begin(); 
       it != this->smap_.end(); it++) {
    //skip peer already displayed
    if (this->rmap_.count(it->first)) continue;
  
    ps = it->second->get_peer();

    //display peer
    display_id_ip(id, it->second->get_ip());
    display_peer(pr, ps);
    id++;
  }
}

/**
 * Display bitfield
 * @bf: pointer to bitfield bytes
 */
void core::show_bf(char* bf)
{
  uint32_t bit = 0; //number of displayed bit
  char* byte;       //pointer to byte in bitfield

  for (int i = 0; i < this->bflen_; i++) {
    byte = bf+i;
    for (int j = 0; j < BYTE_LEN; j++) {
       //skip spare bits
      if (bit == this->pnum_) break;
      
      //test bits
      if (*byte & (1<<(BYTE_LEN-j-1)))
        cout << BON;
      else
        cout << BOFF;

      bit++;
    }
  }
}

/**
 * Display single peer status
 * @pr: peer from perspective of receiver
 * @ps: peer from perspective of sender
 */
void core::display_peer(peer* pr, peer* ps)
{
  //display status:
  //am_choking, am_interested, peer_choking, peer_interested. 
  display_status(ps);
  display_status(pr);
  cout << "   | ";
      
  //display bitfield
  if (pr != nullptr)
    this->show_bf(pr->bitfield);
  else
    this->show_bf(ps->bitfield);
  cout << " | ";

  //display rates
  if (!pr)
    cout << setw(RATE_ALIGN) << BOFF
         << "| ";
  else
    cout << setw(RATE_ALIGN) << pr->rate
         << "| ";
    
  if (!ps)
    cout << setw(RATE_ALIGN) << BOFF
         << "| ";
  else
    cout << ps->rate;

  cout << endl << flush;
}

/**
 * Display table id and peer ip
 * @id: id to print
 * @ip: ip of peer
 */
void display_id_ip(int id, string ip)
{
  cout << "\t\t"
       << setw(ID_ALIGN) << setfill(' ')
       << left << id << "| ";
  cout << setw(IP_ALIGN) << setfill(' ')
       << left << ip << "| ";
}

/**
 * Display status: choking, interested 
 * @p: peer to show status
 */
void display_status(peer* p)
{
  if (!p) {
    cout << BON << BOFF;
    return;
  }

  if (p->choking)
    cout << BON;
  else
    cout << BOFF;
  if (p->interested)
    cout << BON;
  else
    cout << BOFF;
}
