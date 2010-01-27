/***************************************************************************
 *                                                                         *
 *   LinuxSampler - modular, streaming capable sampler                     *
 *                                                                         *
 *   Copyright (C) 2003,2004 by Benno Senoner and Christian Schoenebeck    *
 *   Copyright (C) 2005-2009 Christian Schoenebeck                         *
 *   Copyright (C) 2009 Grigor Iliev                                       *
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

#include "Engine.h"
#include "EngineChannel.h"

namespace LinuxSampler { namespace sf2 {
    Engine::Format Engine::GetEngineFormat() { return SF2; }

    /**
     *  Reacts on supported control change commands (e.g. pitch bend wheel,
     *  modulation wheel, aftertouch).
     *
     *  @param pEngineChannel - engine channel on which this event occured on
     *  @param itControlChangeEvent - controller, value and time stamp of the event
     */
    void Engine::ProcessControlChange (
        LinuxSampler::EngineChannel*  pEngineChannel,
        Pool<Event>::Iterator&        itControlChangeEvent
    ) {
        dmsg(4,("Engine::ContinuousController cc=%d v=%d\n", itControlChangeEvent->Param.CC.Controller, itControlChangeEvent->Param.CC.Value));

        EngineChannel* pChannel = dynamic_cast<EngineChannel*>(pEngineChannel);
        // handle the "control triggered" MIDI rule: a control change
        // event can trigger a new note on or note off event
        if (pChannel->pInstrument) {

            // TODO: 
        }

        // update controller value in the engine channel's controller table
        pChannel->ControllerTable[itControlChangeEvent->Param.CC.Controller] = itControlChangeEvent->Param.CC.Value;

        ProcessHardcodedControllers(pEngineChannel, itControlChangeEvent);

        // handle FX send controllers
        ProcessFxSendControllers(pChannel, itControlChangeEvent);
    }

    DiskThread* Engine::CreateDiskThread() {
        return new DiskThread (
            iMaxDiskStreams,
            ((pAudioOutputDevice->MaxSamplesPerCycle() << CONFIG_MAX_PITCH) << 1) + 6, //FIXME: assuming stereo
            &instruments
        );
    }

    void Engine::TriggerNewVoices (
        LinuxSampler::EngineChannel* pEngineChannel,
        RTList<Event>::Iterator&     itNoteOnEvent,
        bool                         HandleKeyGroupConflicts
    ) {
        EngineChannel* pChannel = static_cast<EngineChannel*>(pEngineChannel);

        uint8_t  chan     = pChannel->MidiChannel();
        int      key      = itNoteOnEvent->Param.Note.Key;
        uint8_t  vel      = itNoteOnEvent->Param.Note.Velocity;
        int      bend     = pChannel->Pitch;
        uint8_t  chanaft  = pChannel->ControllerTable[128];
        uint8_t* cc       = pChannel->ControllerTable;

        std::vector< ::sf2::Region*> regs = pChannel->pInstrument->GetRegionsOnKey (
            key, vel
        );

        pChannel->regionsTemp.clear();

        for (int i = 0; i < regs.size(); i++) {
            // TODO: Generators in the PGEN sub-chunk are applied relative to generators in the IGEN sub-chunk in an additive manner.  In
            // other words, PGEN generators increase or decrease the value of an IGEN generator.
            ::sf2::Instrument* sfInstr = regs[i]->pInstrument;

            std::vector< ::sf2::Region*> subRegs = sfInstr->GetRegionsOnKey (
                key, vel
            );
            for (int j = 0; j < subRegs.size(); j++) {
                pChannel->regionsTemp.push_back(subRegs[j]);
            }
        }
        
        for (int i = 0; i < pChannel->regionsTemp.size(); i++) {
            ::sf2::Region* r = pChannel->regionsTemp[i];
            //std::cout << r->GetSample()->GetName();
            //std::cout << " loKey: " << r->loKey << " hiKey: " << r->hiKey << " minVel: " << r->minVel << " maxVel: " << r->maxVel << " Vel: " << ((int)vel) << std::endl << std::endl;
            if (!RegionSuspended(pChannel->regionsTemp[i])) {
                LaunchVoice(pChannel, itNoteOnEvent, i, false, true, HandleKeyGroupConflicts);
            }
        }
    }

    void Engine::TriggerReleaseVoices (
        LinuxSampler::EngineChannel*  pEngineChannel,
        RTList<Event>::Iterator&      itNoteOffEvent
    ) {
        
    }

    Pool<Voice>::Iterator Engine::LaunchVoice (
        LinuxSampler::EngineChannel*  pEngineChannel,
        Pool<Event>::Iterator&        itNoteOnEvent,
        int                           iLayer,
        bool                          ReleaseTriggerVoice,
        bool                          VoiceStealing,
        bool                          HandleKeyGroupConflicts
    ) {
        EngineChannel* pChannel = static_cast<EngineChannel*>(pEngineChannel);
        int key = itNoteOnEvent->Param.Note.Key;
        EngineChannel::MidiKey* pKey = &pChannel->pMIDIKeyInfo[key];

        Voice::type_t VoiceType = Voice::type_normal;

        Pool<Voice>::Iterator itNewVoice;
        ::sf2::Region* pRgn = pChannel->regionsTemp[iLayer];

        // no need to process if sample is silent
        if (!pRgn->GetSample() || !pRgn->GetSample()->GetTotalFrameCount()) return Pool<Voice>::Iterator();

        // only mark the first voice of a layered voice (group) to be in a
        // key group, so the layered voices won't kill each other
        int iKeyGroup = (iLayer == 0 && !ReleaseTriggerVoice) ? pRgn->exclusiveClass : 0;
        if (HandleKeyGroupConflicts) pChannel->HandleKeyGroupConflicts(iKeyGroup, itNoteOnEvent);

        // allocate a new voice for the key
        itNewVoice = pKey->pActiveVoices->allocAppend();
        int res = InitNewVoice (
                pChannel, pRgn, itNoteOnEvent, VoiceType, iLayer,
                iKeyGroup, ReleaseTriggerVoice, VoiceStealing, itNewVoice
        );
        if (!res) return itNewVoice;

        // return if this is a release triggered voice and there is no
        // releasetrigger dimension (could happen if an instrument
        // change has occured between note on and off)
        //if (ReleaseTriggerVoice && VoiceType != Voice::type_release_trigger) return Pool<Voice>::Iterator();

        return Pool<Voice>::Iterator(); // no free voice or error
    }

    bool Engine::DiskStreamSupported() {
        return true;
    }

    String Engine::Description() {
        return "SoundFont Format Engine";
    }

    String Engine::Version() {
        String s = "$Revision: 1.2 $";
        return s.substr(11, s.size() - 13); // cut dollar signs, spaces and CVS macro keyword
    }

}} // namespace LinuxSampler::sf2