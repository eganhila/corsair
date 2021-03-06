/** This file is part of Corsair simulation.
 *
 *  Copyright 2011-2014 Finnish Meteorological Institute
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SEP_INJECTORS_COMMON_H
#define SEP_INJECTORS_COMMON_H

#include <string>

#include "sep_namespaces.h"

namespace sep {
   
   sep::particleinjector::frame::Frame getInjectionFrame(const std::string& frame);
   sep::particleinjector::region::Region getInjectionRegion(const std::string& region);

} // namespace sep
   
#endif
