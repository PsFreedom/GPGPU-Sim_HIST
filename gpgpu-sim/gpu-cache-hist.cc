#include "gpu-cache.h"

HIST_table::HIST_table( cache_config &config, int core_id ): m_config(config)
{
    m_core_id       = core_id;
    m_hist_assoc    = config.get_m_hist_assoc();
    m_hist_nset     = config.get_m_hist_nset();
    m_hist_HI_width = config.get_m_hist_HI_width();
    m_hist_entries  = new hist_entry_t[m_hist_assoc*m_hist_nset];
    
    printf("==HIST== SM[%3d] HIST table: %u ways x %u sets = %u entries (%u width)\n", 
            core_id, m_hist_assoc, m_hist_nset, m_hist_assoc*m_hist_nset, m_hist_HI_width);
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
            if ( line->m_status == HIST_RESERVED ) {
                idx = index;
                return HIST_HIT_RESERVED;
            } else if ( line->m_status == HIST_WAIT ) {
                idx = index;
                return HIST_HIT_WAIT;
            } else if ( line->m_status == HIST_NOT_WAIT ) {
                idx = index;
                return HIST_HIT_NOT_WAIT;
            } else {
                assert( line->m_status == HIST_INVALID );
            }
        }

        // Pisacha: If it does not match, we keep looking for invalid line first
        if (line->m_status != HIST_RESERVED) {
            all_reserved = false;
            if (line->m_status == HIST_INVALID) {   // Pisacha: Remember invalid line
                invalid_line = index;
            } else {
                // valid line : keep track of most appropriate replacement candidate
                if ( line->m_last_access_time < valid_timestamp ) {
                    valid_timestamp = line->m_last_access_time;
                    valid_line = index;
                }
            }
        }
    }

    if ( all_reserved ) {
        return HIST_RESERVATION_FAIL; // miss and not enough space in cache to allocate on miss
    }

    if ( invalid_line != (unsigned)-1 ) {
        idx = invalid_line;
    } else if ( valid_line != (unsigned)-1) {
        idx = valid_line;
    } else abort(); // if an unreserved block exists, it is either invalid or replaceable 

    return HIST_MISS;
}