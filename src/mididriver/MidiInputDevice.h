/***************************************************************************
 *                                                                         *
 *   LinuxSampler - modular, streaming capable sampler                     *
 *                                                                         *
 *   Copyright (C) 2003, 2004 by Benno Senoner and Christian Schoenebeck   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the Free Software           *
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston,                 *
 *   MA  02111-1307  USA                                                   *
 ***************************************************************************/

#ifndef __LS_MIDIINPUTDEVICE_H__
#define __LS_MIDIINPUTDEVICE_H__

#include <stdexcept>
#include <set>

#include "../common/global.h"
#include "../common/LinuxSamplerException.h"
#include "../engines/common/Engine.h"

namespace LinuxSampler {

    // just symbol prototyping
    class Engine;

    /** Abstract base class for MIDI input drivers in LinuxSampler
     *
     * This class will be derived by specialized classes which implement the
     * connection to a specific MIDI input system (e.g. Alsa Sequencer,
     * CoreMIDI). The MidiInputDevice desendant should just call the
     * appropriate (protected) Dispatch* method here when an MIDI event
     * occured. The dispatch* methods here will automatically forward the
     * MIDI event to the appropriate, connected sampler engines.
     */
    class MidiInputDevice {
        public:

            /////////////////////////////////////////////////////////////////
            // type definitions

            /**
             * List of all currently implemented MIDI input drivers.
             */
            enum type_t {
                type_alsa,
				type_core_midi,
				type_midishare
            };

            /**
             * MIDI channels
             */
            enum midi_chan_t {
                midi_chan_all = 0,
                midi_chan_1   = 1,
                midi_chan_2   = 2,
                midi_chan_3   = 3,
                midi_chan_4   = 4,
                midi_chan_5   = 5,
                midi_chan_6   = 6,
                midi_chan_7   = 7,
                midi_chan_8   = 8,
                midi_chan_9   = 9,
                midi_chan_10  = 10,
                midi_chan_11  = 11,
                midi_chan_12  = 12,
                midi_chan_13  = 13,
                midi_chan_14  = 14,
                midi_chan_15  = 15,
                midi_chan_16  = 16
            };



            /////////////////////////////////////////////////////////////////
            // abstract methods
            //     (these have to be implemented by the descendant)

            /**
             * Start listen to MIDI input events on the MIDI input device.
             * The MIDIInputDevice descendant should forward all MIDI input
             * events by calling the appropriate (protected) Dispatch*
             * method of class MidiInputDevice.
             */
            virtual void Listen() = 0;

            /**
             * Stop to listen to MIDI input events on the MIDI input device.
             * After this method was called, the MidiInputDevice descendant
             * should ignore all MIDI input events.
             */
            virtual void StopListen() = 0;

            virtual void SetInputPort(const char *) = 0;

            /////////////////////////////////////////////////////////////////
            // normal methods
            //     (usually not to be overriden by descendant)

            /**
             * Connect given sampler engine with this MIDI input device.
             * The engine can either be connected to one specific MIDI
             * channel or all MIDI channels. If an engine gets connected
             * twice to this MIDI input device, then the engine's old
             * connection will be detached (no matter on which MIDI channel).
             *
             * @param pEngine     - sampler engine
             * @param MidiChannel - MIDI channel to connect to
             * @throws MidiInputException  if MidiChannel argument invalid
             */
            void Connect(Engine* pEngine, midi_chan_t MidiChannel);

            /**
             * Disconnect given sampler engine from this MIDI input device.
             *
             * @param pEngine - sampler engine
             */
            void Disconnect(Engine* pEngine);

            /**
             * Returns the ID that identifies the implementing MIDI input
             * driver.
             */
            type_t Type();

        protected:
            std::set<Engine*> MidiChannelMap[17]; ///< Contains the list of connected engines for each MIDI channel, where index 0 points to the list of engines which are connected to all MIDI channels. Usually it's not necessary for the descendant to use this map, instead it should just use the Dispatch* methods.
            type_t            MidiInputType;


            /**
             * Constructor. Has to be called by the implementing MIDI
             * input driver to define the ID of the driver. When a new MIDI
             * input driver is implemented, the
             * MidiInputDevice::midi_input_type_t enumeration has to be
             * extended with a new ID for the new MIDI input driver.
             */
            MidiInputDevice(type_t Type);



            /////////////////////////////////////////////////////////////////
            // dispatch methods
            //     (should be called by the MidiInputDevice descendant on events)

            /**
             * Should be called by the implementing MIDI input device
             * whenever a note on event arrived, this will cause the note on
             * event to be forwarded to all connected engines on the
             * corresponding MIDI channel.
             *
             * @param Key         - MIDI key number of the triggered key
             * @param Velocity    - MIDI velocity of the triggered key
             * @param MidiChannel - MIDI channel on which event occured on
             */
            void DispatchNoteOn(uint8_t Key, uint8_t Velocity, uint MidiChannel);

            /**
             * Should be called by the implementing MIDI input device
             * whenever a note off event arrived, this will cause the note
             * off event to be forwarded to all connected engines on the
             * corresponding MIDI channel.
             *
             * @param Key         - MIDI key number of the released key
             * @param Velocity    - MIDI velocity of the released key
             * @param MidiChannel - MIDI channel on which event occured on
             */
            void DispatchNoteOff(uint8_t Key, uint8_t Velocity, uint MidiChannel);

            /**
             * Should be called by the implementing MIDI input device
             * whenever a pitchbend event arrived, this will cause the
             * pitchbend event to be forwarded to all connected engines.
             *
             * @param Pitch       - MIDI pitch value
             * @param MidiChannel - MIDI channel on which event occured on
             */
            void DispatchPitchbend(int Pitch, uint MidiChannel);

            /**
             * Should be called by the implementing MIDI input device
             * whenever a control change event arrived, this will cause the
             * control change event to be forwarded to all engines on the
             * corresponding MIDI channel.
             *
             * @param Controller  - MIDI controller number
             * @param Value       - MIDI control change value
             * @param MidiChannel - MIDI channel on which event occured on
             */
            void DispatchControlChange(uint8_t Controller, uint8_t Value, uint MidiChannel);
    };

    /**
     * Midi input exception that should be thrown by the MidiInputDevice
     * descendants in case initialization of the MIDI input system failed
     * (which should be done in the constructor of the MidiInputDevice
     * descendant).
     */
    class MidiInputException : public LinuxSamplerException {
        public:
            MidiInputException(const std::string& msg) : LinuxSamplerException(msg) {}
    };
}

#endif // __LS_MIDIINPUTDEVICE_H__
