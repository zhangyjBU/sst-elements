// Copyright 2009-2016 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2016, Sandia Corporation
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.


#ifndef _H_SST_MEMH_TIMING_PAGEPOLICY
#define _H_SST_MEMH_TIMING_PAGEPOLICY

#include <sst/core/subcomponent.h>

namespace SST {
namespace MemHierarchy {
namespace TimingDRAM_NS {

class PagePolicy : public SST::SubComponent {
  public:
    PagePolicy( Component* owner, Params& params ) : SubComponent( owner)  { }
    virtual bool shouldClose( SimTime_t current ) = 0;
};

class SimplePagePolicy : public PagePolicy {
  public:
    SimplePagePolicy( Component* owner, Params& params ) : PagePolicy( owner, params )  {
        m_close = params.find<bool>("close", true);
    }

    bool shouldClose( SimTime_t current ) {
        return m_close;
    }
  protected:
    bool m_close;
};

class TimeoutPagePolicy : public PagePolicy {
  public:
    TimeoutPagePolicy( Component* owner, Params& params ) : PagePolicy( owner, params ),
        m_numCyclesLeft(0), m_lastCycle(-2) { 
        m_cycles = params.find<SimTime_t>("timeoutCycles", 5);
    }

    bool shouldClose( SimTime_t current ) {

        if ( 0 == m_numCyclesLeft && current != m_lastCycle + 1 ) {
            m_numCyclesLeft = m_cycles + 1;
        }
        --m_numCyclesLeft;
        m_lastCycle = current;

        return (m_numCyclesLeft == 0);
    }

  protected:
    SimTime_t m_lastCycle;
    SimTime_t m_cycles;
    SimTime_t m_numCyclesLeft;
};

}
}
}

#endif
