#include "gpu-cache.h"

HIST_table::HIST_table( cache_config &config, int core_id )
{
    printf("==HIST== SM[%3d] m_nhist_entry: %u created\n", core_id, config.get_m_nhist_entry());
    m_hist_entries = new hist_entry_t[config.get_m_nhist_entry()];
    m_core_id = core_id;
}
