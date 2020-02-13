#include <stdio.h>
#include <math.h> 
#include "gpu-sim.h"
#include "gpu-misc.h"

#define MAX_INT 1<<30

HIST_table::HIST_table( unsigned set, unsigned assoc, unsigned width, unsigned n_simt, cache_config &config, gpgpu_sim *gpu ): 
                        m_hist_nset(set), m_hist_assoc(assoc), m_hist_HI_width(width), n_simt_clusters(n_simt),
                        m_line_sz_log2(LOGB2(config.get_line_sz())), m_hist_nset_log2(LOGB2(set)),
                        m_cache_config(config), m_gpu(gpu)
{
    m_hist_table = new hist_entry_t*[n_simt];
    for( unsigned i=0; i<n_simt; i++ ){
        m_hist_table[i] = new hist_entry_t[set*assoc];
        for( unsigned j=0; j<set*assoc; j++ ){
            m_hist_table[i][j].filtered_mf = new std::list<mem_fetch*>[width + width + 1];
        }
    }
    
    recv_mf = new std::list<mem_fetch*>[n_simt];
    
    n_simt_sqrt = sqrt(n_simt);
    if( n_simt_sqrt*n_simt_sqrt < n_simt ){
        n_simt_sqrt++;
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
    //return 0;
}

enum hist_request_status HIST_table::probe( new_addr_type addr ) const 
{
    unsigned tmp_idx;
    return probe( addr, tmp_idx );
}

int HIST_table::AB( int number ) const
{
    return number<0? -number:number;
}

unsigned HIST_table::NOC_distance( int SM_A, int SM_B ) const
{
    int X_A = SM_A % n_simt_sqrt;
    int X_B = SM_B % n_simt_sqrt;
    int Y_A = SM_A / n_simt_sqrt;
    int Y_B = SM_B / n_simt_sqrt;
    int dis = AB(X_B-X_A) + AB(Y_B-Y_A);
    
    //printf("==HIST: %d(%d,%d) - %d(%d,%d) = %d\n", SM_A, X_A, Y_A, SM_B, X_B, Y_B, dis);
    return dis;
}

enum hist_request_status HIST_table::probe( new_addr_type addr, unsigned &idx ) const 
{
    unsigned home      = get_home( addr );      // Pisacha: get HOME from address
    unsigned set_index = get_set_idx( addr );   // Pisacha: Index HIST from address
    unsigned tag       = get_key( addr );       // Pisacha: HIST Key from address (Tag)

    unsigned invalid_line    = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED
    unsigned valid_line      = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED
    unsigned valid_timestamp = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED
    unsigned valid_count     = (unsigned)-1;    // Pisacha: This is MAX UNSIGNED

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
        else if (line->m_status == HIST_READY)   // Pisacha: Remember READY line
        {
            if ( line->count() < valid_count && line->count() < 2 ) {
                valid_timestamp = line->m_last_access_time;
                valid_count = line->count();
                valid_line = index; 
            }
            else if( line->count() == valid_count && line->m_last_access_time < valid_timestamp && line->count() < 2 ){
                valid_timestamp = line->m_last_access_time;
                valid_count = line->count();
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

int HIST_table::hist_distance(int miss_core_id, new_addr_type addr) const
{
    unsigned home = get_home( addr );
    unsigned tmp_home;

    for(int i = -(int)m_hist_HI_width; i <= (int)m_hist_HI_width; i++)
    {
        tmp_home = (home + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        if(tmp_home == (unsigned)miss_core_id)
            return i;
    }
    return MAX_INT; // Pisacha: Too far away
}

int HIST_table::hist_abDistance(int miss_core_id, new_addr_type addr) const
{
    return AB(hist_distance(miss_core_id, addr));
}

void HIST_table::allocate( int miss_core_id, new_addr_type addr, unsigned time )
{
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned tag  = get_key( addr );

    assert( probe( addr, idx ) == HIST_MISS );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );

    m_hist_table[home][idx].allocate( tag, time );
}

void HIST_table::add( int miss_core_id, new_addr_type addr, unsigned time )
{
    enum hist_request_status probe_res;
    int distance = hist_distance( miss_core_id, addr );

    unsigned idx;
    unsigned home = get_home( addr );
    unsigned add_HI = 1 << (distance + m_hist_HI_width);

    probe_res = probe( addr, idx );
    assert( probe_res == HIST_HIT_WAIT || probe_res == HIST_HIT_READY );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );

    m_hist_table[home][idx].m_HI = m_hist_table[home][idx].m_HI | add_HI;
    m_hist_table[home][idx].m_last_access_time = time;
}

void HIST_table::del( int miss_core_id, new_addr_type addr, unsigned time )
{
    int distance = hist_distance( miss_core_id, addr );
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned del_HI = 1 << (distance + m_hist_HI_width);
    enum hist_request_status probe_res = probe( addr, idx );

    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );
    if( probe_res == HIST_MISS || probe_res ==  HIST_FULL ){
        return;
    }

    m_hist_table[home][idx].m_HI = m_hist_table[home][idx].m_HI & (~del_HI);
    if( m_hist_table[home][idx].count() == 0 ){
        m_hist_table[home][idx].m_status = HIST_INVALID;
        //printf("==HIST_clr: Clear to INVALID\n");
    }
}

void HIST_table::ready( int miss_core_id, new_addr_type addr, unsigned time )
{
    unsigned idx;
    unsigned home = get_home( addr );

    assert( probe( addr, idx ) == HIST_HIT_WAIT );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );

    m_hist_table[home][idx].m_status = HIST_READY;
    m_hist_table[home][idx].m_last_access_time = time;
}

void HIST_table::add_mf( int miss_core_id, new_addr_type addr, mem_fetch *mf )
{
    unsigned idx;
    unsigned home = get_home( addr );

    assert( probe( addr, idx ) == HIST_HIT_WAIT );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );

    m_hist_table[home][idx].filtered_mf[hist_distance(miss_core_id, addr) + (int)m_hist_HI_width].push_back( mf );
}

void HIST_table::fill_wait( int miss_core_id, new_addr_type addr )
{
    int SM;
    unsigned idx;
    unsigned home = get_home( addr );
    enum hist_request_status probe_res = probe( addr, idx );

    assert( probe_res == HIST_HIT_READY );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );

    for( int i = -(int)m_hist_HI_width; i <= (int)m_hist_HI_width; i++ )
    {
        SM = ((int)home + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        while( m_hist_table[home][idx].filtered_mf[i+(int)m_hist_HI_width].size() > 0 )
        {
            mem_fetch *pending_mf = m_hist_table[home][idx].filtered_mf[i+(int)m_hist_HI_width].front();
            m_gpu->fill_respond_queue( SM, pending_mf );
            m_hist_table[home][idx].filtered_mf[i+(int)m_hist_HI_width].pop_front();
        }
    }
}

void HIST_table::send_mf( int miss_core_id, new_addr_type addr, mem_fetch *mf )
{
    unsigned home = get_home( addr );
    assert( hist_abDistance( miss_core_id, addr ) <= (int)m_hist_HI_width );
    
    recv_mf[home].push_back( mf );
    mf->set_wait(NOC_distance(miss_core_id, home));
    //printf("==HIST: set_wait %u\n", mf->get_wait());
}

void HIST_table::process_mf( mem_fetch *mf )
{
    new_addr_type addr = mf->get_addr();
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned owner_sm = mf->get_sid();
    enum hist_request_status probe_res = probe( addr, idx );
    
    if( probe_res == HIST_MISS ){
        
    }
}

void HIST_table::hist_cycle()
{
    mem_fetch* mf;
    std::list<mem_fetch*>::iterator it;
    
    //std::cout << "==HIST: hist_cycle()\n";
    for( int i=0; i<n_simt_clusters; i++ ){
        //std::cout << "==HIST:     recv[" << i << "] - " << recv_mf[i].size() << " ( ";
        for( it = recv_mf[i].begin(); it != recv_mf[i].end(); it++){
            mf = *it;
            mf->hist_cyle();
            //std::cout << mf->get_wait() << " ";
        }
        //std::cout << ")\n";
    }
}

void HIST_table::hist_process_cycle()
{
    mem_fetch* mf;
    std::list<mem_fetch*>::iterator it;
    
    //std::cout << "==HIST: hist_cycle()\n";
    for( int i=0; i<n_simt_clusters; i++ ){
        //std::cout << "==HIST:     recv[" << i << "] - " << recv_mf[i].size() << " ( ";
        for( it = recv_mf[i].begin(); it != recv_mf[i].end(); it++){
            mf = *it;
            process_mf( mf );
            if( mf->get_wait() == 0 ){
                recv_mf[i].erase(it);
                break;
            }
            //std::cout << mf->get_wait() << " ";
        }
        //std::cout << ")\n";
    }
}

void HIST_table::print_recv_mf() const
{
    for( int i=0; i<n_simt_clusters; i++ ){
        std::cout << "==HIST:     recv[" << i << "] - " << recv_mf[i].size() << "\n";
    }
}

void HIST_table::print_wait( new_addr_type addr ) const
{
    int SM;
    unsigned idx, vec_bit;
    unsigned home = get_home( addr );
    enum hist_request_status probe_res = probe( addr, idx );
    unsigned HI = m_hist_table[home][idx].m_HI;

    assert( probe_res == HIST_HIT_WAIT );

    printf("==HIST Home %u Vector %#04x\n", home, HI);
    for( int i = -(int)m_hist_HI_width; i <= (int)m_hist_HI_width; i++ )
    {
        SM = ((int)home + (int)n_simt_clusters + i) % (int)n_simt_clusters;
        vec_bit = HI&0x1;
        printf("   ==HIST SM[%2d] %u", SM, vec_bit);
        printf(" | list -> %zu", m_hist_table[home][idx].filtered_mf[i+(int)m_hist_HI_width].size());
        printf("\n");
        HI = HI >> 1;
    }
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
