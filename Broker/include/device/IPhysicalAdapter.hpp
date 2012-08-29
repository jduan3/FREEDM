////////////////////////////////////////////////////////////////////////////////
/// @file           IPhysicalAdapter.hpp
///
/// @author         Thomas Roth <tprfh7@mst.edu>
/// @author         Yaxi Liu <ylztf@mst.edu>
/// @author         Michael Catanzaro <michael.catanzaro@mst.edu>
///
/// @project        FREEDM DGI
///
/// @description    Interface for a physical device adapter.
///
/// These source code files were created at Missouri University of Science and
/// Technology, and are intended for use in teaching or research. They may be
/// freely copied, modified, and redistributed as long as modified versions are
/// clearly marked as such and this notice is not removed. Neither the authors
/// nor Missouri S&T make any warranty, express or implied, nor assume any legal
/// responsibility for the accuracy, completeness, or usefulness of these files
/// or any information distributed with these files.
///
/// Suggested modifications or questions about these files can be directed to
/// Dr. Bruce McMillin, Department of Computer Science, Missouri University of
/// Science and Technology, Rolla, MO 65409 <ff@mst.edu>.
////////////////////////////////////////////////////////////////////////////////

#ifndef I_PHYSICAL_ADAPTER_HPP
#define	I_PHYSICAL_ADAPTER_HPP

#include <string>
#include <utility>

#include <boost/shared_ptr.hpp>
#include <boost/noncopyable.hpp>

namespace freedm {
namespace broker {
namespace device {

/// Type of the value for device signals.
typedef float SettingValue;

/// Physical adapter device interface.
////////////////////////////////////////////////////////////////////////////////
/// Defines the interface each device uses to perform its operations.  The
/// concrete adapter is responsible for implementation of both Get and Set
/// functions.
///
/// @limitations None.
////////////////////////////////////////////////////////////////////////////////
class IPhysicalAdapter
    : private boost::noncopyable
{
public:
    /// Pointer to a physical adapter.
    typedef boost::shared_ptr<IPhysicalAdapter> Pointer;
    
    /// Registers a new device signal with the physical adapter.
    void RegisterStateInfo(std::string device, std::string signal,
            std::size_t index );
    
    /// Registers a new device signal with the physical adapter.
    void RegisterCommandInfo(std::string device, std::string signal,
            std::size_t index );
    
    /// Start the adapter.
    virtual void Start() = 0;
    
    /// Retrieves a value from a device.
    virtual SettingValue Get(std::string device, std::string signal) const = 0;

    /// Sets a value on a device.
    virtual void Set(std::string device, std::string signal,
            const SettingValue value) = 0;

    /// Virtual destructor for derived classes.
    virtual ~IPhysicalAdapter();
};

} // namespace device
} // namespace broker
} // namespace freedm

#endif // I_PHYSICAL_ADAPTER_HPP
