#pragma once

#include <vector>
#include <cstdint>
#include "../WaylandProtocol.hpp"
#include "wayland.hpp"
#include "../../helpers/signal/Signal.hpp"
#include "../../managers/eventLoop/EventLoopTimer.hpp"

class CWLOutputProtocol;

class CWLOutputResource {
  public:
    CWLOutputResource(SP<CWlOutput> resource_, PHLMONITOR pMonitor, WP<CWLOutputProtocol> owner);
    static SP<CWLOutputResource> fromResource(wl_resource*);

    bool                         good();
    wl_client*                   client();
    SP<CWlOutput>                getResource();
    void                         updateState();

    PHLMONITORREF                m_monitor;
    WP<CWLOutputProtocol>        m_owner;
    WP<CWLOutputResource>        m_self;

  private:
    SP<CWlOutput> m_resource;
    wl_client*    m_client = nullptr;

    friend class CWLOutputProtocol;
};

class CWLOutputProtocol : public IWaylandProtocol {
  public:
    CWLOutputProtocol(const wl_interface* iface, const int& ver, const std::string& name, PHLMONITOR pMonitor);

    virtual void                       bindManager(wl_client* client, void* data, uint32_t ver, uint32_t id);

    std::vector<SP<CWLOutputResource>> outputResourcesFrom(wl_client* client);
    void                               sendDone();

    PHLMONITORREF                      m_monitor;
    WP<CWLOutputProtocol>              m_self;

    // will mark the protocol for removal, will be removed once inUse() turns false (or when overwritten by a new global)
    void remove();
    bool isDefunct(); // true if above was called
    bool inUse();     // true if a client holds a wl_output resource of ours, or may still be binding one

    struct {
        CSignalT<SP<CWLOutputResource>> outputBound;
    } m_events;

  private:
    void destroyResource(CWLOutputResource* resource);
    void destroyIfUnused();

    //
    std::vector<SP<CWLOutputResource>> m_outputs;
    bool                               m_defunct = false;
    std::string                        m_name    = "";
    SP<CEventLoopTimer>                m_bindGraceTimer; // set while late binds may still arrive

    struct {
        CHyprSignalListener modeChanged;
    } m_listeners;

    friend class CWLOutputResource;
};

namespace PROTO {
    inline std::unordered_map<std::string, SP<CWLOutputProtocol>> outputs;
    // defunct globals that are gone but not yet unref'd by all clients. Destroying their
    // still-bound resources server-side would fatally error the clients ("invalid object"
    // on their in-flight wl_output.release), and destroying the global would do the same to
    // clients whose bind is in flight; they are kept here until inUse() turns false.
    inline std::vector<SP<CWLOutputProtocol>> defunctOutputs;
};
