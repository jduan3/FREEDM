////////////////////////////////////////////////////////////////////////////////
/// @file           PhysicalDeviceTypes.hpp
///
/// @author         Thomas Roth <tprfh7@mst.edu>
///
/// @project        FREEDM DGI
///
/// @description    Common header for all physical device types
///
/// These source code files were created at the Missouri University of Science
/// and Technology, and are intended for use in teaching or research. They may
/// be freely copied, modified and redistributed as long as modified versions
/// are clearly marked as such and this notice is not removed.
///
/// Neither the authors nor Missouri S&T make any warranty, express or implied,
/// nor assume any legal responsibility for the accuracy, completeness or
/// usefulness of these files or any information distributed with these files.
///
/// Suggested modifications or questions about these files can be directed to
/// Dr. Bruce McMillin, Department of Computer Science, Missouri University of
/// Science and Technology, Rolla, MO 65409 <ff@mst.edu>.
////////////////////////////////////////////////////////////////////////////////

#ifndef PHYSICAL_DEVICE_TYPES
#define PHYSICAL_DEVICE_TYPES

#include "types/IDevice.hpp"
#include "types/IDeviceLWI.hpp"
#include "types/CDeviceDESD.hpp"
#include "types/CDeviceDRER.hpp"
#include "types/CDeviceLOAD.hpp"
#include "types/CDeviceSST.hpp"

namespace freedm
{
namespace broker
{
namespace device
{

/// Registers the physical devices known to this file with the device factory.
void RegisterPhysicalDevices();

}
}
}

#endif // PHYSICAL_DEVICE_TYPES
