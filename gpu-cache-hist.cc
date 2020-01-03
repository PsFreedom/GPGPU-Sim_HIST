#include<stdio.h>
#include "gpu-misc.h"
#include "gpu-cache-hist.h"
#define MAX_INT 8888888

HIST_table::HIST_table( unsigned set, unsigned assoc, unsigned width, int core_id, unsigned n_simt, cache_config &config ): 
                        m_hist_nset(set), m_hist_assoc(assoc), m_hist_HI_width(width), 
                        m_core_id(core_id), n_simt_clusters(n_simt),
                        m_hist_nset_log2(LOGB2(set)), m_line_sz_log2(LOGB2(config.get_line_sz())),
                        m_cache_config(config)
{
    m_hist_entries  = new hist_entry_t[set*assoc];
}

void HIST_table::print_config()
{
    printf("==HIST: HIST Table configuration\n");
    printf("    ==HIST: ID    %d\n", m_core_id);
    printf("    ==HIST: Set   %u\n", m_hist_nset);
    printf("    ==HIST: Assoc %u\n", m_hist_assoc);
    printf("    ==HIST: Width %u\n", m_hist_HI_width);
    printf("    ==HIST: Total %u\n", n_simt_clusters);
    printf("    ==HIST: line_log2 %u\n", m_line_sz_log2);
    printf("    ==HIST: nset_log2 %u\n", m_hist_nset_log2);
}

new_addr_type HIST_table::get_key(new_addr_type addr)
{
    return addr >> (m_line_sz_log2 + m_hist_nset_log2);
}

unsigned HIST_table::get_set_idx(new_addr_type addr)
{
    return (addr >> m_line_sz_log2) & (m_hist_nset-1);
}

unsigned HIST_table::get_home(new_addr_type addr)
{
    return get_key(addr) % n_simt_clusters;
}

/*
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
*/