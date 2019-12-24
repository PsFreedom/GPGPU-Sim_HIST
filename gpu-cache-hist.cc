#include "gpu-cache.h"
#define MAX_INT 1<<31

HIST_table::HIST_table( cache_config &config, int core_id ): m_config(config)
{
    m_core_id       = core_id;
    m_hist_assoc    = config.get_m_hist_assoc();
    m_hist_nset     = config.get_m_hist_nset();
    m_hist_HI_width = config.get_m_hist_HI_width();
    n_simt_clusters = config.get_n_simt_clusters();
    m_hist_entries  = new hist_entry_t[m_hist_assoc*m_hist_nset];
    
    printf("==HIST== SM[%3d] HIST table: %u ways x %u sets = %u entries (%u width / %u)\n", 
            core_id, m_hist_assoc, m_hist_nset, m_hist_assoc*m_hist_nset, m_hist_HI_width, n_simt_clusters);
}

enum hist_request_status HIST_table::probe( new_addr_type addr, unsigned &idx ) const 
{
    unsigned set_index = m_config.set_index_hist(addr); // Pisacha: Index HIST from address
    new_addr_type  tag = m_config.key_hist(addr);       // Pisacha: HIST Key from address (Tag)

    unsigned invalid_line    = (unsigned)-1;    // Pisacha: This is MAX INT
    unsigned valid_line      = (unsigned)-1;    // Pisacha: This is MAX INT
    unsigned valid_timestamp = (unsigned)-1;    // Pisacha: This is MAX INT

    bool all_reserved = true;

    // check for hit or pending hit
    for (unsigned way=0; way<m_hist_assoc; way++)
    {
        unsigned     index = set_index*m_hist_assoc + way;  // Pisacha: Each line in set, increasing by way++
        hist_entry_t *line = &m_hist_entries[index];        // Pisacha: Get an entry from calculated index

        // Pisacha: Looking for the matched key, otherwise skip!
        // When hit, it returns immediately and done.
        if (line->m_key == tag) {
            if ( line->m_status == HIST_WAIT ) {
                idx = index;
                return HIST_HIT_WAIT;
            } else if ( line->m_status == HIST_NOT_WAIT ) {
                idx = index;
                return HIST_HIT_NOT_WAIT;
            } else {
                assert( line->m_status == HIST_INVALID );
            }
        }

        // Pisacha: If it does not match, we look for invalid line
        if (line->m_status == HIST_INVALID) {   
            invalid_line = index;   // Pisacha: Remember invalid line
        } 
        else if (line->m_status == HIST_NOT_WAIT)   // Pisacha: Remember NOT_WAIT line
        {
            if ( line->m_last_access_time < valid_timestamp ) {
                valid_timestamp = line->m_last_access_time;
                valid_line = index; 
            }
        }
    }

    if ( invalid_line != (unsigned)-1 ) {
        idx = invalid_line;
    } else if ( valid_line != (unsigned)-1) {
        idx = valid_line;
    } else {    // Pisacha: Both invalid_line (free) line and valid_line are not set, it's full.
        return HIST_FULL;
    }

    return HIST_MISS;
}

int HIST_table::hist_home_distance(int target_id)
{
    int i, home;
    for(i=-(int)m_hist_HI_width; i<=(int)m_hist_HI_width; i++)
    {
        home = (m_core_id + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        if(home == target_id)
            return i;
    }
    return MAX_INT;     // Pisacha: Too far away
}

int HIST_table::hist_home_abDistance(int target_id)
{
    int i, home;
    for(i=-(int)m_hist_HI_width; i<=(int)m_hist_HI_width; i++)
    {
        home = (m_core_id + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        if(home == target_id && i >= 0)
            return i;
        if(home == target_id && i < 0)
            return -i;
    }
    return MAX_INT;     // Pisacha: Too far away
}

void HIST_table::allocate( new_addr_type addr, unsigned idx, unsigned time )
{
    new_addr_type  key = m_config.key_hist( addr );
    m_hist_entries[idx].allocate( HIST_WAIT, key, time );
}

void HIST_table::add( unsigned idx, int distance, unsigned time )
{
    unsigned add_HI = 1 << (distance + m_hist_HI_width);
    m_hist_entries[idx].m_HI = m_hist_entries[idx].m_HI | add_HI;
    m_hist_entries[idx].m_last_access_time = time;
    printf("==HIST== Index %u\n", idx);
}

void HIST_table::print()
{
    for(unsigned i=0; i < m_hist_assoc*m_hist_nset; i++)
    {
        printf("==HIST== %3u ", i);
        m_hist_entries[i].print();
    }
}
