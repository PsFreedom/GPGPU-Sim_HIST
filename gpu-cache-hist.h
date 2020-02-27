#include "gpu-cache.h"

enum hist_entry_status {
    HIST_INVALID,
    HIST_WAIT,
    HIST_READY
};

enum hist_request_status {
    HIST_HIT_WAIT,
    HIST_HIT_READY,
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
    void allocate( unsigned key, unsigned time){
        m_status = HIST_WAIT;
        m_key    = key;
        m_HI     = 0;

        m_alloc_time       = time;
        m_last_access_time = time;
        m_fill_time        = 0;
    }
    void print() {
        if( m_key != 0)
            printf( "| %3u | %#010x | %#04x |\n", m_status, m_key, m_HI );
        else
            printf( "| %3u | %10u | %4u |\n"    , m_status, m_key, m_HI );
    }
    unsigned count(){
        unsigned counter = 0;
        unsigned tmp_HI  = m_HI;

        while( tmp_HI > 0 ){
            counter = counter + ( tmp_HI & 1 );
            tmp_HI  = tmp_HI >> 1;
        }
        return counter;
    }

    // HIST entry fields (veriables) //
    hist_entry_status m_status;
    unsigned m_key;
    unsigned m_HI;

    // For Replacement Policy
    unsigned m_alloc_time;
    unsigned m_last_access_time;
    unsigned m_fill_time;
    
    std::list<mem_fetch*> *filtered_mf;
};

class HIST_table {
public:
    HIST_table( unsigned set, unsigned assoc, unsigned range, unsigned delay, unsigned n_sm, cache_config &config, gpgpu_sim *gpu );
    ~HIST_table(){}

    // Functions
    void print_config() const;
    void print_table( new_addr_type addr ) const;

    new_addr_type get_key(new_addr_type addr) const;
    unsigned get_set_idx(new_addr_type addr) const;
    unsigned get_home(new_addr_type addr) const;
    unsigned NOC_distance( int SM_A, int SM_B ) const;
    int MIN( int num1, int num2 ) const;
    int MAX( int num1, int num2 ) const;
    int AB( int number ) const;

    enum hist_request_status probe( new_addr_type addr) const;
    enum hist_request_status probe( new_addr_type addr, unsigned &idx) const;    
//    int hist_distance(int miss_core_id, new_addr_type addr) const;
//    int hist_abDistance(int miss_core_id, new_addr_type addr) const;

    bool check_in_range( int miss_SM, int home ) const;
    void allocate( int miss_core_id, new_addr_type addr, unsigned time );
    void add( int miss_core_id, new_addr_type addr, unsigned time );
    void del( int miss_core_id, new_addr_type addr );
    void ready( int miss_core_id, new_addr_type addr, unsigned time );
    
    void probe_dest( int miss_core_id, new_addr_type addr, mem_fetch *mf );
    void add_mf( int miss_core_id, new_addr_type addr, mem_fetch *mf );
    void fill_wait( int miss_core_id, new_addr_type addr );
    
    void recv_cycle( int core_id );
    void process_probe( int miss_core_id, mem_fetch *mf );

    // Variable
    unsigned const m_hist_nset;
    unsigned const m_hist_assoc;
    unsigned const m_hist_range;
    unsigned const m_hist_delay;
    unsigned const n_total_sm;
    
    unsigned const m_line_sz_log2;
    unsigned const m_hist_nset_log2;

protected:
    unsigned n_sm_sqrt;
    cache_config &m_cache_config;
    gpgpu_sim *m_gpu;
    
    hist_entry_t **m_hist_table;
    std::list<mem_fetch*> *recv_mf;
    std::list<mem_fetch*> *srcn_mf;
};
