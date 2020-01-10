#include<stdio.h>
#include "gpu-misc.h"
#include "gpu-cache-hist.h"
#define MAX_INT 1<<30

HIST_table::HIST_table( unsigned set, unsigned assoc, unsigned width, unsigned n_simt, cache_config &config ): 
                        m_hist_nset(set), m_hist_assoc(assoc), m_hist_HI_width(width), n_simt_clusters(n_simt), 
                        m_line_sz_log2(LOGB2(config.get_line_sz())), m_hist_nset_log2(LOGB2(set)),
                        m_cache_config(config)
{
    m_hist_table = new hist_entry_t*[n_simt];
    for( unsigned i=0; i<n_simt ; i++){
        m_hist_table[i] = new hist_entry_t[set*assoc];
    }
}

void HIST_table::print_config() const
{
    printf("==HIST: HIST Table configuration\n");
    printf("    ==HIST: Set   %u\n", m_hist_nset);
    printf("    ==HIST: Assoc %u\n", m_hist_assoc);
    printf("    ==HIST: Width %u\n", m_hist_HI_width);
    printf("    ==HIST: Total %u\n", n_simt_clusters);
    printf("    ==HIST: line_log2 %u\n", m_line_sz_log2);
    printf("    ==HIST: nset_log2 %u\n", m_hist_nset_log2);
}

new_addr_type HIST_table::get_key(new_addr_type addr) const
{
    return addr >> (m_line_sz_log2 + m_hist_nset_log2);
}

unsigned HIST_table::get_set_idx(new_addr_type addr) const
{
    return (addr >> m_line_sz_log2) & (m_hist_nset-1);
}

unsigned HIST_table::get_home(new_addr_type addr) const
{
    return get_key(addr) % n_simt_clusters;
}

enum hist_request_status HIST_table::probe( new_addr_type addr ) const 
{
    unsigned tmp_idx;
    return probe( addr, tmp_idx );
}

enum hist_request_status HIST_table::probe( new_addr_type addr, unsigned &idx ) const 
{
    unsigned home      = get_home( addr );      // Pisacha: get HOME from address
    unsigned set_index = get_set_idx( addr );   // Pisacha: Index HIST from address
    unsigned tag       = get_key( addr );       // Pisacha: HIST Key from address (Tag)

    unsigned invalid_line    = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED
    unsigned valid_line      = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED
    unsigned valid_timestamp = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED

    // check for hit or pending hit
    for (unsigned way=0; way<m_hist_assoc; way++)
    {
        unsigned     index = set_index*m_hist_assoc + way;  // Pisacha: Each line in set, increasing by way++
        hist_entry_t *line = &m_hist_table[home][index];    // Pisacha: Get an entry from calculated index

        // Pisacha: Looking for the matched key, otherwise skip!
        // When hit, it returns immediately and done.
        if (line->m_key == tag) {
            if ( line->m_status == HIST_WAIT ) {
                idx = index;
                return HIST_HIT_WAIT;
            } else if ( line->m_status == HIST_READY ) {
                idx = index;
                return HIST_HIT_READY;
            } else {
                assert( line->m_status == HIST_INVALID );
            }
        }

        // Pisacha: If it does not match, we look for invalid line
        if (line->m_status == HIST_INVALID) {   
            invalid_line = index;   // Pisacha: Remember invalid line
        } 
        else if (line->m_status == HIST_HIT_READY)   // Pisacha: Remember READY line
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

int HIST_table::hist_home_distance(int miss_core_id, new_addr_type addr) const
{
    unsigned home = get_home( addr );
    unsigned tmp_home;

    for(int i = -(int)m_hist_HI_width; i <= (int)m_hist_HI_width; i++)
    {
        tmp_home = (miss_core_id + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        if(home == tmp_home)
            return i;
    }
    return MAX_INT; // Pisacha: Too far away
}

int HIST_table::hist_home_abDistance(int miss_core_id, new_addr_type addr) const
{
    int distance = hist_home_distance( miss_core_id, addr );
    if( distance >= 0 ){
        return distance;
    }
    return -distance;
}


void HIST_table::allocate( new_addr_type addr, unsigned time )
{
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned tag  = get_key( addr );
    enum hist_request_status probe_res;
    
    probe_res = probe( addr, idx );
    assert( probe_res == HIST_MISS );
    m_hist_table[home][idx].allocate( tag, time );
}

void HIST_table::add( int miss_core_id, new_addr_type addr, unsigned time )
{
    enum hist_request_status probe_res;
    int distance = hist_home_distance( miss_core_id, addr );

    unsigned idx;
    unsigned home = get_home( addr );
    unsigned add_HI = 1 << (distance + m_hist_HI_width);

    probe_res = probe( addr, idx );
    assert( probe_res == HIST_HIT_WAIT || probe_res == HIST_HIT_READY );

    m_hist_table[home][idx].m_HI = m_hist_table[home][idx].m_HI | add_HI;
    m_hist_table[home][idx].m_last_access_time = time;
}

void HIST_table::ready( new_addr_type addr, unsigned time )
{
    enum hist_request_status probe_res;
    unsigned home = get_home( addr );
    unsigned idx;

    probe_res = probe( addr, idx );
    assert( probe_res == HIST_HIT_WAIT );

    m_hist_table[home][idx].m_status = HIST_READY;
    m_hist_table[home][idx].m_last_access_time = time;
}


void HIST_table::print_table( new_addr_type addr ) const
{
    unsigned home = get_home( addr );
    for(unsigned i=0; i < m_hist_assoc*m_hist_nset; i++)
    {
        printf("==HIST %3u ", i);
        m_hist_table[home][i].print();
    }
}
