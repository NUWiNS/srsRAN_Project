/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#pragma once

#include "srsgnb/asn1/rrc_nr/rrc_nr.h"
#include "srsgnb/cu_cp/ue_context.h"
#include "srsgnb/rrc/rrc_cell_context.h"

namespace srsgnb {

namespace srs_cu_cp {

struct drb_context {
  srsgnb::drb_id_t         drb_id = drb_id_t::invalid;
  asn1::rrc_nr::pdcp_cfg_s pdcp_cfg;
  asn1::rrc_nr::sdap_cfg_s sdap_cfg;
};

/// Holds the RRC UE context used by the UE object and all its procedures.
class rrc_ue_context_t
{
public:
  rrc_ue_context_t(const ue_index_t       ue_index_,
                   const rnti_t           c_rnti_,
                   const rrc_cell_context cell_,
                   const rrc_ue_cfg_t&    cfg_) :
    ue_index(ue_index_), c_rnti(c_rnti_), cell(cell_), cfg(cfg_)
  {
  }
  const ue_index_t                       ue_index; // UE index assigned by the DU processor
  const rnti_t                           c_rnti;   // current C-RNTI
  const rrc_cell_context                 cell;     // current cell
  const rrc_ue_cfg_t&                    cfg;
  std::vector<drb_context>               drbs;
  guami                                  current_guami; // current GUAMI
  uint64_t                               setup_ue_id = -1;
  asn1::rrc_nr::establishment_cause_opts connection_cause;
};

} // namespace srs_cu_cp

} // namespace srsgnb
