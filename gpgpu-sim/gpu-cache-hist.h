class cache_config;

enum hist_entry_status {
    HIST_INVALID,
    HIST_WAIT,
    HIST_NOT_WAIT
};

enum hist_request_status {
    HIST_HIT_WAIT,
    HIST_HIT_NOT_WAIT,
    HIST_MISS,
    HIST_FULL
};

struct hist_entry_t
{
    hist_entry_t(){
        m_status = HIST_INVALID;
        m_key    = 0;
        m_HI     = 0;
        
        m_alloc_time       = 0;
        m_fill_time        = 0;
        m_last_access_time = 0;
    }
    void allocate(hist_entry_status status, unsigned key, unsigned time){
        m_status = status;
        m_key    = key;
        m_HI     = 0;
        
        m_alloc_time       = time;
        m_last_access_time = time;
        m_fill_time        = 0;
    }
    void print() {
        if( m_key != 0)
            printf("| %3u | %#010x | %#04x |\n", m_status, m_key, m_HI);
        else
            printf("| %3u | %10u | %4u |\n", m_status, m_key, m_HI);
    }

    // HIST entry fields (veriables) //
    hist_entry_status m_status;
    unsigned int      m_key;
    unsigned int      m_HI;

    // For Replacement Policy
    unsigned m_alloc_time;
    unsigned m_last_access_time;
    unsigned m_fill_time;
};

class HIST_table {
public:
    HIST_table( cache_config &config, int core_id );
    ~HIST_table();

    void print_core_id(){printf("==HIST== SM core_id %d\n", m_core_id);}
    enum hist_request_status probe( new_addr_type addr, unsigned &idx) const;
    int hist_home_distance(int target_id);
    int hist_home_abDistance(int target_id);

    void allocate( new_addr_type addr, unsigned idx, unsigned time );
    void add( unsigned idx, int distance, unsigned time );

    void print();

protected:
    cache_config &m_config;
    int m_core_id;
    unsigned m_hist_assoc;
    unsigned m_hist_nset;
    unsigned m_hist_HI_width;
    unsigned n_simt_clusters;

    hist_entry_t *m_hist_entries; 
};
