/** This file is part of Corsair simulation.
 *
 *  Copyright 2011-2013 Finnish Meteorological Institute
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

#include <cstdlib>
#include <iostream>
#include <sstream>

#include <linear_algebra.h>

#include "sep_propagate.h"
#include "sep_accumulation_stretched.h"
#include "sep_simcontrol.h"
#include "sep_operator_energy_channels.h"
#include "sep_particle_definition.h"
#include "sep_particle_species.h"
#include "sep_guiding_center_theory.h"

using namespace std;

namespace sep {
   extern sep::SimControl simControl;
   const string PREFIX = "EnergyChannels";

   namespace spacecraft {
      bool createInstrument(SimulationClasses& simClasses,ConfigReader& cr,const string& configRegion,const std::string& name,Instrument& instrument) {
	 const string prefix = configRegion + "." + name;

	 // Define config file items:
	 cr.add(prefix+".energy_units","Units in which energies for instrument '"+name+"' are given 'eV/keV/MeV/GeV' (string)",string("eV"));
	 cr.add(prefix+".energy_per_amu","If 'yes' energies are per amu 'yes/no' (string)",string("no"));
	 cr.add(prefix+".divide_by_bin_width","If 'yes' values in each channel are divided by bin width 'yes/no' (string)",string("yes"));
	 cr.addComposed(prefix+".energy_limits","Min,Max energy for each channel in given units (float)");
	 cr.addComposed(prefix+".channel_names","Names for energy channels (string)");
	 cr.addComposed(prefix+".species_names","Names of particle species included in data (string)");
	 cr.parse();
	 
	 // Read config file items:
	 string energyUnitsString,energyPerAmu,divideByBinWidth;
	 vector<string> channelNames,energyLimits,speciesNames;
	 cr.get(prefix+".energy_units",energyUnitsString);
	 cr.get(prefix+".energy_per_amu",energyPerAmu);
	 cr.get(prefix+".divide_by_bin_width",divideByBinWidth);
	 cr.get(prefix+".energy_limits",energyLimits);
	 cr.get(prefix+".channel_names",channelNames);
	 cr.get(prefix+".species_names",speciesNames);
	 
	 if (energyLimits.size() != channelNames.size()) {
	    simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' energy limits ";
	    simClasses.logger << "and channel name sizes do not match" << endl << write;
	    return false;
	 }
	 
	 // Get energy units conversion factor:
	 const double energyUnits = simClasses.constants.getEnergyInSI(energyUnitsString);
	 if (energyUnits == numeric_limits<double>::infinity()) {
	    simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' has unknown ";
	    simClasses.logger << "energy units '" << energyUnitsString << "'" << endl << write;
	    return false;
	 }
	 
	 
	 instrument.name = name;
	 if (divideByBinWidth == "yes") instrument.divideByBinWidth = true;
	 else instrument.divideByBinWidth = false;
	 if (energyPerAmu == "yes") instrument.energyPerNucleon = true;
	 else instrument.energyPerNucleon = false;
	 
	 for (size_t i=0; i<energyLimits.size(); ++i) {
	    // Copy name of energy channel to instrument:
	    instrument.channelNames.push_back(channelNames[i]);
	    
	    // Read min,max energies for this channel:
	    const size_t separator = energyLimits[i].find(' ');
	    if (separator == string::npos) {
	       simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' energy channel " << i;
	       simClasses.logger << " does not contain min & max energy limits" << endl << write;
	       return false;
	    }
	    const Real energyMin = energyUnits * atof(energyLimits[i].substr(0,separator).c_str());
	    const Real energyMax = energyUnits * atof(energyLimits[i].substr(separator+1,energyLimits[i].size()-separator).c_str());
	    const Real binWidth = energyMax - energyMin;
	    
	    // Copy min,max energies and bin width to instrument:
	    instrument.minValues.push_back(energyMin);
	    instrument.maxValues.push_back(energyMax);
	    instrument.binWidths.push_back(binWidth);
	 }
	    
	 // Remove empty entries from speciesNames:
	 do {
	    bool finished = true;
	    for (std::vector<std::string>::iterator it=speciesNames.begin(); it!=speciesNames.end(); ++it) {
	       if (*it == "") {
		  speciesNames.erase(it);
		  finished = false;
		  break;
	       }
	    }
	    if (finished == true) break;
	 } while (true);
	 
	 if (speciesNames.size() == 0) {
	    simClasses.logger << "(SEP OP ENERGY CHANNELS) WARNING: Instrument '" << name << "' has no particle ";
	    simClasses.logger << "species, skipping it." << endl << write;
	    return false;
	 }
	 instrument.speciesNames = speciesNames;
	 
	 return true;
      }
   }
   
   OperatorEnergyChannels::OperatorEnergyChannels(int32_t order): OperatorAccumulationBase(1,order) {
      totalNumberOfArrays = 0;
      initialized = false;
   }
   
   OperatorEnergyChannels::~OperatorEnergyChannels() {
      finalize();
   }

   void OperatorEnergyChannels::accumulateBlock(pargrid::CellID blockLID,Real* RESTRICT accumArray,
						const std::vector<ParticleListBase*>& particleLists) {
      // Create a temporary array for accumulating:
      const int32_t SIZE = (block::WIDTH_X+2)*(block::WIDTH_Y+2)*(block::WIDTH_Z+2);
      Real* array = new Real[SIZE];
      for (int32_t i=0; i<SIZE; ++i) array[i] = 0.0;

      // Arrays for E,B,grad B values:
      Real E[3];
      Real B[3];
      Real gradB[9];
      
      // Get block global ID:
      int32_t i_block,j_block,k_block;
      const pargrid::CellID blockGID = simClasses->pargrid.getGlobalIDs()[blockLID];
      block::calculateBlockIndices(*sim,blockGID,i_block,j_block,k_block);

      // Iterate over all species accumulated to this channel:
      for (size_t s=0; s<instruments[currentInstrument].speciesNames.size(); ++s) {
	 // Get particles:
	 const std::string& speciesName = instruments[currentInstrument].speciesNames[s];
	 size_t speciesIndex = numeric_limits<size_t>::max();
	 for (size_t l=0; l<particleLists.size(); ++l) {
	    if (particleLists[l]->getSpeciesName() == speciesName) {
	       speciesIndex = l;
	       break;
	    }
	 }
	 if (speciesIndex == numeric_limits<size_t>::max()) continue;
	 
	 typedef sep::Particle<Real> PARTICLE;
	 pargrid::DataID speciesID = pargrid::INVALID_DATAID;
	 if (particleLists[speciesIndex]->getParticles(speciesID) == false) continue;
	 const pargrid::DataWrapper<PARTICLE>& wrapper = simClasses->pargrid.getUserDataDynamic<PARTICLE>(speciesID);
	 PARTICLE* particles = wrapper.data()[blockLID];

	 // Get particle species mass (proton mass if energy per amu):
	 Real mass = constants::MASS_PROTON;
	 const sep::Species& species = *reinterpret_cast<const sep::Species*>(particleLists[speciesIndex]->getSpecies());
	 if (instruments[currentInstrument].energyPerNucleon != true) {
	    mass = species.mass;
	 }

	 for (pargrid::ArraySizetype p=0; p<wrapper.size(blockLID); ++p) {
	    // Get fields at particle position:
	    (*simControl.fieldsGetFields)(blockLID,sim->t,particles[p].state,E,B,gradB);
	    
	    // Calculate drift velocity:
	    Real V_drift[3];
	    calculateDriftVelocity(species,particles[p],E,B,gradB,V_drift);
	    
	    // Calculate non-relativistic energy and check that it is 
	    // within channel min,max limits:
	    Real energy = (particles[p].state[particle::V_PAR]*particles[p].state[particle::V_PAR]
	      + vectorMagnitude2<3>(V_drift)) * 0.5 * mass;
	    energy += vectorMagnitude<3>(B)*particles[p].state[particle::MU];
	    if (energy < instruments[currentInstrument].minValues[currentChannel]) continue;
	    if (energy >= instruments[currentInstrument].maxValues[currentChannel]) continue;

	    // Accumulate particle weight to this channel:
	    Real pos[3];
	    pos[0] = particles[p].state[particle::XCRD] - i_block*block::WIDTH_X + 1;
	    pos[1] = particles[p].state[particle::YCRD] - j_block*block::WIDTH_Y + 1;
	    pos[2] = particles[p].state[particle::ZCRD] - k_block*block::WIDTH_Z + 1;
	    const Real weight = particles[p].state[particle::WEIGHT];

	    switch (OperatorAccumulationBase::getOrder()) {
	     case 0:
	       accumScalarCentroidLogisticNGP_3D(array,pos,weight);
	       break;
	     case 1:
	       accumScalarCentroidLogisticCIC_3D(array,pos,weight);
	       break;
	     case 2:
	       accumScalarCentroidLogisticTSC_3D(array,pos,weight);
	       break;
	    }
	 }
      }
      
      // Add values from array to global accumulation array:
      block::addValues3D(*simClasses,blockLID,array,accumArray);
   }

   bool OperatorEnergyChannels::createInstrument(SimulationClasses& simClasses,ConfigReader& cr,const std::string& name) {
      bool success = true;
      const string prefix = PREFIX+'.'+name;
      
      // Define config file items:
      cr.add(prefix+".energy_units","Units in which energies for instrument '"+name+"' are given 'eV/keV/MeV/GeV' (string)",string("eV"));
      cr.add(prefix+".energy_per_amu","If 'yes' energies are per amu 'yes/no' (string)",string("no"));
      cr.add(prefix+".divide_by_bin_width","If 'yes' values in each channel are divided by bin width 'yes/no' (string)",string("yes"));
      cr.addComposed(prefix+".energy_limits","Min,Max energy for each channel in given units (float)");
      cr.addComposed(prefix+".channel_names","Names for energy channels (string)");
      cr.addComposed(prefix+".species_names","Names of particle species included in data (string)");
      cr.parse();

      // Read config file items:
      string energyUnitsString,energyPerAmu,divideByBinWidth;
      vector<string> channelNames,energyLimits,speciesNames;
      cr.get(prefix+".energy_units",energyUnitsString);
      cr.get(prefix+".energy_per_amu",energyPerAmu);
      cr.get(prefix+".divide_by_bin_width",divideByBinWidth);
      cr.get(prefix+".energy_limits",energyLimits);
      cr.get(prefix+".channel_names",channelNames);
      cr.get(prefix+".species_names",speciesNames);

      if (energyLimits.size() != channelNames.size()) {
	 simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' energy limits ";
	 simClasses.logger << "and channel name sizes do not match" << endl << write;
	 return false;
      }

      // Get energy units conversion factor:
      const double energyUnits = simClasses.constants.getEnergyInSI(energyUnitsString);
      if (energyUnits == numeric_limits<double>::infinity()) {
	 simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' has unknown ";
	 simClasses.logger << "energy units '" << energyUnitsString << "'" << endl << write;
	 return false;
      }
	 
      spacecraft::Instrument instrument;
      instrument.name = name;
      if (divideByBinWidth == "yes") instrument.divideByBinWidth = true;
      else instrument.divideByBinWidth = false;
      if (energyPerAmu == "yes") instrument.energyPerNucleon = true;
      else instrument.energyPerNucleon = false;
      
      for (size_t i=0; i<energyLimits.size(); ++i) {
	 // Copy name of energy channel to instrument:
	 instrument.channelNames.push_back(channelNames[i]);
	 
	 // Read min,max energies for this channel:
	 const size_t separator = energyLimits[i].find(' ');
	 if (separator == string::npos) {
	    simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Instrument '" << name << "' energy channel " << i;
	    simClasses.logger << " does not contain min & max energy limits" << endl << write;
	    return false;
	 }
	 const Real energyMin = energyUnits * atof(energyLimits[i].substr(0,separator).c_str());
	 const Real energyMax = energyUnits * atof(energyLimits[i].substr(separator+1,energyLimits[i].size()-separator).c_str());
	 const Real binWidth = energyMax - energyMin;
	 
	 // Copy min,max energies and bin width to instrument:
	 instrument.minValues.push_back(energyMin);
	 instrument.maxValues.push_back(energyMax);
	 instrument.binWidths.push_back(binWidth);
      }
      
      // Remove empty entries from speciesNames:
      do {
	 bool finished = true;
	 for (std::vector<std::string>::iterator it=speciesNames.begin(); it!=speciesNames.end(); ++it) {
	    if (*it == "") {
	       speciesNames.erase(it);
	       finished = false;
	       break;
	    }
	 }
	 if (finished == true) break;
      } while (true);
      
      if (speciesNames.size() == 0) {
	 simClasses.logger << "(SEP OP ENERGY CHANNELS) WARNING: Instrument '" << name << "' has no particle ";
	 simClasses.logger << "species, skipping it." << endl << write;
	 return success;
      }
      instrument.speciesNames = speciesNames;

      // If instrument was created successfully, add it to vector instruments:
      if (success == true) {
	 const size_t i = instruments.size();
	 instruments.push_back(instrument);

	 for (size_t c=0; c<energyLimits.size(); ++c) {
	    instrumentIndices.push_back(make_pair(i,c));
	    ++totalNumberOfArrays;
	 }
      }

      return success;
   }
   
   bool OperatorEnergyChannels::finalize() {
      if (initialized == false) return true;
      initialized = false;
      return true;
   }

   std::string OperatorEnergyChannels::getName() const {
      stringstream ss;
      ss << "EnergyChannels";
      return ss.str();
   }

   uint32_t OperatorEnergyChannels::getNumberOfArrays() const {
      return totalNumberOfArrays;
   }
   
   std::string OperatorEnergyChannels::getOutputName(uint32_t arrayIndex) const {
      const pair<size_t,size_t>& indices = instrumentIndices[arrayIndex];
      
      stringstream ss;
      ss << instruments[indices.first].name << "/";
      ss << instruments[indices.first].channelNames[indices.second];
      return ss.str();
   }
   
   std::string OperatorEnergyChannels::getOutputUnits(uint32_t arrayIndex) const {
      const std::pair<size_t,size_t>& indices = instrumentIndices[arrayIndex];

      stringstream ss;
      ss << "1/(m^3";
      if (instruments[indices.first].divideByBinWidth == true) ss << " eV";
      if (instruments[indices.first].energyPerNucleon == true) ss << " amu";
      ss << ')';
      
      return ss.str();
   }
   
   bool OperatorEnergyChannels::initialize(ConfigReader& cr,Simulation& sim,SimulationClasses& simClasses) {
      initialized = true;
      simClasses.logger << "(SEP OP ENERGY CHANNELS) Starting initialization" << endl << write;
      
      // Init base class:
      if (OperatorAccumulationBase::initialize(cr,sim,simClasses) == false) {
	 simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: OperatorAccumulationBase failed to initialize!" << endl << write;
	 initialized = false;
      }

      // Create a profiler section name for this DataOperator:
      #if PROFILE_LEVEL > 0
         stringstream ss;
         ss << "Energy Channels" << OperatorAccumulationBase::getOrder();
         profileName = ss.str();
         OperatorAccumulationBase::setProfileName(ss.str());
      #endif
      
      // Get names of instruments:
      vector<string> instrumentNames;
      cr.addComposed(PREFIX+".instrument_names","Names of energy instruments (string).");
      cr.parse();
      cr.get(PREFIX+".instrument_names",instrumentNames);

      //instrumentIndices.push_back(make_pair<size_t,size_t>(0,0));
      for (size_t i=0; i<instrumentNames.size(); ++i) {
	 // Skip empty lines
	 if (instrumentNames[i].size() == 0) continue;

	 // Read config file items for instrument and create it:
	 if (createInstrument(simClasses,cr,instrumentNames[i]) == false) {
	    simClasses.logger << "(SEP OP ENERGY CHANNELS) ERROR: Failed to read required config file items for ";
	    simClasses.logger << "instrument '" << instrumentNames[i] << "'" << endl << write;
	    initialized = false;
	 }
      }

      // Write init status and exit:
      simClasses.logger << "(SEP OP ENERGY CHANNELS) Initialization complete, status is ";
      if (initialized == true) simClasses.logger << "SUCCESS" << endl << write;
      else simClasses.logger << "FAILURE" << endl << write;
      
      return initialized;
   }
   
   /** Post-process accumulated values. Given array contains accumulated 
    * particle weights in the current channel. Here we divide the values 
    * by spatial cell volumes and energy channel widths (if necessary) to 
    * produce particles / m^3 / eV units.
    * @param array Array containing accumulated values.
    * @return If true, data was post-processes successfully.*/
   bool OperatorEnergyChannels::postProcessData(Real* array) {
      recalculateCellVolumes(*sim,*simClasses);
      
      Real normalization = 1.0;
      if (instruments[currentInstrument].divideByBinWidth == true) {
	 // Bin widths are in Joules:
	 Real binWidth = instruments[currentInstrument].binWidths[currentChannel];
	 binWidth /= constants::CHARGE_ELEMENTARY;
	 normalization /= binWidth;
      }

      // Divide all values by spatial cell volumes and energy channel widths (in eV):
      for (pargrid::CellID blockLID=0; blockLID<simClasses->pargrid.getNumberOfLocalCells(); ++blockLID) {
	 for (int k=0; k<block::WIDTH_Z; ++k) for (int j=0; j<block::WIDTH_Y; ++j) for (int i=0; i<block::WIDTH_X; ++i) {
	    Real cellVolume = simControl.cellVolumes[blockLID*block::SIZE+block::index(i,j,k)];
	    array[blockLID*block::SIZE+block::index(i,j,k)] *= (normalization/cellVolume);
	 }
      }
      
      return true;
   }
   
   bool OperatorEnergyChannels::setAccumulatedArray(uint32_t arrayIndex) {
      currentInstrument = instrumentIndices[arrayIndex].first;
      currentChannel    = instrumentIndices[arrayIndex].second;
      return true;
   }
   
} // namespace sep
