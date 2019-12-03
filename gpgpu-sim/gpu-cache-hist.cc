#include "gpu-cache.h"

HIST_table::HIST_table( cache_config &config, int core_id )
{
    printf("==HIST== SM[%3d] HIST table: %u ways x %u sets = %u entries\n", core_id, config.get_m_hist_assoc(), config.get_m_hist_nset(), config.get_m_hist_assoc()*config.get_m_hist_nset());
    m_hist_entries = new hist_entry_t[config.get_m_hist_assoc()*config.get_m_hist_nset()];
    m_core_id = core_id;
}
