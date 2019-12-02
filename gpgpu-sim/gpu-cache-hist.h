class cache_config;

enum hist_status {
    WAIT,
    NOT_WAIT
};

struct hist_entry_t
{
    hist_entry_t(){
        m_status = NOT_WAIT;
        m_key    = 0;
        m_HI     = 0;
    }
    void allocate(hist_status status, unsigned int key, unsigned int HI){
        m_status = status;
        m_key    = key;
        m_HI     = HI;
    }

    // HIST entry fields (veriables) //
    hist_status  m_status;
    unsigned int m_key;
    unsigned int m_HI;
};

class HIST_table {
public:
    HIST_table( cache_config &config, int core_id );
    ~HIST_table();

protected:
    hist_entry_t *m_hist_entries; 
    int m_core_id;  // which shader core is using this
};
