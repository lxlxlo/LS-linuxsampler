/***************************************************************************
 *                                                                         *
 *   LinuxSampler - modular, streaming capable sampler                     *
 *                                                                         *
 *   Copyright (C) 2003, 2004 by Benno Senoner and Christian Schoenebeck   *
 *   Copyright (C) 2005, 2006 Christian Schoenebeck                        *
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

#include <sstream>

#include "InstrumentResourceManager.h"

// We need to know the maximum number of sample points which are going to
// be processed for each render cycle of the audio output driver, to know
// how much initial sample points we need to cache into RAM. If the given
// sampler channel does not have an audio output device assigned yet
// though, we simply use this default value.
#define GIG_RESOURCE_MANAGER_DEFAULT_MAX_SAMPLES_PER_CYCLE     128

namespace LinuxSampler { namespace gig {

    // data stored as long as an instrument resource exists
    struct instr_entry_t {
        InstrumentManager::instrument_id_t ID;
        ::gig::File*                       pGig;
        uint                               MaxSamplesPerCycle; ///< if some engine requests an already allocated instrument with a higher value, we have to reallocate the instrument
    };

    // some data needed for the libgig callback function
    struct progress_callback_arg_t {
        InstrumentResourceManager*          pManager;
        InstrumentManager::instrument_id_t* pInstrumentKey;
    };

    /**
     * Callback function which will be called by libgig during loading of
     * instruments to inform about the current progress. Or to be more
     * specific; it will be called during the GetInstrument() call.
     *
     * @param pProgress - contains current progress value, pointer to the
     *                    InstrumentResourceManager instance and
     *                    instrument ID
     */
    void InstrumentResourceManager::OnInstrumentLoadingProgress(::gig::progress_t* pProgress) {
        dmsg(7,("gig::InstrumentResourceManager: progress %f%", pProgress->factor));
        progress_callback_arg_t* pArg = static_cast<progress_callback_arg_t*>(pProgress->custom);
        // we randomly schedule 90% for the .gig file loading and the remaining 10% later for sample caching
        const float localProgress = 0.9f * pProgress->factor;
        pArg->pManager->DispatchResourceProgressEvent(*pArg->pInstrumentKey, localProgress);
    }

    std::vector<InstrumentResourceManager::instrument_id_t> InstrumentResourceManager::Instruments() {
        return Entries();
    }

    InstrumentManager::mode_t InstrumentResourceManager::GetMode(const instrument_id_t& ID) {
        return static_cast<InstrumentManager::mode_t>(AvailabilityMode(ID));
    }

    void InstrumentResourceManager::SetMode(const instrument_id_t& ID, InstrumentManager::mode_t Mode) {
        dmsg(2,("gig::InstrumentResourceManager: setting mode for %s (Index=%d) to %d\n",ID.FileName.c_str(),ID.Index,Mode));
        SetAvailabilityMode(ID, static_cast<ResourceManager<InstrumentManager::instrument_id_t, ::gig::Instrument>::mode_t>(Mode));
    }

    float InstrumentResourceManager::GetVolume(const instrument_id_t& ID) {
        void* pCustomData = CustomData(ID);
        const float fVolume = (pCustomData) ? *((float*)pCustomData) /* stored value */ : 1.0f /* default value */;
        return fVolume;
    }

    void InstrumentResourceManager::SetVolume(const instrument_id_t& ID, float Volume) {
        void* pCustomData = CustomData(ID);
        if (Volume == 1.0f) { // if default volume ...
            if (pCustomData) { // ... delete volume entry if necessary
                delete (float*)pCustomData;
                SetCustomData(ID, NULL);
            }
        } else { // if not default volume
            if (!pCustomData) { // create volume entry if necessary
                pCustomData = new float;
                SetCustomData(ID, pCustomData);
            }
            *((float*)pCustomData) = Volume;
        }
    }

    String InstrumentResourceManager::GetInstrumentName(instrument_id_t ID) {
        return ""; // TODO: ...
    }

    ::gig::Instrument* InstrumentResourceManager::Create(instrument_id_t Key, InstrumentConsumer* pConsumer, void*& pArg) {
        // get gig file from inernal gig file manager
        ::gig::File* pGig = Gigs.Borrow(Key.FileName, (GigConsumer*) Key.Index); // conversion kinda hackish :/

        // we pass this to the progress callback mechanism of libgig
        progress_callback_arg_t callbackArg;
        callbackArg.pManager       = this;
        callbackArg.pInstrumentKey = &Key;

        ::gig::progress_t progress;
        progress.callback = OnInstrumentLoadingProgress;
        progress.custom   = &callbackArg;

        dmsg(1,("Loading gig instrument ('%s',%d)...",Key.FileName.c_str(),Key.Index));
        ::gig::Instrument* pInstrument = pGig->GetInstrument(Key.Index, &progress);
        if (!pInstrument) {
            std::stringstream msg;
            msg << "There's no instrument with index " << Key.Index << ".";
            throw InstrumentResourceManagerException(msg.str());
        }
        pGig->GetFirstSample(); // just to force complete instrument loading
        dmsg(1,("OK\n"));

        // cache initial samples points (for actually needed samples)
        dmsg(1,("Caching initial samples..."));
        uint iRegion = 0; // just for progress calculation
        ::gig::Region* pRgn = pInstrument->GetFirstRegion();
        while (pRgn) {
            // we randomly schedule 90% for the .gig file loading and the remaining 10% now for sample caching
            const float localProgress = 0.9f + 0.1f * (float) iRegion / (float) pInstrument->Regions;
            DispatchResourceProgressEvent(Key, localProgress);

            if (pRgn->GetSample() && !pRgn->GetSample()->GetCache().Size) {
                dmsg(2,("C"));
                CacheInitialSamples(pRgn->GetSample(), (gig::EngineChannel*) pConsumer);
            }
            for (uint i = 0; i < pRgn->DimensionRegions; i++) {
                CacheInitialSamples(pRgn->pDimensionRegions[i]->pSample, (gig::EngineChannel*) pConsumer);
            }

            pRgn = pInstrument->GetNextRegion();
            iRegion++;
        }
        dmsg(1,("OK\n"));
        DispatchResourceProgressEvent(Key, 1.0f); // done; notify all consumers about progress 100%

        // we need the following for destruction later
        instr_entry_t* pEntry = new instr_entry_t;
        pEntry->ID.FileName   = Key.FileName;
        pEntry->ID.Index      = Key.Index;
        pEntry->pGig          = pGig;

        gig::EngineChannel* pEngineChannel = (gig::EngineChannel*) pConsumer;
        // and we save this to check if we need to reallocate for a engine with higher value of 'MaxSamplesPerSecond'
        pEntry->MaxSamplesPerCycle =
            (pEngineChannel && pEngineChannel->GetEngine()) ? dynamic_cast<gig::Engine*>(pEngineChannel->GetEngine())->pAudioOutputDevice->MaxSamplesPerCycle()
                                          : GIG_RESOURCE_MANAGER_DEFAULT_MAX_SAMPLES_PER_CYCLE;
        pArg = pEntry;

        return pInstrument;
    }

    void InstrumentResourceManager::Destroy( ::gig::Instrument* pResource, void* pArg) {
        instr_entry_t* pEntry = (instr_entry_t*) pArg;
        // remove volume entry if necessary
        void* pCustomData = CustomData(pEntry->ID);
        if (pCustomData) {
            delete (float*)pCustomData;
            SetCustomData(pEntry->ID, NULL);
        }
        // we don't need the .gig file here anymore
        Gigs.HandBack(pEntry->pGig, (GigConsumer*) pEntry->ID.Index); // conversion kinda hackish :/
        delete pEntry;
    }

    void InstrumentResourceManager::OnBorrow(::gig::Instrument* pResource, InstrumentConsumer* pConsumer, void*& pArg) {
        instr_entry_t* pEntry = (instr_entry_t*) pArg;
        gig::EngineChannel* pEngineChannel = dynamic_cast<gig::EngineChannel*>(pConsumer);
        uint maxSamplesPerCycle =
            (pEngineChannel->GetEngine()) ? dynamic_cast<gig::Engine*>(pEngineChannel->GetEngine())->pAudioOutputDevice->MaxSamplesPerCycle()
                                          : GIG_RESOURCE_MANAGER_DEFAULT_MAX_SAMPLES_PER_CYCLE;
        if (pEntry->MaxSamplesPerCycle < maxSamplesPerCycle) {
            Update(pResource, pConsumer);
        }
    }

    /**
     *  Caches a certain size at the beginning of the given sample in RAM. If the
     *  sample is very short, the whole sample will be loaded into RAM and thus
     *  no disk streaming is needed for this sample. Caching an initial part of
     *  samples is needed to compensate disk reading latency.
     *
     *  @param pSample - points to the sample to be cached
     *  @param pEngineChannel - pointer to Gig Engine Channel which caused this call
     */
    void InstrumentResourceManager::CacheInitialSamples(::gig::Sample* pSample, gig::EngineChannel* pEngineChannel) {
        if (!pSample) {
            dmsg(4,("gig::InstrumentResourceManager: Skipping sample (pSample == NULL)\n"));
            return;
        }
        if (!pSample->SamplesTotal) return; // skip zero size samples

        if (pSample->SamplesTotal <= CONFIG_PRELOAD_SAMPLES) {
            // Sample is too short for disk streaming, so we load the whole
            // sample into RAM and place 'pAudioIO->FragmentSize << CONFIG_MAX_PITCH'
            // number of '0' samples (silence samples) behind the official buffer
            // border, to allow the interpolator do it's work even at the end of
            // the sample.
            const uint maxSamplesPerCycle =
                (pEngineChannel && pEngineChannel->GetEngine()) ? dynamic_cast<gig::Engine*>(pEngineChannel->GetEngine())->pAudioOutputDevice->MaxSamplesPerCycle()
                                              : GIG_RESOURCE_MANAGER_DEFAULT_MAX_SAMPLES_PER_CYCLE;
            const uint neededSilenceSamples = (maxSamplesPerCycle << CONFIG_MAX_PITCH) + 3;
            const uint currentlyCachedSilenceSamples = pSample->GetCache().NullExtensionSize / pSample->FrameSize;
            if (currentlyCachedSilenceSamples < neededSilenceSamples) {
                dmsg(3,("Caching whole sample (sample name: \"%s\", sample size: %d)\n", pSample->pInfo->Name.c_str(), pSample->SamplesTotal));
                ::gig::buffer_t buf = pSample->LoadSampleDataWithNullSamplesExtension(neededSilenceSamples);
                dmsg(4,("Cached %d Bytes, %d silence bytes.\n", buf.Size, buf.NullExtensionSize));
            }
        }
        else { // we only cache CONFIG_PRELOAD_SAMPLES and stream the other sample points from disk
            if (!pSample->GetCache().Size) pSample->LoadSampleData(CONFIG_PRELOAD_SAMPLES);
        }

        if (!pSample->GetCache().Size) std::cerr << "Unable to cache sample - maybe memory full!" << std::endl << std::flush;
    }



    // internal gig file manager

    ::gig::File* InstrumentResourceManager::GigResourceManager::Create(String Key, GigConsumer* pConsumer, void*& pArg) {
        dmsg(1,("Loading gig file \'%s\'...", Key.c_str()));
        ::RIFF::File* pRIFF = new ::RIFF::File(Key);
        ::gig::File* pGig   = new ::gig::File(pRIFF);
        pArg                = pRIFF;
        dmsg(1,("OK\n"));
        return pGig;
    }

    void InstrumentResourceManager::GigResourceManager::Destroy(::gig::File* pResource, void* pArg) {
        dmsg(1,("Freeing gig file from memory..."));
        delete pResource;
        delete (::RIFF::File*) pArg;
        dmsg(1,("OK\n"));
    }

}} // namespace LinuxSampler::gig
