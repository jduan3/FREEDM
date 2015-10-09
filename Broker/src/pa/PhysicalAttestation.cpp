#include "PhysicalAttestation.hpp"

#include "CLogger.hpp"
#include "CTimings.hpp"
#include "CGlobalConfiguration.hpp"
#include "CDeviceManager.hpp"
#include "CGlobalPeerList.hpp"
#include "CPhysicalTopology.hpp"
#include "CDataManager.hpp"

#include <set>
#include <cmath>
#include <iterator>
#include <algorithm>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/system/error_code.hpp>
#include <boost/range/adaptor/map.hpp>

namespace freedm {
namespace broker {
namespace pa {

namespace {
CLocalLogger Logger(__FILE__);
}

PAAgent::PAAgent()
    : REQUEST_TIMEOUT(boost::posix_time::milliseconds(CTimings::Get("PA_REQUEST_TIMEOUT")))
    , HARDWARE_DELAY_MS(CTimings::Get("PA_HARDWARE_DELAY"))
    , ERROR_MARGIN(CGlobalConfiguration::Instance().GetAttestationTolerance())
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    m_RoundTimer = CBroker::Instance().AllocateTimer("pa");
    m_WaitTimer = CBroker::Instance().AllocateTimer("pa");
}

int PAAgent::Run()
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    CBroker::Instance().Schedule(m_RoundTimer, boost::posix_time::not_a_date_time,
        boost::bind(&PAAgent::RoundStart, this, boost::asio::placeholders::error));
    return 0;
}

void PAAgent::RoundStart(const boost::system::error_code & error)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;

    if(!error)
    {
        CBroker::Instance().Schedule(m_RoundTimer, boost::posix_time::not_a_date_time,
            boost::bind(&PAAgent::RoundStart, this, boost::asio::placeholders::error));
        CBroker::Instance().Schedule(m_WaitTimer, boost::posix_time::milliseconds(0),
            boost::bind(&PAAgent::RequestStates, this, boost::asio::placeholders::error));
    }
    else if(error == boost::asio::error::operation_aborted)
    {
        Logger.Notice << "Physical Attestation Aborted" << std::endl;
    }
    else
    {
        Logger.Error << error << std::endl;
        throw boost::system::system_error(error);
    }
}

void PAAgent::RequestStates(const boost::system::error_code & error)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;

    if(!error)
    {
        BOOST_FOREACH(Framework & f, m_frameworks)
        {
            if(f.invalid)
                continue;

            if(f.migration_states.size() != f.migration_members.size())
            {
                BOOST_FOREACH(std::string uuid, f.migration_members)
                {
                    if(f.migration_states.count(uuid) == 0)
                    {
                        // exception if peer node found
                        Logger.Info << "Requested state from " << uuid << " for time " << f.migration_time << std::endl;
                        CGlobalPeerList::instance().GetPeer(uuid).Send(MessageStateRequest(f.migration_time));
                    }
                }
            }

            device::CDevice::Pointer clock = device::CDeviceManager::Instance().GetClock();
            if(f.completion_members.empty() && clock && clock->GetState("time") >= f.completion_time)
            {
                try
                {
                    f.completion_members = BuildFramework(f.target, f.completion_time);
                }
                catch(std::exception & e)
                {
                    Logger.Notice << "Failed to build attestation framework for " << f.target << " at " << f.completion_time << std::endl;
                    f.invalid = true;
                }
            }

            if(f.completion_states.size() != f.completion_members.size())
            {
                BOOST_FOREACH(std::string uuid, f.completion_members)
                {
                    if(f.completion_states.count(uuid) == 0)
                    {
                        // exception if peer node found
                        Logger.Info << "Requested state from " << uuid << " for time " << f.completion_time << std::endl;
                        CGlobalPeerList::instance().GetPeer(uuid).Send(MessageStateRequest(f.completion_time));
                    }
                }
            }
        }

        m_responses.clear();
        m_ExpiredStates.clear();
        CBroker::Instance().Schedule(m_WaitTimer, REQUEST_TIMEOUT,
            boost::bind(&PAAgent::EvaluateFrameworks, this, boost::asio::placeholders::error));
    }
    else if(error == boost::asio::error::operation_aborted)
    {
        Logger.Notice << "Physical Attestation Aborted" << std::endl;
    }
    else
    {
        Logger.Error << error << std::endl;
        throw boost::system::system_error(error);
    }

}

void PAAgent::EvaluateFrameworks(const boost::system::error_code & error)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;

    if(!error)
    {
        BOOST_FOREACH(Framework & f, m_frameworks)
        {
            BOOST_FOREACH(std::string uuid, f.migration_members)
            {
                std::string key = uuid + "." + boost::lexical_cast<std::string>(f.migration_time);
                if(m_responses.count(key) > 0)
                {
                    Logger.Info << "Received state from " << uuid << " for time " << f.migration_time << std::endl;
                    f.migration_states[uuid] = m_responses[key];
                }
                else if(m_ExpiredStates.count(key) > 0 && f.migration_states.count(uuid) == 0)
                {
                    Logger.Notice << "Expired state for " << uuid << " for time " << f.migration_time << std::endl;
                    f.invalid = true;
                }
            }
            BOOST_FOREACH(std::string uuid, f.completion_members)
            {
                std::string key = uuid + "." + boost::lexical_cast<std::string>(f.completion_time);
                if(m_responses.count(key) > 0)
                {
                    Logger.Info << "Received state from " << uuid << " for time " << f.completion_time << std::endl;
                    f.completion_states[uuid] = m_responses[key];
                }
                else if(m_ExpiredStates.count(key) > 0 && f.completion_states.count(uuid) == 0)
                {
                    Logger.Notice << "Expired state for " << uuid << " for time " << f.completion_time << std::endl;
                    f.invalid = true;
                }
            }
        }
        std::list<Framework>::iterator it = m_frameworks.begin();
        while(it != m_frameworks.end())
        {
            if(it->invalid)
            {
                Logger.Status << "Deleted invalid framework for " << it->target << " at " << it->migration_time << std::endl;
                GetMe().Send(MessageAttestationFailure(it->target, it->expected_value));
                CPeerNode t = CGlobalPeerList::instance().GetPeer(it->target);
                t.Send(MessageAttestationFailure(GetUUID(), -it->expected_value));
                m_frameworks.erase(it++);
            }
            else if(it->migration_states.size() == it->migration_members.size() &&
                it->completion_states.size() == it->completion_members.size())
            {
                CalculateInvariant(*it);
                m_frameworks.erase(it++);
            }
            else
            {
                it++;
            }
        }
    }
    else if(error == boost::asio::error::operation_aborted)
    {
        Logger.Notice << "Physical Attestation Aborted" << std::endl;
    }
    else
    {
        Logger.Error << error << std::endl;
        throw boost::system::system_error(error);
    }
}

void PAAgent::CalculateInvariant(const Framework & framework)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    float before_power = SecurePowerFlow(framework.target, framework.migration_states);
    float after_power = SecurePowerFlow(framework.target, framework.completion_states);
    float actual_change = after_power - before_power;

    Logger.Info << "Physical Attestation Invariant Calculation\n"
        << "\tTarget UUID:       " << framework.target << "\n"
        << "\tExpected Change:   " << framework.expected_value << "\n"
        << "\tCalculated Change: " << after_power - before_power << "\n"
        << "\t-----------------------------------\n"
        << "\tTarget Power Flow Calculations\n"
        << "\tt=" << framework.migration_time << " with " << framework.migration_states.size() << " peers, P = " << before_power << "\n"
        << "\tt=" << framework.completion_time << " with " << framework.completion_states.size() << " peers, P = " << after_power << std::endl;

    if(std::abs(actual_change - framework.expected_value) > ERROR_MARGIN)
    {
        GetMe().Send(MessageAttestationFailure(framework.target, framework.expected_value));
    }
}

float PAAgent::SecurePowerFlow(std::string target, const std::map<std::string, StateResponseMessage> & data)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    bool all_true = true, one_hop_false = true, target_invariant;

    std::set<std::string> one_hop_inv;
    BOOST_FOREACH(std::string v, CPhysicalTopology::Instance().GetAdjacent(target))
    {
        if(data.count(v) > 0)
        {
            one_hop_inv.insert(v);
            Logger.Debug << "Added " << v << " as a 1-hop invariant" << std::endl;
        }
    }

    std::set<std::string> two_hop_inv;
    BOOST_FOREACH(std::string u, one_hop_inv)
    {
        BOOST_FOREACH(std::string v, CPhysicalTopology::Instance().GetAdjacent(u))
        {
            if(v != target && data.count(v) > 0 && one_hop_inv.count(v) == 0)
            {
                two_hop_inv.insert(v);
                Logger.Debug << "Added " << v << " as a 2-hop invariant" << std::endl;
            }
        }
    }

    BOOST_FOREACH(std::string u, data | boost::adaptors::map_keys)
    {
        if(u != target && one_hop_inv.count(u) == 0 && two_hop_inv.count(u) == 0)
            continue;
        Logger.Debug << "Calculating invariant located at " << u << std::endl;
        float total_power_flow = 0;
        BOOST_FOREACH(std::string v, CPhysicalTopology::Instance().GetAdjacent(u))
        {
            if(data.count(v) == 0)
                continue;
            total_power_flow += CalculateLineFlow(u, v, data);
        }
        Logger.Debug << "Line Power Sum: " << total_power_flow << "\nBus Generation: " << data.at(u).real_power() << std::endl;
        total_power_flow -= data.at(u).real_power();
        bool invariant = std::abs(total_power_flow) < ERROR_MARGIN;

        if(u == target)
        {
            target_invariant = invariant;

            if(!invariant)
                Logger.Info << "Invarviant violation at target " << u << std::endl;
        }
        else if(one_hop_inv.count(u) > 0)
        {
            all_true &= invariant;
            one_hop_false &= !invariant;

            if(!invariant)
                Logger.Info << "Invariant violation at 1-hop invariant " << u << std::endl;
        }
        else
        {
            all_true &= invariant;
            one_hop_false &= invariant;

            if(!invariant)
                Logger.Info << "Invariant violation at 2-hop invariant " << u << std::endl;
        }
    }

    // does this make sense?
    if(one_hop_false || (!target_invariant && all_true))
    {
        Logger.Notice << "Target " << target << " malicious, calculating correct power flow" << std::endl;
        return CalculateTargetPower(target, data);
    }
    else
    {
        return data.at(target).real_power();
    }
}

float PAAgent::CalculateLineFlow(std::string u, std::string v, const std::map<std::string, StateResponseMessage> & data)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    float X = CPhysicalTopology::Instance().GetReactance(u, v);
    float R = CPhysicalTopology::Instance().GetResistance(u, v);
    float three_phase_power = 0, term1, term2;

    term1 = R * (data.at(u).voltage1() - data.at(v).voltage1() * std::cos(data.at(u).phase1() - data.at(v).phase1()));
    term2 = X * data.at(v).voltage1() * std::sin(data.at(u).phase1() - data.at(v).phase1());
    three_phase_power += data.at(u).voltage1() / (X * X + R * R) * (term1 + term2);

    term1 = R * (data.at(u).voltage2() - data.at(v).voltage2() * std::cos(data.at(u).phase2() - data.at(v).phase2()));
    term2 = X * data.at(v).voltage2() * std::sin(data.at(u).phase2() - data.at(v).phase2());
    three_phase_power += data.at(u).voltage2() / (X * X + R * R) * (term1 + term2);

    term1 = R * (data.at(u).voltage3() - data.at(v).voltage3() * std::cos(data.at(u).phase3() - data.at(v).phase3()));
    term2 = X * data.at(v).voltage3() * std::sin(data.at(u).phase3() - data.at(v).phase3());
    three_phase_power += data.at(u).voltage3() / (X * X + R * R) * (term1 + term2);

    return three_phase_power;
}

float PAAgent::CalculateTargetPower(std::string target, const std::map<std::string, StateResponseMessage> & data)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    // all the signs are wrong!
    float calculated_real_power = 0;
    BOOST_FOREACH(std::string n, CPhysicalTopology::Instance().GetAdjacent(target))
    {
        if(data.count(n) == 0)
            continue;

        float target_line_power = 0;
        BOOST_FOREACH(std::string v, CPhysicalTopology::Instance().GetAdjacent(n))
        {
            if(data.count(v) == 0 || v == target)
                continue;
            target_line_power += CalculateLineFlow(n, v, data);
        }
        target_line_power -= data.at(n).real_power();
        calculated_real_power += target_line_power;
    }
    return calculated_real_power;
}

void PAAgent::HandleIncomingMessage(boost::shared_ptr<const ModuleMessage> m, CPeerNode peer)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;

    if(m->has_physical_attestation_message())
    {
        PhysicalAttestationMessage pam = m->physical_attestation_message();

        if(pam.has_attestation_request_message())
        {
            HandleAttestationRequest(pam.attestation_request_message());
        }
        else if(pam.has_state_request_message())
        {
            HandleStateRequest(pam.state_request_message(), peer);
        }
        else if(pam.has_state_response_message())
        {
            HandleStateResponse(pam.state_response_message(), peer);
        }
        else if(pam.has_expired_state_message())
        {
            HandleExpiredState(pam.expired_state_message(), peer);
        }
        else
        {
            Logger.Warn << "Dropped unexpected message:\n" << m->DebugString();
        }
    }
    else
    {
        Logger.Warn << "Dropped message of type:\n" << m->DebugString();
    }
}

void PAAgent::HandleAttestationRequest(const AttestationRequestMessage & m)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    device::CDevice::Pointer clock = device::CDeviceManager::Instance().GetClock();
    Framework framework;
    framework.invalid = m.request_time() < 0 || (clock && m.request_time() > clock->GetState("time"));
    framework.target = m.attestation_target();
    framework.expected_value = m.expected_value();
    framework.migration_time = m.request_time();
    framework.completion_time = m.request_time() + HARDWARE_DELAY_MS / 1000.0;
    try
    {
        Logger.Info << "Building framework " << framework.target << " at " << framework.migration_time << std::endl;
        framework.migration_members = BuildFramework(framework.target, framework.migration_time);
    }
    catch(std::exception & e)
    {
        Logger.Notice << "Failed to build attestation framework for " << framework.target << " at " << framework.completion_time << std::endl;
        framework.invalid = true;
    }
    m_frameworks.push_back(framework);
}

void PAAgent::HandleStateRequest(const StateRequestMessage & m, CPeerNode peer)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    try
    {
        peer.Send(MessageStateResponse(m.request_time()));
    }
    catch(std::exception & e)
    {
        device::CDevice::Pointer clock = device::CDeviceManager::Instance().GetClock();
        if(clock)
            Logger.Notice << "Expired state for " << m.request_time() << ", current time " << clock->GetState("time") << std::endl;
        else
            Logger.Notice << "Expired state for " << m.request_time() << " (No Clock)" << std::endl;
        peer.Send(MessageExpiredState(m.request_time()));
    }
}

void PAAgent::HandleStateResponse(const StateResponseMessage & m, CPeerNode peer)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    std::string key = peer.GetUUID() + "." + boost::lexical_cast<std::string>(m.time());
    m_responses[key] = m;
}

void PAAgent::HandleExpiredState(const ExpiredStateMessage & m, CPeerNode peer)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    std::string key = peer.GetUUID() + "." + boost::lexical_cast<std::string>(m.time());
    m_ExpiredStates.insert(key);
}

std::set<std::string> PAAgent::BuildFramework(std::string target, float time)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    std::set<std::string> members;
    members = CPhysicalTopology::Instance().ReachablePeers(target, CDataManager::Instance().GetFIDState(time), 4);
    std::map< std::string, std::set<std::string> > adjacent_invariant;
    std::set<std::string> reachable, adjacent, invariant, intersect;

    // get the current graph structure (V)
    reachable = CPhysicalTopology::Instance().ReachablePeers(target, CDataManager::Instance().GetFIDState(time));

    // calculate all the framework invariants (I^S)
    BOOST_FOREACH(std::string m, members)
    {
        adjacent = CPhysicalTopology::Instance().ReachablePeers(m, CDataManager::Instance().GetFIDState(time), 1);
        if(std::includes(members.begin(), members.end(), adjacent.begin(), adjacent.end()))
        {
            invariant.insert(m);
        }
    }

    // determine which invariants each vertex is adjacent with (mu(N(x)) for x in V)
    BOOST_FOREACH(std::string x, reachable)
    {
        adjacent = CPhysicalTopology::Instance().ReachablePeers(x, CDataManager::Instance().GetFIDState(time), 1);
        adjacent.erase(x);
        std::set_intersection(adjacent.begin(), adjacent.end(), invariant.begin(), invariant.end(), std::inserter(intersect, intersect.begin()));
        adjacent_invariant[x] = intersect;
        intersect.clear();
    }

    // condition 2 and 3 (includes target)
    BOOST_FOREACH(std::string x, CPhysicalTopology::Instance().ReachablePeers(target, CDataManager::Instance().GetFIDState(time), 1))
    {
        if(adjacent_invariant.at(x).size() < 2)
        {
            Logger.Info << x << " is not adjacent to at least two invariants." << std::endl;
            throw std::runtime_error("Nondeducible Framework");
        }
    }

    // condition 4
    BOOST_FOREACH(std::string x, reachable)
    {
        if(x == target)
            continue;
        std::set<std::string> x_invariant = adjacent_invariant.at(x);
        std::set<std::string> t_invariant = adjacent_invariant.at(target);
  
        x_invariant.erase(target);
        t_invariant.erase(x);
  
        if(x_invariant == t_invariant)
        {
            Logger.Info << x << " and " << target << " share the same invariants." << std::endl;
            throw std::runtime_error("Nondeducible Framework");
        }
    }
    return members;
}

ModuleMessage PAAgent::MessageStateRequest(float time)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    PhysicalAttestationMessage msg;
    StateRequestMessage * submsg = msg.mutable_state_request_message();
    submsg->set_request_time(time);
    return PrepareForSending(msg, "pa");
}

ModuleMessage PAAgent::MessageAttestationFailure(std::string target, float correction)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    PhysicalAttestationMessage msg;
    AttestationFailureMessage * submsg = msg.mutable_attestation_failure_message();
    submsg->set_target(target);
    submsg->set_adjustment(correction);
    return PrepareForSending(msg, "lb");
}

ModuleMessage PAAgent::MessageStateResponse(float time)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    PhysicalAttestationMessage msg;
    StateResponseMessage * submsg = msg.mutable_state_response_message();
    submsg->set_time(time);
    submsg->set_real_power(CDataManager::Instance().GetData("SST.gateway", time));
    submsg->set_voltage1(CDataManager::Instance().GetData("BUS.V1", time));
    submsg->set_voltage2(CDataManager::Instance().GetData("BUS.V2", time));
    submsg->set_voltage3(CDataManager::Instance().GetData("BUS.V3", time));
    submsg->set_phase1(CDataManager::Instance().GetData("BUS.delta", time));
    submsg->set_phase2(CDataManager::Instance().GetData("BUS.delta", time));
    submsg->set_phase3(CDataManager::Instance().GetData("BUS.delta", time));
    return PrepareForSending(msg, "pa");
}

ModuleMessage PAAgent::MessageExpiredState(float time)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    PhysicalAttestationMessage msg;
    ExpiredStateMessage * submsg = msg.mutable_expired_state_message();
    submsg->set_time(time);
    return PrepareForSending(msg, "pa");
}

ModuleMessage PAAgent::PrepareForSending(const PhysicalAttestationMessage & m, std::string recipient)
{
    Logger.Trace << __PRETTY_FUNCTION__ << std::endl;
    ModuleMessage mm;
    mm.mutable_physical_attestation_message()->CopyFrom(m);
    mm.set_recipient_module(recipient);
    return mm;
}

} // namespace pa
} // namespace broker
} // namespace freedm
