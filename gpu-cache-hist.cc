#include <stdio.h>
#include <math.h> 
#include "gpu-sim.h"
#include "gpu-misc.h"

#define MAX_INT 1<<30

HIST_table::HIST_table( unsigned set, unsigned assoc, unsigned range, unsigned delay, unsigned n_sm, cache_config &config, gpgpu_sim *gpu ): 
                        m_hist_nset(set), m_hist_assoc(assoc), m_hist_range(range), m_hist_delay(delay), n_total_sm(n_sm),
                        m_line_sz_log2(LOGB2(config.get_line_sz())), m_hist_nset_log2(LOGB2(set)),
                        m_cache_config(config), m_gpu(gpu)
{
    recv_mf = new std::list<mem_fetch*>[n_sm];
    srcn_mf = new std::list<mem_fetch*>[n_sm];
    
    m_hist_table = new hist_entry_t*[n_sm];
    for( unsigned i=0; i<n_sm; i++ ){
        m_hist_table[i] = new hist_entry_t[set*assoc];
        for( unsigned j=0; j<set*assoc; j++ ){
            m_hist_table[i][j].filtered_mf = new std::list<mem_fetch*>[n_sm];
        }
    }

    n_sm_sqrt = sqrt(n_sm);
    if( n_sm_sqrt*n_sm_sqrt < n_sm ){
        n_sm_sqrt++;
    }
}

void HIST_table::print_config() const
{
    printf("==HIST: HIST Table configuration\n");
    printf("    ==HIST: Set   %u\n", m_hist_nset);
    printf("    ==HIST: Assoc %u\n", m_hist_assoc);
    printf("    ==HIST: Range %u\n", m_hist_range);
    printf("    ==HIST: Total %u\n", n_total_sm);
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
    return get_key(addr) % n_total_sm;
    //return 0;
}

int HIST_table::AB( int number ) const
{
    return number<0? -number:number;
}

int HIST_table::MIN( int num1, int num2 ) const
{
    return num1<=num2? num1:num2;
}

int HIST_table::MAX( int num1, int num2 ) const
{
    return num1>=num2? num1:num2;
}

unsigned HIST_table::NOC_distance( int SM_A, int SM_B ) const
{
    int distance, dX, dY, dXT, dYT; 
    int X_A = SM_A % n_sm_sqrt;
    int X_B = SM_B % n_sm_sqrt;
    int Y_A = SM_A / n_sm_sqrt;
    int Y_B = SM_B / n_sm_sqrt;
    
    dX  = AB( X_A - X_B );
    dY  = AB( Y_A - Y_B );
    dXT = AB( MIN(X_A, X_B) + n_sm_sqrt - MAX(X_A, X_B) );
    dYT = AB( MIN(Y_A, Y_B) + n_sm_sqrt - MAX(Y_A, Y_B) );
    
    distance = MIN( dX, dXT ) + MIN( dY, dYT );
    //printf("==HIST: %d(%d,%d) - %d(%d,%d) = %d\n", SM_A, X_A, Y_A, SM_B, X_B, Y_B, dis);
    return distance;
}

bool HIST_table::check_in_range( int miss_SM, int home ) const
{
    unsigned current_distance, home_distance, SM, counter=0;

    for( current_distance=0; current_distance <= (n_sm_sqrt*2); current_distance++ ){
        for( SM = 0; SM < n_total_sm; SM++ )
        {
            home_distance = NOC_distance( SM, home );
            if( home_distance == current_distance )
            {
                counter++;
                if( SM == miss_SM ){
                    return true;
                }
                if( counter >= m_hist_range ){
                    return false;
                }
            }
        }
    }
    return false;
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
/*
int HIST_table::hist_distance(int miss_core_id, new_addr_type addr) const
{
    unsigned home = get_home( addr );
    unsigned tmp_home;

    for(int i = -(int)m_hist_HI_width; i <= (int)m_hist_HI_width; i++)
    {
        tmp_home = (home + (int)n_total_sm + i) % (int)n_total_sm;
        if(tmp_home == (unsigned)miss_core_id)
            return i;
    }
    return MAX_INT; // Pisacha: Too far away
}

int HIST_table::hist_abDistance(int miss_core_id, new_addr_type addr) const
{
    return AB(hist_distance(miss_core_id, addr));
}
*/
void HIST_table::allocate( int miss_core_id, new_addr_type addr, unsigned time )
{
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned tag  = get_key( addr );

    assert( probe( addr, idx ) == HIST_MISS );
    assert( NOC_distance( miss_core_id, home ) <= m_hist_range );

    m_hist_table[home][idx].allocate( tag, time );
}

void HIST_table::add( int miss_core_id, new_addr_type addr, unsigned time )
{
    enum hist_request_status probe_res;
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned add_HI = 1 << miss_core_id;

    probe_res = probe( addr, idx );
    assert( probe_res == HIST_HIT_WAIT || probe_res == HIST_HIT_READY );
    assert( NOC_distance( miss_core_id, home ) <= m_hist_range );

    m_hist_table[home][idx].m_HI = m_hist_table[home][idx].m_HI | add_HI;
    m_hist_table[home][idx].m_last_access_time = time;
}

void HIST_table::del( int miss_core_id, new_addr_type addr )
{
    unsigned idx;
    unsigned home = get_home( addr );
    unsigned del_HI = 1 << miss_core_id;
    enum hist_request_status probe_res = probe( addr, idx );

    if( NOC_distance( miss_core_id, home ) > m_hist_range ){
        return;
    }
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
    assert( NOC_distance( miss_core_id, home ) <= m_hist_range );

    m_hist_table[home][idx].m_status = HIST_READY;
    m_hist_table[home][idx].m_last_access_time = time;
}

void HIST_table::add_mf( int miss_core_id, new_addr_type addr, mem_fetch *mf )
{
    unsigned idx;
    unsigned home = get_home( addr );

    assert( probe( addr, idx ) == HIST_HIT_WAIT );
    assert( NOC_distance( miss_core_id, home ) <= m_hist_range );

    m_hist_table[home][idx].filtered_mf[miss_core_id].push_back( mf );
}

void HIST_table::probe_dest( int miss_core_id, new_addr_type addr, mem_fetch *mf )
{
    //std::list<mem_fetch*> *miss_queue = mf->get_miss_queue();
    //miss_queue->push_back(mf);
    //m_gpu->fill_respond_queue( mf->get_sid(), mf );
    recv_mf[get_home(addr)].push_back( mf );
    mf->set_wait( 0 );
}

void HIST_table::process_probe( int miss_core_id, mem_fetch *mf )
{
    std::list<mem_fetch*> *miss_queue = mf->get_miss_queue();
    new_addr_type addr = mf->get_addr();
    
    unsigned home  = get_home( addr );
    unsigned NOC_d = NOC_distance( miss_core_id, home );
    enum hist_request_status probe_res = probe( addr );
    
    if( check_in_range( miss_core_id, home ) ){
        if( probe_res == HIST_MISS ){
            //std::cout << "HIST_MISS\n";
            allocate( miss_core_id, addr, mf->get_time() );
            add( miss_core_id, addr, mf->get_time() );
            
            miss_queue->push_back( mf );
            hist_ctr_MISS++;
        }
        else if( probe_res == HIST_HIT_WAIT ){
            //std::cout << "HIST_HIT_WAIT\n";
            add( miss_core_id, addr, mf->get_time() );
            add_mf( miss_core_id, addr, mf );
            
            hist_ctr_WAIT++;
        }
        else if( probe_res == HIST_HIT_READY ){
            //std::cout << "HIST_HIT_READY\n";
            add( miss_core_id, addr, mf->get_time() );
            
            recv_mf[miss_core_id].push_back( mf );
            mf->set_wait( m_hist_delay + NOC_d );
            hist_ctr_READY++;
        }
        else{
            assert( probe_res == HIST_FULL );
            miss_queue->push_back( mf );
            hist_ctr_FULL++;
        }
    }
    else{
        if( probe_res == HIST_HIT_READY ){
            recv_mf[miss_core_id].push_back( mf );
            mf->set_wait( m_hist_delay + NOC_d );
            hist_ctr_GPROBE_S++;
        }
        else{
            miss_queue->push_back( mf );
            hist_ctr_GPROBE_F++;
        }
    }
}

void HIST_table::recv_cycle( int core_id )
{
    std::list<mem_fetch*>::iterator it     = recv_mf[core_id].begin();
    std::list<mem_fetch*>::iterator it_min = recv_mf[core_id].end();
    int min_cycle = MAX_INT;
    
    while( it != recv_mf[core_id].end() ){
        mem_fetch *mf_ptr = *it;
        if( min_cycle > mf_ptr->get_wait() && mf_ptr->get_wait() <= m_hist_delay ){
            min_cycle = mf_ptr->get_wait();
            it_min = it;
        }
        if( mf_ptr->get_wait() > m_hist_delay ){
            mf_ptr->hist_cycle();
        }
        it++;
    }
    
    if( it_min != recv_mf[core_id].end() ){
        mem_fetch *mf_ptr = *it_min;
        
        new_addr_type addr = mf_ptr->get_addr();
        enum hist_request_status probe_res = probe( addr );
        std::list<mem_fetch*> *miss_queue = mf_ptr->get_miss_queue();
        
        if( min_cycle == 0 ){
            assert( mf_ptr->get_wait() == 0 );
            process_probe( mf_ptr->get_sid(), mf_ptr );
            recv_mf[core_id].erase( it_min );
        }
        else if( min_cycle == 1 ){
            assert( mf_ptr->get_wait() == 1 );
            assert( mf_ptr->get_sid() == core_id );
            if( probe_res == HIST_HIT_READY ){
                m_gpu->fill_respond_queue( core_id, mf_ptr );
            }
            else{
                miss_queue->push_back( mf_ptr );
                hist_ctr_FREADY++;
            }
            recv_mf[core_id].erase( it_min );
        }
        else{
            assert( mf_ptr->get_wait() > 1 );
            mf_ptr->hist_cycle();
        }
    }
}

void HIST_table::fill_wait( int miss_core_id, new_addr_type addr )
{
    unsigned idx, SM;
    unsigned home = get_home( addr );
    enum hist_request_status probe_res = probe( addr, idx );

    assert( probe_res == HIST_HIT_READY );
    assert( NOC_distance( miss_core_id, home ) <= m_hist_range );

    for( SM = 0; SM < n_total_sm; SM++ ){
        while( m_hist_table[home][idx].filtered_mf[SM].size() > 0 )
        {
            mem_fetch *pending_mf = m_hist_table[home][idx].filtered_mf[SM].front();
            
            recv_mf[SM].push_back( pending_mf );
            pending_mf->set_wait( m_hist_delay + NOC_distance( miss_core_id, home ) );
            
            m_hist_table[home][idx].filtered_mf[SM].pop_front();
        }
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
