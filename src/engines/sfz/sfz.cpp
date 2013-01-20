/***************************************************************************
 *                                                                         *
 *   LinuxSampler - modular, streaming capable sampler                     *
 *                                                                         *
 *   Copyright (C) 2008 Anders Dahnielson <anders@dahnielson.com>          *
 *   Copyright (C) 2009 - 2013 Anders Dahnielson and Grigor Iliev          *
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

#include "sfz.h"

#include <iostream>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "../../common/File.h"
#include "../../common/Path.h"
#include "LookupTable.h"

namespace sfz
{
    template <typename T> T check(std::string name, T min, T max, T val) {
        if (val < min) {
            std::cerr << "sfz: The value of opcode '" << name;
            std::cerr << "' is below the minimum allowed value (min=" << min << "): " << val << std::endl;
            val = min;
        }
        if (val > max) {
            std::cerr << "sfz: The value of opcode '" << name;
            std::cerr << "' is above the maximum allowed value (max=" << max << "): " << val << std::endl;
            val = max;
        }
        
        return val;
    }

    Sample* SampleManager::FindSample(std::string samplePath, uint offset, int end) {
        std::map<Sample*, std::set<Region*> >::iterator it = sampleMap.begin();
        for (; it != sampleMap.end(); it++) {
            if (it->first->GetFile() == samplePath) {
                /* Because the start of the sample is cached in RAM we treat
                 * same sample with different offset as different samples
                 * // TODO: Ignore offset when the whole sample is cached in RAM?
                 */
                if (it->first->Offset == offset && it->first->End == end) return it->first;
            }
        }

        return NULL;
    }

    /////////////////////////////////////////////////////////////
    // class optional

    const optional_base::nothing_t optional_base::nothing;

    /////////////////////////////////////////////////////////////
    // class Articulation

    Articulation::Articulation()
    {
    }

    Articulation::~Articulation()
    {
    }

    /////////////////////////////////////////////////////////////
    // class Definition

    Definition::Definition()
    {
    }

    Definition::~Definition()
    {
    }

    /////////////////////////////////////////////////////////////
    // class Region

    Region::Region()
    {
        pSample = NULL;
        seq_counter = 1;
    }

    Region::~Region()
    {
        DestroySampleIfNotUsed();
    }

    Sample* Region::GetSample(bool create)
    {
        if (pSample == NULL && create) {
            uint i = offset ? *offset : 0;
            Sample* sf = GetInstrument()->GetSampleManager()->FindSample(sample, i, end);
            if (sf != NULL) pSample = sf; // Reuse already created sample
            else pSample = new Sample(sample, false, i, end);
            GetInstrument()->GetSampleManager()->AddSampleConsumer(pSample, this);
        }
        return pSample;
    }

    void Region::DestroySampleIfNotUsed() {
        if (pSample == NULL) return;
        GetInstrument()->GetSampleManager()->RemoveSampleConsumer(pSample, this);
        if (!GetInstrument()->GetSampleManager()->HasSampleConsumers(pSample)) {
            GetInstrument()->GetSampleManager()->RemoveSample(pSample);
            delete pSample;
            pSample = NULL;
        }
    }

    bool Region::OnKey(const Query& q) {
        // As the region comes from a LookupTable search on the query,
        // the following parameters are not checked here: chan, key,
        // vel, chanaft, polyaft, prog, sw_previous, cc. They are all
        // handled by the lookup table.
        bool is_triggered(
            q.bend    >= lobend     &&  q.bend    <= hibend     &&
            q.bpm     >= lobpm      &&  q.bpm     <  hibpm      &&
            q.rand    >= lorand     &&  q.rand    <  hirand     &&
            q.timer   >= lotimer    &&  q.timer   <= hitimer    &&

            ( sw_last == -1 ||
              ((sw_last >= sw_lokey && sw_last <= sw_hikey) ? (q.last_sw_key == sw_last) : false) ) &&

            ( sw_down == -1 ||
              ((sw_down >= sw_lokey && (sw_hikey == -1 || sw_down <= sw_hikey)) ? (q.sw[sw_down]) : false) )  &&

            ( sw_up   == -1 ||
              ((sw_up   >= sw_lokey && (sw_hikey == -1 || sw_up   <= sw_hikey)) ? (!q.sw[sw_up]) : true) )  &&

            ((trigger & q.trig) != 0)
        );

        if (!is_triggered)
            return false;

        // seq_position has to be checked last, so we know that we
        // increment the right counter
        is_triggered = (seq_counter == seq_position);
        seq_counter = (seq_counter % seq_length) + 1;

        return is_triggered;
    }

    Articulation*
    Region::GetArticulation(int bend, uint8_t bpm, uint8_t chanaft, uint8_t polyaft, uint8_t* cc)
    {
        return new Articulation(); //todo: implement GetArticulation()
    }

    bool Region::HasLoop() {
        bool b = loop_mode == LOOP_UNSET ? pSample->GetLoops() :
            (loop_mode == LOOP_CONTINUOUS || loop_mode == LOOP_SUSTAIN);
        return b && GetLoopEnd() > GetLoopStart();
    }

    uint Region::GetLoopStart() {
        return (!loop_start) ? pSample->GetLoopStart() : *loop_start;
    }

    uint Region::GetLoopEnd() {
        return (!loop_end) ? pSample->GetLoopEnd() : *loop_end;
    }

    uint Region::GetLoopCount() {
        return (!count) ? 0 : *count;
    }

    /////////////////////////////////////////////////////////////
    // class Instrument

    Instrument::Instrument(std::string name, SampleManager* pSampleManager) : KeyBindings(128, false), KeySwitchBindings(128, false)
    {
        this->name = name;
        this->pSampleManager = pSampleManager ? pSampleManager : this;
        pLookupTable = 0;
        
        // The first 6 curves are defined internally (actually 7 with the one at index 0)
        Curve c;
        for (int i = 0; i < 128; i++) c.v[i] = i / 127.0f;
        curves.add(c); curves.add(c); curves.add(c); curves.add(c);
        curves.add(c); curves.add(c); curves.add(c);
        ///////
    }

    Instrument::~Instrument()
    {
        for (int i = 0; i < regions.size(); i++) {
            delete regions[i];
        }
        delete pLookupTable;
        for (int i = 0 ; i < 128 ; i++) {
            delete pLookupTableCC[i];
        }
    }

    void Query::search(const Instrument* pInstrument) {
        pRegionList = &pInstrument->pLookupTable->query(*this);
        regionIndex = 0;
    }

    void Query::search(const Instrument* pInstrument, int triggercc) {
        pRegionList = &pInstrument->pLookupTableCC[triggercc]->query(*this);
        regionIndex = 0;
    }

    Region* Query::next() {
        for ( ; regionIndex < pRegionList->size() ; regionIndex++) {
            if ((*pRegionList)[regionIndex]->OnKey(*this)) {
                return (*pRegionList)[regionIndex++];
            }
        }
        return 0;
    }

    bool Instrument::DestroyRegion(Region* pRegion) {
        for (std::vector<Region*>::iterator it = regions.begin(); it != regions.end(); it++) {
            if(*it == pRegion) {
                regions.erase(it);
                delete pRegion;
                return true;
            }
        }

        return false;
    }

    bool Instrument::HasKeyBinding(uint8_t key) {
        if (key > 127) return false;
        return KeyBindings[key];
    }

    bool Instrument::HasKeySwitchBinding(uint8_t key) {
        if (key > 127) return false;
        return KeySwitchBindings[key];
    }

    /////////////////////////////////////////////////////////////
    // class Group

    Group::Group() :
        id(0)
    {
        Reset();
    }

    Group::~Group()
    {
    }

    void
    Group::Reset()
    {
        // This is where all the default values are set.

        // sample definition default
        sample = "";

        // input control
        lochan = 1; hichan = 16;
        lokey = 0; hikey = 127;
        lovel = 0; hivel = 127;
        lobend = -8192; hibend = 8192;
        lobpm = 0; hibpm = 500;
        lochanaft = 0; hichanaft = 127;
        lopolyaft = 0; hipolyaft = 127;
        loprog = 0; hiprog = 127;
        lorand = 0.0; hirand = 1.0;
        lotimer = 0.0; hitimer = 0.0;

        seq_length = 1;
        seq_position = 1;

        sw_lokey = -1; sw_hikey = -1;
        sw_last = -1;
        sw_down = -1;
        sw_up = -1;
        sw_previous = -1;
        sw_vel = VEL_CURRENT;

        trigger = TRIGGER_ATTACK;

        group = 0;
        off_by = 0;
        off_mode = OFF_FAST;

        // sample player
        count.unset();
        delay.unset(); delay_random.unset();
        delay_beats.unset(); stop_beats.unset();
        delay_samples.unset();
        end = 0;
        loop_crossfade.unset();
        offset.unset(); offset_random.unset();
        loop_mode = LOOP_UNSET;
        loop_start.unset(); loop_end.unset();
        sync_beats.unset(); sync_offset.unset();

        // amplifier
        volume = 0;
        volume_oncc.clear();
        volume_curvecc.clear();
        volume_smoothcc.clear();
        volume_stepcc.clear();
        amplitude = 100;
        pan = 0;
        pan_oncc.clear();
        pan_curvecc.clear();
        pan_smoothcc.clear();
        pan_stepcc.clear();
        width = 100;
        position = 0;
        amp_keytrack = 0;
        amp_keycenter = 60;
        amp_veltrack = 100;
        amp_random = 0;
        rt_decay = 0;
        xfin_lokey = 0; xfin_hikey = 0;
        xfout_lokey = 127; xfout_hikey = 127;
        xf_keycurve = POWER;
        xfin_lovel = 0;    xfin_hivel = 0;
        xfout_lovel = 127; xfout_hivel = 127;
        xf_velcurve = POWER;
        xf_cccurve = POWER;

        // pitch
        transpose = 0;
        tune = 0;
        pitch_keycenter = 60;
        pitch_keytrack = 100;
        pitch_veltrack = 0;
        pitch_random = 0;
        bend_up = 200;
        bend_down = -200;
        bend_step = 1;
        
        pitch_oncc.clear();
        pitch_smoothcc.clear();
        pitch_curvecc.clear();
        pitch_stepcc.clear();

        // filter
        fil_type = LPF_2P;
        cutoff.unset();
        cutoff_chanaft = 0;
        cutoff_polyaft = 0;
        resonance = 0;
        fil_keytrack = 0;
        fil_keycenter = 60;
        fil_veltrack = 0;
        fil_random = 0;

        fil2_type = LPF_2P;
        cutoff2.unset();
        cutoff2_chanaft = 0;
        cutoff2_polyaft = 0;
        resonance2 = 0;
        fil2_keytrack = 0;
        fil2_keycenter = 60;
        fil2_veltrack = 0;
        fil2_random = 0;
        
        cutoff_oncc.clear();
        cutoff_smoothcc.clear();
        cutoff_curvecc.clear();
        cutoff_stepcc.clear();
        cutoff2_oncc.clear();
        cutoff2_smoothcc.clear();
        cutoff2_curvecc.clear();
        cutoff2_stepcc.clear();
        
        resonance_oncc.clear();
        resonance_smoothcc.clear();
        resonance_curvecc.clear();
        resonance_stepcc.clear();
        resonance2_oncc.clear();
        resonance2_smoothcc.clear();
        resonance2_curvecc.clear();
        resonance2_stepcc.clear();

        // per voice equalizer
        eq1_freq = 50;
        eq2_freq = 500;
        eq3_freq = 5000;
        eq1_vel2freq = 0;
        eq2_vel2freq = 0;
        eq3_vel2freq = 0;
        eq1_bw = 1;
        eq2_bw = 1;
        eq3_bw = 1;
        eq1_gain = 0;
        eq2_gain = 0;
        eq3_gain = 0;
        eq1_vel2gain = 0;
        eq2_vel2gain = 0;
        eq3_vel2gain = 0;

        // CCs
        for (int i = 0; i < 128; ++i)
        {
            // input control
            locc.set(i, 0);
            hicc.set(i, 127);
            start_locc.set(i, -1);
            start_hicc.set(i, -1);
            stop_locc.set(i, -1);
            stop_hicc.set(i, -1);
            on_locc.set(i, -1);
            on_hicc.set(i, -1);

            // sample player
            delay_oncc.set(i, optional<float>::nothing);
            delay_samples_oncc.set(i, optional<int>::nothing);
            offset_oncc.set(i, optional<int>::nothing);

            // amplifier
            amp_velcurve.set(i, -1);
            gain_oncc.set(i, 0);
            xfin_locc.set(i, 0);
            xfin_hicc.set(i, 0);
            xfout_locc.set(i, 0);
            xfout_hicc.set(i, 0);

            // per voice equalizer
            eq1_freq_oncc.set(i, 0);
            eq2_freq_oncc.set(i, 0);
            eq3_freq_oncc.set(i, 0);
            eq1_bw_oncc.set(i, 0);
            eq2_bw_oncc.set(i, 0);
            eq3_bw_oncc.set(i, 0);
            eq1_gain_oncc.set(i, 0);
            eq2_gain_oncc.set(i, 0);
            eq3_gain_oncc.set(i, 0);
        }

        eg.clear();
        lfos.clear();

        // deprecated
        ampeg_delay    = 0;
        ampeg_start    = 0; //in percentage
        ampeg_attack   = 0;
        ampeg_hold     = 0;
        ampeg_decay    = 0;
        ampeg_sustain  = -1; // in percentage
        ampeg_release  = 0;

        ampeg_vel2delay   = 0;
        ampeg_vel2attack  = 0;
        ampeg_vel2hold    = 0;
        ampeg_vel2decay   = 0;
        ampeg_vel2sustain = 0;
        ampeg_vel2release = 0;
        
        ampeg_delaycc.clear();
        ampeg_startcc.clear();
        ampeg_attackcc.clear();
        ampeg_holdcc.clear();
        ampeg_decaycc.clear();
        ampeg_sustaincc.clear();
        ampeg_releasecc.clear();

        fileg_delay    = 0;
        fileg_start    = 0; //in percentage
        fileg_attack   = 0;
        fileg_hold     = 0;
        fileg_decay    = 0;
        fileg_sustain  = 100; // in percentage
        fileg_release  = 0;

        fileg_vel2delay   = 0;
        fileg_vel2attack  = 0;
        fileg_vel2hold    = 0;
        fileg_vel2decay   = 0;
        fileg_vel2sustain = 0;
        fileg_vel2release = 0;
        fileg_depth       = 0;
        
        fileg_delay_oncc.clear();
        fileg_start_oncc.clear();
        fileg_attack_oncc.clear();
        fileg_hold_oncc.clear();
        fileg_decay_oncc.clear();
        fileg_sustain_oncc.clear();
        fileg_release_oncc.clear();
        fileg_depth_oncc.clear();

        pitcheg_delay    = 0;
        pitcheg_start    = 0; //in percentage
        pitcheg_attack   = 0;
        pitcheg_hold     = 0;
        pitcheg_decay    = 0;
        pitcheg_sustain  = 100; // in percentage
        pitcheg_release  = 0;
        pitcheg_depth    = 0;

        pitcheg_vel2delay   = 0;
        pitcheg_vel2attack  = 0;
        pitcheg_vel2hold    = 0;
        pitcheg_vel2decay   = 0;
        pitcheg_vel2sustain = 0;
        pitcheg_vel2release = 0;
        
        pitcheg_delay_oncc.clear();
        pitcheg_start_oncc.clear();
        pitcheg_attack_oncc.clear();
        pitcheg_hold_oncc.clear();
        pitcheg_decay_oncc.clear();
        pitcheg_sustain_oncc.clear();
        pitcheg_release_oncc.clear();
        pitcheg_depth_oncc.clear();

        amplfo_delay     = 0;
        amplfo_fade      = 0;
        amplfo_freq      = -1; /* -1 is used to determine whether the LFO was initialized */
        amplfo_depth     = 0;
        amplfo_delay_oncc.clear();
        amplfo_fade_oncc.clear();
        amplfo_depthcc.clear();
        amplfo_freqcc.clear();

        fillfo_delay     = 0;
        fillfo_fade      = 0;
        fillfo_freq      = -1; /* -1 is used to determine whether the LFO was initialized */
        fillfo_depth     = 0;
        fillfo_delay_oncc.clear();
        fillfo_fade_oncc.clear();
        fillfo_depthcc.clear();
        fillfo_freqcc.clear();

        pitchlfo_delay   = 0;
        pitchlfo_fade    = 0;
        pitchlfo_freq    = -1; /* -1 is used to determine whether the LFO was initialized */
        pitchlfo_depth   = 0;
        pitchlfo_delay_oncc.clear();
        pitchlfo_fade_oncc.clear();
        pitchlfo_depthcc.clear();
        pitchlfo_freqcc.clear();
    }

    Region*
    Group::RegionFactory()
    {
        // This is where the current group setting are copied to the new region.

        Region* region = new Region();

        region->id = id++;

        // sample definition
        region->sample = sample;

        // input control
        region->lochan = lochan;
        region->hichan = hichan;
        region->lokey = lokey;
        region->hikey = hikey;
        region->lovel = lovel;
        region->hivel = hivel;
        region->locc = locc;
        region->hicc = hicc;
        region->lobend = lobend;
        region->hibend = hibend;
        region->lobpm = lobpm;
        region->hibpm = hibpm;
        region->lochanaft = lochanaft;
        region->hichanaft = hichanaft;
        region->lopolyaft = lopolyaft;
        region->hipolyaft = hipolyaft;
        region->loprog = loprog;
        region->hiprog = hiprog;
        region->lorand = lorand;
        region->hirand = hirand;
        region->lotimer = lotimer;
        region->hitimer = hitimer;
        region->seq_length = seq_length;
        region->seq_position = seq_position;
        region->start_locc = start_locc;
        region->start_hicc = start_hicc;
        region->stop_locc = stop_locc;
        region->stop_hicc = stop_hicc;
        region->sw_lokey = sw_lokey;
        region->sw_hikey = sw_hikey;
        region->sw_last = sw_last;
        region->sw_down = sw_down;
        region->sw_up = sw_up;
        region->sw_previous = sw_previous;
        region->sw_vel = sw_vel;
        region->trigger = trigger;
        region->group = group;
        region->off_by = off_by;
        region->off_mode = off_mode;
        region->on_locc = on_locc;
        region->on_hicc = on_hicc;

        // sample player
        region->count = count;
        region->delay = delay;
        region->delay_random = delay_random;
        region->delay_oncc = delay_oncc;
        region->delay_beats = delay_beats;
        region->stop_beats = stop_beats;
        region->delay_samples = delay_samples;
        region->delay_samples_oncc = delay_samples_oncc;
        region->end = end;
        region->loop_crossfade = loop_crossfade;
        region->offset = offset;
        region->offset_random = offset_random;
        region->offset_oncc = offset_oncc;
        region->loop_mode = loop_mode;
        region->loop_start = loop_start;
        region->loop_end = loop_end;
        region->sync_beats = sync_beats;
        region->sync_offset = sync_offset;

        // amplifier
        region->volume = volume;
        region->volume_oncc = volume_oncc;
        region->volume_curvecc = volume_curvecc;
        region->volume_smoothcc = volume_smoothcc;
        region->volume_stepcc = volume_stepcc;
        region->amplitude = amplitude;
        region->pan = pan;
        region->pan_oncc = pan_oncc;
        region->pan_curvecc = pan_curvecc;
        region->pan_smoothcc = pan_smoothcc;
        region->pan_stepcc = pan_stepcc;
        region->width = width;
        region->position = position;
        region->amp_keytrack = amp_keytrack;
        region->amp_keycenter = amp_keycenter;
        region->amp_veltrack = amp_veltrack;
        region->amp_velcurve = amp_velcurve;
        region->amp_random = amp_random;
        region->rt_decay = rt_decay;
        region->gain_oncc = gain_oncc;
        region->xfin_lokey = xfin_lokey;
        region->xfin_hikey = xfin_hikey;
        region->xfout_lokey = xfout_lokey;
        region->xfout_hikey = xfout_hikey;
        region->xf_keycurve = xf_keycurve;
        region->xfin_lovel = xfin_lovel;
        region->xfin_hivel = xfin_lovel;
        region->xfout_lovel = xfout_lovel;
        region->xfout_hivel = xfout_hivel;
        region->xf_velcurve = xf_velcurve;
        region->xfin_locc = xfin_locc;
        region->xfin_hicc = xfin_hicc;
        region->xfout_locc = xfout_locc;
        region->xfout_hicc = xfout_hicc;
        region->xf_cccurve = xf_cccurve;

        // pitch
        region->transpose = transpose;
        region->tune = tune;
        region->pitch_keycenter = pitch_keycenter;
        region->pitch_keytrack = pitch_keytrack;
        region->pitch_veltrack = pitch_veltrack;
        region->pitch_random = pitch_random;
        region->bend_up = bend_up;
        region->bend_down = bend_down;
        region->bend_step = bend_step;
        
        region->pitch_oncc     = pitch_oncc;
        region->pitch_smoothcc = pitch_smoothcc;
        region->pitch_curvecc  = pitch_curvecc;
        region->pitch_stepcc   = pitch_stepcc;

        // filter
        region->fil_type = fil_type;
        region->cutoff = cutoff;
        region->cutoff_oncc = cutoff_oncc;
        region->cutoff_smoothcc = cutoff_smoothcc;
        region->cutoff_stepcc = cutoff_stepcc;
        region->cutoff_curvecc = cutoff_curvecc;
        region->cutoff_chanaft = cutoff_chanaft;
        region->cutoff_polyaft = cutoff_polyaft;
        region->resonance = resonance;
        region->resonance_oncc = resonance_oncc;
        region->resonance_smoothcc = resonance_smoothcc;
        region->resonance_stepcc = resonance_stepcc;
        region->resonance_curvecc = resonance_curvecc;
        region->fil_keytrack = fil_keytrack;
        region->fil_keycenter = fil_keycenter;
        region->fil_veltrack = fil_veltrack;
        region->fil_random = fil_random;

        region->fil2_type = fil2_type;
        region->cutoff2 = cutoff2;
        region->cutoff2_oncc = cutoff2_oncc;
        region->cutoff2_smoothcc = cutoff2_smoothcc;
        region->cutoff2_stepcc = cutoff2_stepcc;
        region->cutoff2_curvecc = cutoff2_curvecc;
        region->cutoff2_chanaft = cutoff2_chanaft;
        region->cutoff2_polyaft = cutoff2_polyaft;
        region->resonance2 = resonance2;
        region->resonance2_oncc = resonance2_oncc;
        region->resonance2_smoothcc = resonance2_smoothcc;
        region->resonance2_stepcc = resonance2_stepcc;
        region->resonance2_curvecc = resonance2_curvecc;
        region->fil2_keytrack = fil2_keytrack;
        region->fil2_keycenter = fil2_keycenter;
        region->fil2_veltrack = fil2_veltrack;
        region->fil2_random = fil2_random;

        // per voice equalizer
        region->eq1_freq = eq1_freq;
        region->eq2_freq = eq2_freq;
        region->eq3_freq = eq3_freq;
        region->eq1_freq_oncc = eq1_freq_oncc;
        region->eq2_freq_oncc = eq2_freq_oncc;
        region->eq3_freq_oncc = eq3_freq_oncc;
        region->eq1_vel2freq = eq1_vel2freq;
        region->eq2_vel2freq = eq2_vel2freq;
        region->eq3_vel2freq = eq3_vel2freq;
        region->eq1_bw = eq1_bw;
        region->eq2_bw = eq2_bw;
        region->eq3_bw = eq3_bw;
        region->eq1_bw_oncc = eq1_bw_oncc;
        region->eq2_bw_oncc = eq2_bw_oncc;
        region->eq3_bw_oncc = eq3_bw_oncc;
        region->eq1_gain = eq1_gain;
        region->eq2_gain = eq2_gain;
        region->eq3_gain = eq3_gain;
        region->eq1_gain_oncc = eq1_gain_oncc;
        region->eq2_gain_oncc = eq2_gain_oncc;
        region->eq3_gain_oncc = eq3_gain_oncc;
        region->eq1_vel2gain = eq1_vel2gain;
        region->eq2_vel2gain = eq2_vel2gain;
        region->eq3_vel2gain = eq3_vel2gain;

        // envelope generator
        region->eg = eg;

        // deprecated
        region->ampeg_delay    = ampeg_delay;
        region->ampeg_start    = ampeg_start;
        region->ampeg_attack   = ampeg_attack;
        region->ampeg_hold     = ampeg_hold;
        region->ampeg_decay    = ampeg_decay;
        region->ampeg_sustain  = ampeg_sustain;
        region->ampeg_release  = ampeg_release;

        region->ampeg_vel2delay   = ampeg_vel2delay;
        region->ampeg_vel2attack  = ampeg_vel2attack;
        region->ampeg_vel2hold    = ampeg_vel2hold;
        region->ampeg_vel2decay   = ampeg_vel2decay;
        region->ampeg_vel2sustain = ampeg_vel2sustain;
        region->ampeg_vel2release = ampeg_vel2release;
        
        region->ampeg_delaycc   = ampeg_delaycc;
        region->ampeg_startcc   = ampeg_startcc;
        region->ampeg_attackcc  = ampeg_attackcc;
        region->ampeg_holdcc    = ampeg_holdcc;
        region->ampeg_decaycc   = ampeg_decaycc;
        region->ampeg_sustaincc = ampeg_sustaincc;
        region->ampeg_releasecc = ampeg_releasecc;

        region->fileg_delay    = fileg_delay;
        region->fileg_start    = fileg_start;
        region->fileg_attack   = fileg_attack;
        region->fileg_hold     = fileg_hold;
        region->fileg_decay    = fileg_decay;
        region->fileg_sustain  = fileg_sustain;
        region->fileg_release  = fileg_release;
        region->fileg_depth    = fileg_depth;

        region->fileg_vel2delay   = fileg_vel2delay;
        region->fileg_vel2attack  = fileg_vel2attack;
        region->fileg_vel2hold    = fileg_vel2hold;
        region->fileg_vel2decay   = fileg_vel2decay;
        region->fileg_vel2sustain = fileg_vel2sustain;
        region->fileg_vel2release = fileg_vel2release;
        
        region->fileg_delay_oncc   = fileg_delay_oncc;
        region->fileg_start_oncc   = fileg_start_oncc;
        region->fileg_attack_oncc  = fileg_attack_oncc;
        region->fileg_hold_oncc    = fileg_hold_oncc;
        region->fileg_decay_oncc   = fileg_decay_oncc;
        region->fileg_sustain_oncc = fileg_sustain_oncc;
        region->fileg_release_oncc = fileg_release_oncc;
        region->fileg_depth_oncc   = fileg_depth_oncc;

        region->pitcheg_delay    = pitcheg_delay;
        region->pitcheg_start    = pitcheg_start;
        region->pitcheg_attack   = pitcheg_attack;
        region->pitcheg_hold     = pitcheg_hold;
        region->pitcheg_decay    = pitcheg_decay;
        region->pitcheg_sustain  = pitcheg_sustain;
        region->pitcheg_release  = pitcheg_release;
        region->pitcheg_depth    = pitcheg_depth;

        region->pitcheg_vel2delay   = pitcheg_vel2delay;
        region->pitcheg_vel2attack  = pitcheg_vel2attack;
        region->pitcheg_vel2hold    = pitcheg_vel2hold;
        region->pitcheg_vel2decay   = pitcheg_vel2decay;
        region->pitcheg_vel2sustain = pitcheg_vel2sustain;
        region->pitcheg_vel2release = pitcheg_vel2release;
        
        region->pitcheg_delay_oncc   = pitcheg_delay_oncc;
        region->pitcheg_start_oncc   = pitcheg_start_oncc;
        region->pitcheg_attack_oncc  = pitcheg_attack_oncc;
        region->pitcheg_hold_oncc    = pitcheg_hold_oncc;
        region->pitcheg_decay_oncc   = pitcheg_decay_oncc;
        region->pitcheg_sustain_oncc = pitcheg_sustain_oncc;
        region->pitcheg_release_oncc = pitcheg_release_oncc;
        region->pitcheg_depth_oncc   = pitcheg_depth_oncc;

        region->amplfo_delay     = amplfo_delay;
        region->amplfo_fade      = amplfo_fade;
        region->amplfo_freq      = amplfo_freq;
        region->amplfo_depth     = amplfo_depth;
        
        region->amplfo_delay_oncc = amplfo_delay_oncc;
        region->amplfo_fade_oncc  = amplfo_fade_oncc;
        region->amplfo_depthcc   = amplfo_depthcc;
        region->amplfo_freqcc    = amplfo_freqcc;

        region->fillfo_delay     = fillfo_delay;
        region->fillfo_fade      = fillfo_fade;
        region->fillfo_freq      = fillfo_freq;
        region->fillfo_depth     = fillfo_depth;
        
        region->fillfo_delay_oncc = fillfo_delay_oncc;
        region->fillfo_fade_oncc  = fillfo_fade_oncc;
        region->fillfo_depthcc   = fillfo_depthcc;
        region->fillfo_freqcc    = fillfo_freqcc;

        region->pitchlfo_delay   = pitchlfo_delay;
        region->pitchlfo_fade    = pitchlfo_fade;
        region->pitchlfo_freq    = pitchlfo_freq;
        region->pitchlfo_depth   = pitchlfo_depth;
        
        region->pitchlfo_delay_oncc = pitchlfo_delay_oncc;
        region->pitchlfo_fade_oncc  = pitchlfo_fade_oncc;
        region->pitchlfo_depthcc = pitchlfo_depthcc;
        region->pitchlfo_freqcc  = pitchlfo_freqcc;
        
        region->eg = eg;
        region->lfos = lfos;

        return region;
    }

    /////////////////////////////////////////////////////////////
    // class File

    File::File(std::string file, SampleManager* pSampleManager) :
        _current_section(GROUP),
        default_path(""),
        octave_offset(0),
        note_offset(0)
    {
        _instrument = new Instrument(LinuxSampler::Path::getBaseName(file), pSampleManager);
        _current_group = new Group();
        pCurDef = _current_group;
        enum token_type_t { HEADER, OPCODE };
        token_type_t token_type;
        std::string token_string;

        std::ifstream fs(file.c_str());
        currentDir = LinuxSampler::Path::stripLastName(file);
        std::string token;
        std::string line;
        currentLine = 0;

        while (std::getline(fs, line))
        {
            currentLine++;
            // COMMENT
            std::string::size_type slash_index = line.find("//");
            if (slash_index != std::string::npos)
                line.resize(slash_index);

            // DEFINITION
            std::stringstream linestream(line);
            int spaces = 0;
            while (linestream >> token)
            {
                linestream >> std::noskipws;
                if (token[0] == '<' && token[token.size()-1] == '>')
                {
                    // HEAD
                    if (!token_string.empty())
                    {
                        switch (token_type)
                        {
                        case HEADER:
                            push_header(token_string);
                            break;
                        case OPCODE:
                            push_opcode(token_string);
                            break;
                        }
                        token_string.erase();
                    }
                    token_string.append(token);
                    token_type = HEADER;
                }
                else if (token.find('=') != std::string::npos)
                {
                    // HEAD
                    if (!token_string.empty())
                    {
                        switch (token_type)
                        {
                        case HEADER:
                            push_header(token_string);
                            break;
                        case OPCODE:
                            push_opcode(token_string);
                            break;
                        }
                        token_string.erase();
                    }
                    token_string.append(token);
                    token_type = OPCODE;
                }
                else
                {
                    // TAIL
                    token_string.append(spaces, ' ');
                    token_string.append(token);
                }
                spaces = 0;
                while (isspace(linestream.peek())) {
                    linestream.ignore();
                    spaces++;
                }
            }

            // EOL
            if (!token_string.empty())
            {
                switch (token_type)
                {
                case HEADER:
                    push_header(token_string);
                    break;
                case OPCODE:
                    push_opcode(token_string);
                    break;
                }
                token_string.erase();
            }
        }

        std::set<float*> velcurves;
        for (int i = 0; i < _instrument->regions.size(); i++) {
            ::sfz::Region* pRegion = _instrument->regions[i];
            int low = pRegion->lokey;
            int high = pRegion->hikey;
            if (low != -1) { // lokey -1 means region doesn't play on note-on
                // hikey -1 is the same as no limit, except that it
                // also enables on_locc/on_hicc
                if (high == -1) high = 127;
                if (low < 0 || low > 127 || high < 0 || high > 127 || low > high) {
                    std::cerr << "Invalid key range: " << low << " - " << high << std::endl;
                } else {
                    for (int j = low; j <= high; j++) _instrument->KeyBindings[j] = true;
                }
            }

            // get keyswitches
            low = pRegion->sw_lokey;
            if (low < 0) low = 0;
            high = pRegion->sw_hikey;
            if (high == -1) {
                // Key switches not defined, so nothing to do
            } else if (low >= 0 && low <= 127 && high >= 0 && high <= 127 && high >= low) {
                for (int j = low; j <= high; j++) _instrument->KeySwitchBindings[j] = true;
            } else {
                std::cerr << "Invalid key switch range: " << low << " - " << high << std::endl;
            }

            // create velocity response curve

            // don't use copy-on-write here, instead change the actual
            // unique buffers in memory
            float* velcurve = const_cast<float*>(&pRegion->amp_velcurve[0]);
            if (velcurves.insert(velcurve).second) {
                int prev = 0;
                float prevvalue = 0;
                for (int v = 0 ; v < 128 ; v++) {
                    if (velcurve[v] >= 0) {
                        float step = (velcurve[v] - prevvalue) / (v - prev);
                        for ( ; prev < v ; prev++) {
                            velcurve[prev] = prevvalue;
                            prevvalue += step;
                        }
                    }
                }
                if (prev) {
                    float step = (1 - prevvalue) / (127 - prev);
                    for ( ; prev < 128 ; prev++) {
                        velcurve[prev] = prevvalue;
                        prevvalue += step;
                    }
                } else {
                    // default curve
                    for (int v = 0 ; v < 128 ; v++) {
                        velcurve[v] = v * v / (127.0 * 127.0);
                    }
                }
            }
        }

        _instrument->pLookupTable = new LookupTable(_instrument);

        // create separate lookup tables for controller triggered
        // regions, one for each CC
        for (int i = 0 ; i < 128 ; i++) {
            _instrument->pLookupTableCC[i] = new LookupTable(_instrument, i);
        }
        
        for (int i = 0; i < _instrument->regions.size(); i++) {
            Region* r = _instrument->regions[i];
            
            copyCurves(r->volume_curvecc, r->volume_oncc);
            r->volume_curvecc.clear();
            
            copySmoothValues(r->volume_smoothcc, r->volume_oncc);
            r->volume_smoothcc.clear();
            
            copyStepValues(r->volume_stepcc, r->volume_oncc);
            r->volume_stepcc.clear();
            
            copyCurves(r->pitch_curvecc, r->pitch_oncc);
            r->pitch_curvecc.clear();
            
            copySmoothValues(r->pitch_smoothcc, r->pitch_oncc);
            r->pitch_smoothcc.clear();
            
            copyStepValues(r->pitch_stepcc, r->pitch_oncc);
            r->pitch_stepcc.clear();
            
            copyCurves(r->pan_curvecc, r->pan_oncc);
            r->pan_curvecc.clear();
            
            copySmoothValues(r->pan_smoothcc, r->pan_oncc);
            r->pan_smoothcc.clear();
            
            copyStepValues(r->pan_stepcc, r->pan_oncc);
            r->pan_stepcc.clear();
            
            copyCurves(r->cutoff_curvecc, r->cutoff_oncc);
            r->cutoff_curvecc.clear();
            
            copySmoothValues(r->cutoff_smoothcc, r->cutoff_oncc);
            r->cutoff_smoothcc.clear();
            
            copyStepValues(r->cutoff_stepcc, r->cutoff_oncc);
            r->cutoff_stepcc.clear();
            
            copyCurves(r->cutoff2_curvecc, r->cutoff2_oncc);
            r->cutoff2_curvecc.clear();
            
            copySmoothValues(r->cutoff2_smoothcc, r->cutoff2_oncc);
            r->cutoff2_smoothcc.clear();
            
            copyStepValues(r->cutoff2_stepcc, r->cutoff2_oncc);
            r->cutoff2_stepcc.clear();
            
            copyCurves(r->resonance_curvecc, r->resonance_oncc);
            r->resonance_curvecc.clear();
            
            copySmoothValues(r->resonance_smoothcc, r->resonance_oncc);
            r->resonance_smoothcc.clear();
            
            copyStepValues(r->resonance_stepcc, r->resonance_oncc);
            r->resonance_stepcc.clear();
            
            copyCurves(r->resonance2_curvecc, r->resonance2_oncc);
            r->resonance2_curvecc.clear();
            
            copySmoothValues(r->resonance2_smoothcc, r->resonance2_oncc);
            r->resonance2_smoothcc.clear();
            
            copyStepValues(r->resonance2_stepcc, r->resonance2_oncc);
            r->resonance2_stepcc.clear();
            
            for (int j = 0; j < r->eg.size(); j++) {
                copyCurves(r->eg[j].pan_curvecc, r->eg[j].pan_oncc);
                r->eg[j].pan_curvecc.clear();
            }
            
            for (int j = 0; j < r->lfos.size(); j++) {
                r->lfos[j].copySmoothValues();
                r->lfos[j].copyStepValues();
                
                copySmoothValues(r->lfos[j].volume_smoothcc, r->lfos[j].volume_oncc);
                r->lfos[j].volume_smoothcc.clear();
                
                copyStepValues(r->lfos[j].volume_stepcc, r->lfos[j].volume_oncc);
                r->lfos[j].volume_stepcc.clear();
                
                copySmoothValues(r->lfos[j].freq_smoothcc, r->lfos[j].freq_oncc);
                r->lfos[j].freq_smoothcc.clear();
                
                copyStepValues(r->lfos[j].freq_stepcc, r->lfos[j].freq_oncc);
                r->lfos[j].freq_stepcc.clear();
                
                copySmoothValues(r->lfos[j].pitch_smoothcc, r->lfos[j].pitch_oncc);
                r->lfos[j].pitch_smoothcc.clear();
                
                copyStepValues(r->lfos[j].pitch_stepcc, r->lfos[j].pitch_oncc);
                r->lfos[j].pitch_stepcc.clear();
                
                copySmoothValues(r->lfos[j].pan_smoothcc, r->lfos[j].pan_oncc);
                r->lfos[j].pan_smoothcc.clear();
                
                copyStepValues(r->lfos[j].pan_stepcc, r->lfos[j].pan_oncc);
                r->lfos[j].pan_stepcc.clear();
                
                copySmoothValues(r->lfos[j].cutoff_smoothcc, r->lfos[j].cutoff_oncc);
                r->lfos[j].cutoff_smoothcc.clear();
                
                copyStepValues(r->lfos[j].cutoff_stepcc, r->lfos[j].cutoff_oncc);
                r->lfos[j].cutoff_stepcc.clear();
                
                copySmoothValues(r->lfos[j].resonance_smoothcc, r->lfos[j].resonance_oncc);
                r->lfos[j].resonance_smoothcc.clear();
                
                copyStepValues(r->lfos[j].resonance_stepcc, r->lfos[j].resonance_oncc);
                r->lfos[j].resonance_stepcc.clear();
            }
        }
    }

    File::~File()
    {
        delete _current_group;
        delete _instrument;
    }

    Instrument*
    File::GetInstrument()
    {
        return _instrument;
    }
    
    void File::copyCurves(LinuxSampler::ArrayList<CC>& curves, LinuxSampler::ArrayList<CC>& dest) {
        for (int i = 0; i < curves.size(); i++) {
            for (int j = 0; j < dest.size(); j++) {
                if (curves[i].Controller == dest[j].Controller) {
                    dest[j].Curve = curves[i].Curve;
                }
            }
        }
    }
    
    void File::copySmoothValues(LinuxSampler::ArrayList<CC>& smooths, LinuxSampler::ArrayList<CC>& dest) {
        for (int i = 0; i < smooths.size(); i++) {
            for (int j = 0; j < dest.size(); j++) {
                if (smooths[i].Controller == dest[j].Controller) {
                    dest[j].Smooth = smooths[i].Smooth;
                }
            }
        }
    }
    
    void File::copyStepValues(LinuxSampler::ArrayList<CC>& steps, LinuxSampler::ArrayList<CC>& dest) {
        for (int i = 0; i < steps.size(); i++) {
            for (int j = 0; j < dest.size(); j++) {
                if (steps[i].Controller == dest[j].Controller) {
                    dest[j].Step = steps[i].Step;
                }
            }
        }
    }
    
    int File::ToInt(const std::string& s) throw(LinuxSampler::Exception) {
        int i;
        std::istringstream iss(s);
        if(!(iss >> i)) {
            std::ostringstream oss;
            oss << "Line " << currentLine << ": Expected an integer";
            throw LinuxSampler::Exception(oss.str());
        }
        return i;
    }

    float File::ToFloat(const std::string& s) throw(LinuxSampler::Exception) {
        float i;
        std::istringstream iss(s);
        if(!(iss >> i)) {
            std::ostringstream oss;
            oss << "Line " << currentLine << ": Expected a floating-point number";
            throw LinuxSampler::Exception(oss.str());
        }
        return i;
    }

    void
    File::push_header(std::string token)
    {
        if (token == "<group>")
        {
            _current_section = GROUP;
            _current_group->Reset();
            pCurDef = _current_group;
        }
        else if (token == "<region>")
        {
            _current_section = REGION;
            _current_region = _current_group->RegionFactory();
            pCurDef = _current_region;
            _instrument->regions.push_back(_current_region);
            _current_region->SetInstrument(_instrument);
        }
        else if (token == "<control>")
        {
            _current_section = CONTROL;
            default_path = "";
            octave_offset = 0;
            note_offset = 0;
        }
        else if (token == "<curve>")
        {
            _current_section = CURVE;
            _instrument->curves.add(Curve());
            _current_curve = &_instrument->curves[_instrument->curves.size() - 1];
        }
        else
        {
            _current_section = UNKNOWN;
            std::cerr << "The header '" << token << "' is unsupported by libsfz!" << std::endl;
        }
    }

    void
    File::push_opcode(std::string token)
    {
        if (_current_section == UNKNOWN)
            return;

        std::string::size_type delimiter_index = token.find('=');
        std::string key = token.substr(0, delimiter_index);
        std::string value = token.substr(delimiter_index + 1);
        int x, y, z;
        
        if (_current_section == CURVE) {
            if (sscanf(key.c_str(), "v%d", &x)) {
                if (x < 0 || x > 127) {
                    std::cerr << "Invalid curve index: " << x << std::endl;
                }
                _current_curve->v[x] = check(key, 0.0f, 1.0f, ToFloat(value));
            } else {
                std::cerr << "The opcode '" << key << "' in section <curve> is unsupported by libsfz!" << std::endl;
            }
            
            return;
        }

        // sample definition
        if ("sample" == key)
        {
            std::string path = default_path + value;
            #ifndef WIN32
            for (int i = 0; i < path.length(); i++) if (path[i] == '\\') path[i] = '/';
            bool absolute = path[0] == '/';
            #else
            bool absolute = path[0] == '/' || path[0] == '\\' ||
                (path.length() >= 2 && isalpha(path[0]) && path[1] == ':');
            #endif
            if (!absolute) path = currentDir + LinuxSampler::File::DirSeparator + path;
            if(pCurDef) pCurDef->sample = path;
            return;
        }

        // control header directives
        else if ("default_path" == key)
        {
            switch (_current_section)
            {
            case CONTROL:
                default_path = value;
            }
            return;
        }
        else if ("octave_offset" == key)
        {
            switch (_current_section)
            {
            case CONTROL:
                octave_offset = ToInt(value);
            }
            return;
        }
        else if ("note_offset" == key)
        {
            switch (_current_section)
            {
            case CONTROL:
                note_offset = ToInt(value);
            }
            return;
        }

        // input controls
        else if ("lochan" == key) pCurDef->lochan = ToInt(value);
        else if ("hichan" == key) pCurDef->hichan = ToInt(value);
        else if ("lokey"  == key) pCurDef->lokey  = parseKey(value);
        else if ("hikey"  == key) pCurDef->hikey  = parseKey(value);
        else if ("key" == key)
        {
            pCurDef->lokey = pCurDef->hikey = pCurDef->pitch_keycenter = parseKey(value);
        }
        else if ("lovel"  == key) pCurDef->lovel = ToInt(value);
        else if ("hivel"  == key) pCurDef->hivel = ToInt(value);
        else if ("lobend" == key) pCurDef->lobend = ToInt(value);
        else if ("hibend" == key) pCurDef->hibend = ToInt(value);
        else if ("lobpm"  == key) pCurDef->lobpm = ToFloat(value);
        else if ("hibpm"  == key) pCurDef->hibpm = ToFloat(value);
        else if ("lochanaft" == key) pCurDef->lochanaft = ToInt(value);
        else if ("hichanaft" == key) pCurDef->hichanaft = ToInt(value);
        else if ("lopolyaft" == key) pCurDef->lopolyaft = ToInt(value);
        else if ("hipolyaft" == key) pCurDef->hipolyaft = ToInt(value);
        else if ("loprog"  == key) pCurDef->loprog = ToInt(value);
        else if ("hiprog"  == key) pCurDef->hiprog = ToInt(value);
        else if ("lorand"  == key) pCurDef->lorand = ToFloat(value);
        else if ("hirand"  == key) pCurDef->hirand = ToFloat(value);
        else if ("lotimer" == key) pCurDef->lotimer = ToFloat(value);
        else if ("hitimer" == key) pCurDef->hitimer = ToFloat(value);
        else if ("seq_length"   == key) pCurDef->seq_length = ToInt(value);
        else if ("seq_position" == key) pCurDef->seq_position = ToInt(value);
        else if ("sw_lokey" == key) pCurDef->sw_lokey = parseKey(value);
        else if ("sw_hikey" == key) pCurDef->sw_hikey = parseKey(value);
        else if ("sw_last"  == key) pCurDef->sw_last = parseKey(value);
        else if ("sw_down"  == key) pCurDef->sw_down = parseKey(value);
        else if ("sw_up"    == key) pCurDef->sw_up = parseKey(value);
        else if ("sw_previous" == key) pCurDef->sw_previous = parseKey(value);
        else if ("sw_vel" == key)
        {
            if (value == "current") pCurDef->sw_vel = VEL_CURRENT;
            else if (value == "previous") pCurDef->sw_vel = VEL_PREVIOUS;
        }
        else if ("trigger" == key)
        {
            if (value == "attack") pCurDef->trigger = TRIGGER_ATTACK;
            else if (value == "release") pCurDef->trigger = TRIGGER_RELEASE;
            else if (value == "first")   pCurDef->trigger = TRIGGER_FIRST;
            else if (value == "legato")  pCurDef->trigger = TRIGGER_LEGATO;
        }
        else if ("group"  == key) pCurDef->group = ToInt(value);
        else if ("off_by" == key || "offby" == key) pCurDef->off_by = ToInt(value);
        else if ("off_mode" == key || "offmode" == key)
        {
            if (value == "fast")  pCurDef->off_mode = OFF_FAST;
            else if (value == "normal") pCurDef->off_mode = OFF_NORMAL;
        }

        // sample player
        else if ("count" == key) { pCurDef->count = ToInt(value); pCurDef->loop_mode = ONE_SHOT; }
        else if ("delay" == key) pCurDef->delay = ToFloat(value);
        else if ("delay_random"   == key) pCurDef->delay_random = ToFloat(value);
        else if ("delay_beats"    == key) pCurDef->delay_beats = ToInt(value);
        else if ("stop_beats"     == key) pCurDef->stop_beats = ToInt(value);
        else if ("delay_samples"  == key) pCurDef->delay_samples = ToInt(value);
        else if ("end"            == key) pCurDef->end = ToInt(value);
        else if ("loop_crossfade" == key) pCurDef->loop_crossfade = ToFloat(value);
        else if ("offset_random"  == key) pCurDef->offset_random = ToInt(value);
        else if ("loop_mode" == key || "loopmode" == key)
        {
            if (value == "no_loop") pCurDef->loop_mode = NO_LOOP;
            else if (value == "one_shot") pCurDef->loop_mode = ONE_SHOT;
            else if (value == "loop_continuous") pCurDef->loop_mode = LOOP_CONTINUOUS;
            else if (value == "loop_sustain") pCurDef->loop_mode = LOOP_SUSTAIN;
        }
        else if ("loop_start" == key) pCurDef->loop_start = ToInt(value);
        else if ("loopstart" == key) pCurDef->loop_start = ToInt(value); // nonstandard
        else if ("loop_end" == key) pCurDef->loop_end = ToInt(value);
        else if ("loopend" == key) pCurDef->loop_end = ToInt(value); // nonstandard
        else if ("offset" == key) pCurDef->offset = ToInt(value);
        else if ("sync_beats" == key) pCurDef->sync_beats = ToInt(value);
        else if ("sync_offset" == key) pCurDef->sync_offset = ToInt(value);

        // amplifier
        else if ("volume"   == key) pCurDef->volume = ToFloat(value);
        else if ("amplitude" == key) pCurDef->amplitude = ToFloat(value);
        else if ("pan"      == key) pCurDef->pan = ToFloat(value);
        else if ("width"    == key) pCurDef->width = ToFloat(value);
        else if ("position" == key) pCurDef->position = ToFloat(value);
        else if ("amp_keytrack"  == key) pCurDef->amp_keytrack = ToFloat(value);
        else if ("amp_keycenter" == key) pCurDef->amp_keycenter = parseKey(value);
        else if ("amp_veltrack"  == key) pCurDef->amp_veltrack = ToFloat(value);
        else if ("amp_random"  == key) pCurDef->amp_random = ToFloat(value);
        else if ("rt_decay" == key || "rtdecay" == key) pCurDef->rt_decay = ToFloat(value);
        else if ("xfin_lokey"  == key) pCurDef->xfin_lokey = parseKey(value);
        else if ("xfin_hikey"  == key) pCurDef->xfin_hikey = parseKey(value);
        else if ("xfout_lokey" == key) pCurDef->xfout_lokey = parseKey(value);
        else if ("xfout_hikey" == key) pCurDef->xfout_hikey = parseKey(value);
        else if ("xf_keycurve" == key)
        {
            if (value == "gain") pCurDef->xf_keycurve = GAIN;
            else if (value == "power") pCurDef->xf_keycurve = POWER;
        }
        else if ("xfin_lovel"  == key) pCurDef->xfin_lovel = ToInt(value);
        else if ("xfin_hivel"  == key) pCurDef->xfin_hivel = ToInt(value);
        else if ("xfout_lovel" == key) pCurDef->xfout_lovel = ToInt(value);
        else if ("xfout_hivel" == key) pCurDef->xfout_hivel = ToInt(value);
        else if ("xf_velcurve" == key)
        {
            if (value == "gain") pCurDef->xf_velcurve = GAIN;
            else if (value == "power") pCurDef->xf_velcurve = POWER;
        }
        else if ("xf_cccurve" == key)
        {
            if (value == "gain") pCurDef->xf_cccurve = GAIN;
            else if (value == "power") pCurDef->xf_cccurve = POWER;
        }

        // pitch
        else if ("transpose" == key) pCurDef->transpose = ToInt(value);
        else if ("tune" == key) pCurDef->tune = ToInt(value);
        else if ("pitch_keycenter" == key) pCurDef->pitch_keycenter = parseKey(value);
        else if ("pitch_keytrack" == key) pCurDef->pitch_keytrack = ToInt(value);
        else if ("pitch_veltrack" == key) pCurDef->pitch_veltrack = ToInt(value);
        else if ("pitch_random" == key) pCurDef->pitch_random = ToInt(value);
        else if ("bend_up" == key || "bendup" == key) pCurDef->bend_up = ToInt(value);
        else if ("bend_down" == key || "benddown" == key) pCurDef->bend_down = ToInt(value);
        else if ("bend_step" == key || "bendstep" == key) pCurDef->bend_step = ToInt(value);

        // filter
        else if ("fil_type" == key || "filtype" == key)
        {
            if (value == "lpf_1p") pCurDef->fil_type = LPF_1P;
            else if (value == "hpf_1p") pCurDef->fil_type = HPF_1P;
            else if (value == "bpf_1p") pCurDef->fil_type = BPF_1P;
            else if (value == "brf_1p") pCurDef->fil_type = BRF_1P;
            else if (value == "apf_1p") pCurDef->fil_type = APF_1P;
            else if (value == "lpf_2p") pCurDef->fil_type = LPF_2P;
            else if (value == "hpf_2p") pCurDef->fil_type = HPF_2P;
            else if (value == "bpf_2p") pCurDef->fil_type = BPF_2P;
            else if (value == "brf_2p") pCurDef->fil_type = BRF_2P;
            else if (value == "pkf_2p") pCurDef->fil_type = PKF_2P;
            else if (value == "lpf_4p") pCurDef->fil_type = LPF_4P;
            else if (value == "hpf_4p") pCurDef->fil_type = HPF_4P;
            else if (value == "lpf_6p") pCurDef->fil_type = LPF_6P;
            else if (value == "hpf_6p") pCurDef->fil_type = HPF_6P;
        }
        else if ("fil2_type" == key)
        {
            if (value == "lpf_1p") pCurDef->fil2_type = LPF_1P;
            else if (value == "hpf_1p") pCurDef->fil2_type = HPF_1P;
            else if (value == "bpf_1p") pCurDef->fil2_type = BPF_1P;
            else if (value == "brf_1p") pCurDef->fil2_type = BRF_1P;
            else if (value == "apf_1p") pCurDef->fil2_type = APF_1P;
            else if (value == "lpf_2p") pCurDef->fil2_type = LPF_2P;
            else if (value == "hpf_2p") pCurDef->fil2_type = HPF_2P;
            else if (value == "bpf_2p") pCurDef->fil2_type = BPF_2P;
            else if (value == "brf_2p") pCurDef->fil2_type = BRF_2P;
            else if (value == "pkf_2p") pCurDef->fil2_type = PKF_2P;
            else if (value == "lpf_4p") pCurDef->fil2_type = LPF_4P;
            else if (value == "hpf_4p") pCurDef->fil2_type = HPF_4P;
            else if (value == "lpf_6p") pCurDef->fil2_type = LPF_6P;
            else if (value == "hpf_6p") pCurDef->fil2_type = HPF_6P;
        }
        else if ("cutoff"  == key) pCurDef->cutoff = ToFloat(value);
        else if ("cutoff2" == key) pCurDef->cutoff2 = ToFloat(value);
        else if ("cutoff_chanaft"  == key) {
            pCurDef->cutoff_chanaft = check(key, -9600, 9600, ToInt(value));
            pCurDef->cutoff_oncc.add( CC(128, check(key, -9600, 9600, ToInt(value))) );
        } else if ("cutoff2_chanaft" == key) pCurDef->cutoff2_chanaft = ToInt(value);
        else if ("cutoff_polyaft"  == key) pCurDef->cutoff_polyaft = ToInt(value);
        else if ("cutoff2_polyaft" == key) pCurDef->cutoff2_polyaft = ToInt(value);
        else if ("resonance"  == key) pCurDef->resonance = ToFloat(value);
        else if ("resonance2" == key) pCurDef->resonance2 = ToFloat(value);
        else if ("fil_keytrack"   == key) pCurDef->fil_keytrack = ToInt(value);
        else if ("fil2_keytrack"  == key) pCurDef->fil2_keytrack = ToInt(value);
        else if ("fil_keycenter"  == key) pCurDef->fil_keycenter = parseKey(value);
        else if ("fil2_keycenter" == key) pCurDef->fil2_keycenter = parseKey(value);
        else if ("fil_veltrack"   == key) pCurDef->fil_veltrack = ToInt(value);
        else if ("fil2_veltrack"  == key) pCurDef->fil2_veltrack = ToInt(value);
        else if ("fil_random"     == key) pCurDef->fil_random = ToInt(value);
        else if ("fil2_random"    == key) pCurDef->fil2_random = ToInt(value);

        // per voice equalizer
        else if ("eq1_freq" == key) pCurDef->eq1_freq = ToFloat(value);
        else if ("eq2_freq" == key) pCurDef->eq2_freq = ToFloat(value);
        else if ("eq3_freq" == key) pCurDef->eq3_freq = ToFloat(value);
        else if ("eq1_vel2freq" == key) pCurDef->eq1_vel2freq = ToFloat(value);
        else if ("eq2_vel2freq" == key) pCurDef->eq2_vel2freq = ToFloat(value);
        else if ("eq3_vel2freq" == key) pCurDef->eq3_vel2freq = ToFloat(value);
        else if ("eq1_bw" == key) pCurDef->eq1_bw = ToFloat(value);
        else if ("eq2_bw" == key) pCurDef->eq2_bw = ToFloat(value);
        else if ("eq3_bw" == key) pCurDef->eq3_bw = ToFloat(value);
        else if ("eq1_gain" == key) pCurDef->eq1_gain = ToFloat(value);
        else if ("eq2_gain" == key) pCurDef->eq2_gain = ToFloat(value);
        else if ("eq3_gain" == key) pCurDef->eq3_gain = ToFloat(value);
        else if ("eq1_vel2gain" == key) pCurDef->eq1_vel2gain = ToFloat(value);
        else if ("eq2_vel2gain" == key) pCurDef->eq2_vel2gain = ToFloat(value);
        else if ("eq3_vel2gain" == key) pCurDef->eq3_vel2gain = ToFloat(value);

        else if (sscanf(key.c_str(), "amp_velcurve_%d", &x)) {
            pCurDef->amp_velcurve.set(x, ToFloat(value));
        }
        
        // v2 envelope generators
        else if (sscanf(key.c_str(), "eg%d%n", &x, &y)) {
            const char* s = key.c_str() + y;
            if (sscanf(s, "_time%d%n", &y, &z)) {
                const char* s2 = s + z;
                if (strcmp(s2, "") == 0) egnode(x, y).time = check(key, 0.0f, 100.0f, ToFloat(value));
                else if (sscanf(s2, "_oncc%d", &z)) egnode(x, y).time_oncc.add( CC(z, check(key, 0.0f, 100.0f, ToFloat(value))) );
            } else if (sscanf(s, "_level%d%n", &y, &z)) {
                const char* s2 = s + z;
                if (strcmp(s2, "") == 0) egnode(x, y).level = check(key, 0.0f, 1.0f, ToFloat(value));
                else if (sscanf(s2, "_oncc%d", &z)) egnode(x, y).level_oncc.add( CC(z, check(key, 0.0f, 1.0f, ToFloat(value))) );
            }
            else if (sscanf(s, "_shape%d", &y)) egnode(x, y).shape = ToFloat(value);
            else if (sscanf(s, "_curve%d", &y)) egnode(x, y).curve = ToFloat(value);
            else if (strcmp(s, "_sustain") == 0) eg(x).sustain = ToInt(value);
            else if (strcmp(s, "_loop") == 0) eg(x).loop = ToInt(value);
            else if (strcmp(s, "_loop_count") == 0) eg(x).loop_count = ToInt(value);
            else if (strcmp(s, "_amplitude") == 0) eg(x).amplitude = ToFloat(value);
            else if (sscanf(s, "_amplitude_oncc%d", &y)) eg(x).amplitude_oncc.add( CC(y, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if (strcmp(s, "_volume") == 0) eg(x).volume = check(key, -144.0f, 6.0f, ToFloat(value));
            else if (sscanf(s, "_volume_oncc%d", &y)) eg(x).volume_oncc.add( CC(y, check(key, -144.0f, 6.0f, ToFloat(value))) );
            else if (strcmp(s, "_cutoff") == 0) eg(x).cutoff = ToFloat(value);
            else if (sscanf(s, "_cutoff_oncc%d", &y)) eg(x).cutoff_oncc.add( CC(y, check(key, -9600, 9600, ToInt(value))) );
            else if (strcmp(s, "_pitch") == 0) eg(x).pitch = check(key, -9600, 9600, ToInt(value));
            else if (sscanf(s, "_pitch_oncc%d", &y)) eg(x).pitch_oncc.add( CC(y, check(key, -9600, 9600, ToInt(value))) );
            else if (strcmp(s, "_resonance") == 0) eg(x).resonance = check(key, 0.0f, 40.0f, ToFloat(value));
            else if (sscanf(s, "_resonance_oncc%d", &y)) eg(x).resonance_oncc.add( CC(y, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if (strcmp(s, "_pan") == 0) eg(x).pan = check(key, -100.0f, 100.0f, ToFloat(value));
            else if (strcmp(s, "_pan_curve") == 0) eg(x).pan_curve = check(key, 0, 30000, ToInt(value));
            else if (sscanf(s, "_pan_oncc%d", &y)) eg(x).pan_oncc.add( CC(y, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if (sscanf(s, "_pan_curvecc%d", &y)) eg(x).pan_curvecc.add( CC(y, 0.0f, check(key, 0, 30000, ToInt(value))) );
            else if (strcmp(s, "_eq1freq") == 0) eg(x).eq1freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq2freq") == 0) eg(x).eq2freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq3freq") == 0) eg(x).eq3freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq1bw") == 0) eg(x).eq1bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq2bw") == 0) eg(x).eq2bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq3bw") == 0) eg(x).eq3bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq1gain") == 0) eg(x).eq1gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (strcmp(s, "_eq2gain") == 0) eg(x).eq2gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (strcmp(s, "_eq3gain") == 0) eg(x).eq3gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (sscanf(s, "_eq1freq_oncc%d", &y)) eg(x).eq1freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2freq_oncc%d", &y)) eg(x).eq2freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3freq_oncc%d", &y)) eg(x).eq3freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1bw_oncc%d", &y)) eg(x).eq1bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2bw_oncc%d", &y)) eg(x).eq2bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3bw_oncc%d", &y)) eg(x).eq3bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1gain_oncc%d", &y)) eg(x).eq1gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2gain_oncc%d", &y)) eg(x).eq2gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3gain_oncc%d", &y)) eg(x).eq3gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else std::cerr << "The opcode '" << key << "' is unsupported by libsfz!" << std::endl;
        }

        // v1 envelope generators
        else if ("ampeg_delay"   == key) pCurDef->ampeg_delay = ToFloat(value);
        else if ("ampeg_start"   == key) pCurDef->ampeg_start = ToFloat(value);
        else if ("ampeg_attack"   == key) pCurDef->ampeg_attack = ToFloat(value);
        else if ("ampeg_hold"   == key) pCurDef->ampeg_hold = ToFloat(value);
        else if ("ampeg_decay"   == key) pCurDef->ampeg_decay = ToFloat(value);
        else if ("ampeg_sustain"   == key) pCurDef->ampeg_sustain = ToFloat(value);
        else if ("ampeg_release" == key) pCurDef->ampeg_release = ToFloat(value);
        else if ("ampeg_vel2delay" == key) pCurDef->ampeg_vel2delay = ToFloat(value);
        else if ("ampeg_vel2attack" == key) pCurDef->ampeg_vel2attack = ToFloat(value);
        else if ("ampeg_vel2hold" == key) pCurDef->ampeg_vel2hold = ToFloat(value);
        else if ("ampeg_vel2decay" == key) pCurDef->ampeg_vel2decay = ToFloat(value);
        else if ("ampeg_vel2sustain" == key) pCurDef->ampeg_vel2sustain = ToFloat(value);
        else if ("ampeg_vel2release" == key) pCurDef->ampeg_vel2release = ToFloat(value);
        else if ("fileg_delay"   == key) pCurDef->fileg_delay = ToFloat(value);
        else if ("fileg_start"   == key) pCurDef->fileg_start = ToFloat(value);
        else if ("fileg_attack"   == key) pCurDef->fileg_attack = ToFloat(value);
        else if ("fileg_hold"   == key) pCurDef->fileg_hold = ToFloat(value);
        else if ("fileg_decay"   == key) pCurDef->fileg_decay = ToFloat(value);
        else if ("fileg_sustain"   == key) pCurDef->fileg_sustain = ToFloat(value);
        else if ("fileg_release"   == key) pCurDef->fileg_release = ToFloat(value);
        else if ("fileg_depth"   == key) pCurDef->fileg_depth = check(key, -12000, 12000, ToInt(value));
        else if ("fileg_vel2delay"   == key) pCurDef->fileg_vel2delay = check(key, -100.0f, 100.0f, ToFloat(value));
        else if ("fileg_vel2attack"  == key) pCurDef->fileg_vel2attack = ToFloat(value);
        else if ("fileg_vel2hold"    == key) pCurDef->fileg_vel2hold = ToFloat(value);
        else if ("fileg_vel2decay"   == key) pCurDef->fileg_vel2decay = ToFloat(value);
        else if ("fileg_vel2sustain" == key) pCurDef->fileg_vel2sustain = ToFloat(value);
        else if ("fileg_vel2release" == key) pCurDef->fileg_vel2release = ToFloat(value);
        else if ("pitcheg_delay"   == key) pCurDef->pitcheg_delay = ToFloat(value);
        else if ("pitcheg_start"   == key) pCurDef->pitcheg_start = ToFloat(value);
        else if ("pitcheg_attack"  == key) pCurDef->pitcheg_attack = ToFloat(value);
        else if ("pitcheg_hold"    == key) pCurDef->pitcheg_hold = ToFloat(value);
        else if ("pitcheg_decay"   == key) pCurDef->pitcheg_decay = ToFloat(value);
        else if ("pitcheg_sustain" == key) pCurDef->pitcheg_sustain = ToFloat(value);
        else if ("pitcheg_release" == key) pCurDef->pitcheg_release = ToFloat(value);
        else if ("pitcheg_depth"   == key) pCurDef->pitcheg_depth = check(key, -12000, 12000, ToInt(value));
        else if ("pitcheg_vel2delay"   == key) pCurDef->pitcheg_vel2delay = check(key, -100.0f, 100.0f, ToFloat(value));
        else if ("pitcheg_vel2attack"  == key) pCurDef->pitcheg_vel2attack = ToFloat(value);
        else if ("pitcheg_vel2hold"    == key) pCurDef->pitcheg_vel2hold = ToFloat(value);
        else if ("pitcheg_vel2decay"   == key) pCurDef->pitcheg_vel2decay = ToFloat(value);
        else if ("pitcheg_vel2sustain" == key) pCurDef->pitcheg_vel2sustain = ToFloat(value);
        else if ("pitcheg_vel2release" == key) pCurDef->pitcheg_vel2release = ToFloat(value);
        

        // v1 LFO
        else if ("amplfo_delay" == key) pCurDef->amplfo_delay = ToFloat(value);
        else if ("amplfo_fade" == key) pCurDef->amplfo_fade = ToFloat(value);
        else if ("amplfo_freq" == key) pCurDef->amplfo_freq = ToFloat(value);
        else if ("amplfo_freqchanaft" == key) pCurDef->amplfo_freqcc.add( CC(128, check(key, -200.0f, 200.0f, ToFloat(value))) );
        else if ("amplfo_depth" == key) pCurDef->amplfo_depth = ToFloat(value);
        else if ("amplfo_depthchanaft" == key) pCurDef->amplfo_depthcc.add( CC(128, check(key, -10.0f, 10.0f, ToFloat(value))) );
        else if ("fillfo_delay" == key) pCurDef->fillfo_delay = ToFloat(value);
        else if ("fillfo_fade" == key) pCurDef->fillfo_fade = ToFloat(value);
        else if ("fillfo_freq" == key) pCurDef->fillfo_freq = ToFloat(value);
        else if ("fillfo_freqchanaft" == key) pCurDef->fillfo_freqcc.add( CC(128, check(key, -200.0f, 200.0f, ToFloat(value))) );
        else if ("fillfo_depth" == key) pCurDef->fillfo_depth = ToFloat(value);
        else if ("fillfo_depthchanaft" == key) pCurDef->fillfo_depthcc.add( CC(128, check(key, -1200, 1200, ToInt(value))) );
        else if ("pitchlfo_delay" == key) pCurDef->pitchlfo_delay = ToFloat(value);
        else if ("pitchlfo_fade" == key) pCurDef->pitchlfo_fade = ToFloat(value);
        else if ("pitchlfo_freq" == key) pCurDef->pitchlfo_freq = ToFloat(value);
        else if ("pitchlfo_freqchanaft" == key) pCurDef->pitchlfo_freqcc.add( CC(128, check(key, -200.0f, 200.0f, ToFloat(value))) );
        else if ("pitchlfo_depth" == key) pCurDef->pitchlfo_depth = ToInt(value);
        else if ("pitchlfo_depthchanaft" == key) pCurDef->pitchlfo_depthcc.add( CC(128, check(key, -1200, 1200, ToInt(value))) );
        
        
        // v2 LFO
        else if (sscanf(key.c_str(), "lfo%d%n", &x, &y)) {
            const char* s = key.c_str() + y;
            if (strcmp(s, "_freq") == 0) lfo(x).freq = check(key, 0.0f, 20.0f, ToFloat(value));
            else if (sscanf(s, "_freq_oncc%d", &y)) lfo(x).freq_oncc.add( CC(y, check(key, 0.0f, 20.0f, ToFloat(value))) );
            else if (sscanf(s, "_freq_smoothcc%d", &y)) lfo(x).freq_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_freq_stepcc%d", &y)) lfo(x).freq_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 20.0f, ToFloat(value))) );
            else if (strcmp(s, "_wave") == 0) lfo(x).wave = ToInt(value);
            else if (strcmp(s, "_delay") == 0) lfo(x).delay = check(key, 0.0f, 100.0f, ToFloat(value));
            else if (sscanf(s, "_delay_oncc%d", &y)) lfo(x).delay_oncc.add( CC(y, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if (strcmp(s, "_fade") == 0) lfo(x).fade = check(key, 0.0f, 100.0f, ToFloat(value));
            else if (sscanf(s, "_fade_oncc%d", &y)) lfo(x).fade_oncc.add( CC(y, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if (strcmp(s, "_phase") == 0) lfo(x).phase = check(key, 0.0f, 360.0f, ToFloat(value));
            else if (sscanf(s, "_phase_oncc%d", &y)) lfo(x).phase_oncc.add( CC(y, check(key, 0.0f, 360.0f, ToFloat(value))) );
            else if (strcmp(s, "_volume") == 0) lfo(x).volume = check(key, -144.0f, 6.0f, ToFloat(value));
            else if (sscanf(s, "_volume_oncc%d", &y)) lfo(x).volume_oncc.add( CC(y, check(key, -144.0f, 6.0f, ToFloat(value))) );
            else if (sscanf(s, "_volume_smoothcc%d", &y)) lfo(x).volume_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_volume_stepcc%d", &y)) lfo(x).volume_stepcc.add( CC(y, 0, -1, 0, check(key, -20.0f, 20.0f, ToFloat(value))) );
            else if (strcmp(s, "_pitch") == 0) lfo(x).pitch = check(key, -9600, 9600, ToInt(value));
            else if (sscanf(s, "_pitch_oncc%d", &y)) lfo(x).pitch_oncc.add( CC(y, check(key, -9600, 9600, ToInt(value))) );
            else if (sscanf(s, "_pitch_smoothcc%d", &y)) lfo(x).pitch_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_pitch_stepcc%d", &y)) lfo(x).pitch_stepcc.add( CC(y, 0, -1, 0, check(key, -9600, 9600, ToInt(value))) );
            else if (strcmp(s, "_cutoff") == 0) lfo(x).cutoff = check(key, -9600, 9600, ToInt(value));
            else if (sscanf(s, "_cutoff_oncc%d", &y)) lfo(x).cutoff_oncc.add( CC(y, check(key, -9600, 9600, ToInt(value))) );
            else if (sscanf(s, "_cutoff_smoothcc%d", &y)) lfo(x).cutoff_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_cutoff_stepcc%d", &y)) lfo(x).cutoff_stepcc.add( CC(y, 0, -1, 0, check(key, -9600, 9600, ToInt(value))) );
            else if (strcmp(s, "_resonance") == 0) lfo(x).resonance = check(key, 0.0f, 40.0f, ToFloat(value));
            else if (sscanf(s, "_resonance_oncc%d", &y)) lfo(x).resonance_oncc.add( CC(y, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if (sscanf(s, "_resonance_smoothcc%d", &y)) lfo(x).resonance_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_resonance_stepcc%d", &y)) lfo(x).resonance_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if (strcmp(s, "_pan") == 0) lfo(x).pan = check(key, -100.0f, 100.0f, ToFloat(value));
            else if (sscanf(s, "_pan_oncc%d", &y)) lfo(x).pan_oncc.add( CC(y, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if (sscanf(s, "_pan_smoothcc%d", &y)) lfo(x).pan_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_pan_stepcc%d", &y)) lfo(x).pan_stepcc.add( CC(y, 0, -1, 0, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if (strcmp(s, "_eq1freq") == 0) lfo(x).eq1freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq2freq") == 0) lfo(x).eq2freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq3freq") == 0) lfo(x).eq3freq = check(key, 0.0f, 30000.0f, ToFloat(value));
            else if (strcmp(s, "_eq1bw") == 0) lfo(x).eq1bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq2bw") == 0) lfo(x).eq2bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq3bw") == 0) lfo(x).eq3bw = check(key, 0.001f, 4.0f, ToFloat(value));
            else if (strcmp(s, "_eq1gain") == 0) lfo(x).eq1gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (strcmp(s, "_eq2gain") == 0) lfo(x).eq2gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (strcmp(s, "_eq3gain") == 0) lfo(x).eq3gain = check(key, -96.0f, 24.0f, ToFloat(value));
            else if (sscanf(s, "_eq1freq_oncc%d", &y)) lfo(x).eq1freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1freq_smoothcc%d", &y)) lfo(x).eq1freq_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq1freq_stepcc%d", &y)) lfo(x).eq1freq_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2freq_oncc%d", &y)) lfo(x).eq2freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2freq_smoothcc%d", &y)) lfo(x).eq2freq_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq2freq_stepcc%d", &y)) lfo(x).eq2freq_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3freq_oncc%d", &y)) lfo(x).eq3freq_oncc.add( CC(y, check(key, 0.0f, 30000.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3freq_smoothcc%d", &y)) lfo(x).eq3freq_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq3freq_stepcc%d", &y)) lfo(x).eq3freq_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1bw_oncc%d", &y)) lfo(x).eq1bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1bw_smoothcc%d", &y)) lfo(x).eq1bw_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq1bw_stepcc%d", &y)) lfo(x).eq1bw_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2bw_oncc%d", &y)) lfo(x).eq2bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2bw_smoothcc%d", &y)) lfo(x).eq2bw_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq2bw_stepcc%d", &y)) lfo(x).eq2bw_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3bw_oncc%d", &y)) lfo(x).eq3bw_oncc.add( CC(y, check(key, 0.001f, 4.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3bw_smoothcc%d", &y)) lfo(x).eq3bw_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq3bw_stepcc%d", &y)) lfo(x).eq3bw_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1gain_oncc%d", &y)) lfo(x).eq1gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq1gain_smoothcc%d", &y)) lfo(x).eq1gain_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq1gain_stepcc%d", &y)) lfo(x).eq1gain_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2gain_oncc%d", &y)) lfo(x).eq2gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq2gain_smoothcc%d", &y)) lfo(x).eq2gain_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq2gain_stepcc%d", &y)) lfo(x).eq2gain_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3gain_oncc%d", &y)) lfo(x).eq3gain_oncc.add( CC(y, check(key, -96.0f, 24.0f, ToFloat(value))) );
            else if (sscanf(s, "_eq3gain_smoothcc%d", &y)) lfo(x).eq3gain_smoothcc.add( CC(y, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if (sscanf(s, "_eq3gain_stepcc%d", &y)) lfo(x).eq3gain_stepcc.add( CC(y, 0, -1, 0, check(key, 0.0f, 4294967296.0f, ToFloat(value))) );
            else std::cerr << "The opcode '" << key << "' is unsupported by libsfz!" << std::endl;
        }
        
        // CCs
        else if (key.find("cc") != std::string::npos)
        {
            std::string::size_type delimiter_index = key.find("cc");
            std::string key_cc = key.substr(0, delimiter_index);
            if (key_cc.size() > 3 && !strcmp(key_cc.c_str() + (key_cc.size() - 3), "_on")) {
                key_cc = key_cc.substr(0, key_cc.size() - 3);
            }
            int num_cc = ToInt(key.substr(delimiter_index + 2));

            // input controls
            if ("lo" == key_cc) pCurDef->locc.set(num_cc, ToInt(value));
            else if ("hi" == key_cc) pCurDef->hicc.set(num_cc, ToInt(value));
            else if ("start_lo" == key_cc) pCurDef->start_locc.set(num_cc, ToInt(value));
            else if ("start_hi" == key_cc) pCurDef->start_hicc.set(num_cc, ToInt(value));
            else if ("stop_lo" == key_cc) pCurDef->stop_locc.set(num_cc, ToInt(value));
            else if ("stop_hi" == key_cc) pCurDef->stop_hicc.set(num_cc, ToInt(value));
            else if ("on_lo" == key_cc) pCurDef->on_locc.set(num_cc, ToInt(value));
            else if ("on_hi" == key_cc) pCurDef->on_hicc.set(num_cc, ToInt(value));

            // sample player
            else if ("delay" == key_cc) pCurDef->delay_oncc.set(num_cc, ToFloat(value));
            else if ("delay_samples" == key_cc) pCurDef->delay_samples_oncc.set(num_cc, ToInt(value));
            else if ("offset" == key_cc) pCurDef->offset_oncc.set(num_cc, ToInt(value));

            // amplifier
            else if ("gain"  == key_cc || "gain_" == key_cc) pCurDef->gain_oncc.set(num_cc, ToFloat(value));
            else if ("xfin_lo"  == key_cc) pCurDef->xfin_locc.set(num_cc, ToInt(value));
            else if ("xfin_hi"  == key_cc) pCurDef->xfin_hicc.set(num_cc, ToInt(value));
            else if ("xfout_lo" == key_cc) pCurDef->xfout_locc.set(num_cc, ToInt(value));
            else if ("xfout_hi" == key_cc) pCurDef->xfout_hicc.set(num_cc, ToInt(value));
            
            // pitch
            else if ("pitch" == key_cc) pCurDef->pitch_oncc.add( CC(num_cc, check(key, -9600, 9600, ToInt(value))) );
            else if ("pitch_smooth" == key_cc) pCurDef->pitch_smoothcc.add( CC(num_cc, 0, -1, check(key, 0.0f, 100000.0f /* max? */, ToFloat(value))) );
            else if ("pitch_curve" == key_cc) pCurDef->pitch_curvecc.add( CC(num_cc, 0, check(key, 0, 30000, ToInt(value))) );
            else if ("pitch_step" == key_cc) pCurDef->pitch_stepcc.add( CC(num_cc, 0, -1, 0, check(key, 0, 1200, ToInt(value))) );

            // filter
            else if ("cutoff"  == key_cc || "cutoff_" == key_cc) {
                pCurDef->cutoff_oncc.add( CC(num_cc, check(key, -9600, 9600, ToInt(value))) );
            } else if ("cutoff2" == key_cc) pCurDef->cutoff2_oncc.add( CC(num_cc, check(key, -9600, 9600, ToInt(value))) );
            else if ("cutoff_smooth"  == key_cc) pCurDef->cutoff_smoothcc.add( CC(num_cc, 0, -1, check(key, 0.0f, 100000.0f /* max? */, ToFloat(value))) );
            else if ("cutoff2_smooth" == key_cc) pCurDef->cutoff2_smoothcc.add( CC(num_cc, 0, -1, check(key, 0.0f, 100000.0f /* max? */, ToFloat(value))) );
            else if ("cutoff_step"  == key_cc) pCurDef->cutoff_stepcc.add( CC(num_cc, 0, -1, 0, check(key, -1200, 1200, ToInt(value))) );
            else if ("cutoff2_step" == key_cc) pCurDef->cutoff2_stepcc.add( CC(num_cc, 0, -1, 0, check(key, -1200, 1200, ToInt(value))) );
            else if ("cutoff_curve" == key_cc) pCurDef->cutoff_curvecc.add( CC(num_cc, 0, check(key, 0, 30000, ToInt(value))) );
            else if ("cutoff2_curve" == key_cc) pCurDef->cutoff2_curvecc.add( CC(num_cc, 0, check(key, 0, 30000, ToInt(value))) );
            else if ("resonance" == key_cc) pCurDef->resonance_oncc.add( CC(num_cc, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if ("resonance2" == key_cc) pCurDef->resonance2_oncc.add( CC(num_cc, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if ("resonance_smooth" == key_cc) pCurDef->resonance_smoothcc.add( CC(num_cc, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if ("resonance2_smooth" == key_cc) pCurDef->resonance2_smoothcc.add( CC(num_cc, 0, -1, check(key, 0, 100000 /* max? */, ToInt(value))) );
            else if ("resonance_step" == key_cc) pCurDef->resonance_stepcc.add( CC(num_cc, 0, -1, 0, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if ("resonance2_step" == key_cc) pCurDef->resonance2_stepcc.add( CC(num_cc, 0, -1, 0, check(key, 0.0f, 40.0f, ToFloat(value))) );
            else if ("resonance_curve" == key_cc) pCurDef->resonance_curvecc.add( CC(num_cc, 0.0f, check(key, 0, 30000, ToInt(value))) );
            else if ("resonance2_curve" == key_cc) pCurDef->resonance2_curvecc.add( CC(num_cc, 0.0f, check(key, 0, 30000, ToInt(value))) );

            // per voice equalizer
            else if ("eq1_freq" == key_cc) pCurDef->eq1_freq_oncc.set(num_cc, ToInt(value));
            else if ("eq2_freq" == key_cc) pCurDef->eq2_freq_oncc.set(num_cc, ToInt(value));
            else if ("eq3_freq" == key_cc) pCurDef->eq3_freq_oncc.set(num_cc, ToInt(value));
            else if ("eq1_bw" == key_cc) pCurDef->eq1_bw_oncc.set(num_cc, ToInt(value));
            else if ("eq2_bw" == key_cc) pCurDef->eq2_bw_oncc.set(num_cc, ToInt(value));
            else if ("eq3_bw" == key_cc) pCurDef->eq3_bw_oncc.set(num_cc, ToInt(value));
            else if ("eq1_gain" == key_cc) pCurDef->eq1_gain_oncc.set(num_cc, ToInt(value));
            else if ("eq2_gain" == key_cc) pCurDef->eq2_gain_oncc.set(num_cc, ToInt(value));
            else if ("eq3_gain" == key_cc) pCurDef->eq3_gain_oncc.set(num_cc, ToInt(value));
            
            else if ("ampeg_delay"   == key_cc) pCurDef->ampeg_delaycc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_start"   == key_cc) pCurDef->ampeg_startcc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_attack"  == key_cc) pCurDef->ampeg_attackcc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_hold"    == key_cc) pCurDef->ampeg_holdcc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_decay"   == key_cc) pCurDef->ampeg_decaycc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_sustain" == key_cc) pCurDef->ampeg_sustaincc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("ampeg_release" == key_cc) pCurDef->ampeg_releasecc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            
            else if ("fileg_delay"   == key_cc) pCurDef->fileg_delay_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_start"   == key_cc) pCurDef->fileg_start_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_attack"  == key_cc) pCurDef->fileg_attack_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_hold"    == key_cc) pCurDef->fileg_hold_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_decay"   == key_cc) pCurDef->fileg_decay_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_sustain" == key_cc) pCurDef->fileg_sustain_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_release" == key_cc) pCurDef->fileg_release_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("fileg_depth"   == key_cc) pCurDef->fileg_depth_oncc.add( CC(num_cc, check(key, -12000, 12000, ToInt(value))) );
            
            else if ("pitcheg_delay"   == key_cc) pCurDef->pitcheg_delay_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_start"   == key_cc) pCurDef->pitcheg_start_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_attack"  == key_cc) pCurDef->pitcheg_attack_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_hold"    == key_cc) pCurDef->pitcheg_hold_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_decay"   == key_cc) pCurDef->pitcheg_decay_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_sustain" == key_cc) pCurDef->pitcheg_sustain_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_release" == key_cc) pCurDef->pitcheg_release_oncc.add( CC(num_cc, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else if ("pitcheg_depth"   == key_cc) pCurDef->pitcheg_depth_oncc.add( CC(num_cc, check(key, -12000, 12000, ToInt(value))) );
            
            else if ("pitchlfo_delay" == key_cc) pCurDef->pitchlfo_delay_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("pitchlfo_fade" == key_cc) pCurDef->pitchlfo_fade_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("pitchlfo_depth" == key_cc) pCurDef->pitchlfo_depthcc.add( CC(num_cc, check(key, -1200, 1200, ToInt(value))) );
            else if ("pitchlfo_freq" == key_cc) pCurDef->pitchlfo_freqcc.add( CC(num_cc, check(key, -200.0f, 200.0f, ToFloat(value))) );
            else if ("fillfo_delay" == key_cc) pCurDef->fillfo_delay_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("fillfo_fade" == key_cc) pCurDef->fillfo_fade_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("fillfo_depth" == key_cc) pCurDef->fillfo_depthcc.add( CC(num_cc, check(key, -1200, 1200, ToInt(value))) );
            else if ("fillfo_freq" == key_cc) pCurDef->fillfo_freqcc.add( CC(num_cc, check(key, -200.0f, 200.0f, ToFloat(value))) );
            else if ("amplfo_delay" == key_cc) pCurDef->amplfo_delay_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("amplfo_fade" == key_cc) pCurDef->amplfo_fade_oncc.add( CC(num_cc, check(key, 0.0f, 100.0f, ToFloat(value))) );
            else if ("amplfo_depth" == key_cc) pCurDef->amplfo_depthcc.add( CC(num_cc, check(key, -10.0f, 10.0f, ToFloat(value))) );
            else if ("amplfo_freq" == key_cc) pCurDef->amplfo_freqcc.add( CC(num_cc, check(key, -200.0f, 200.0f, ToFloat(value))) );
            else if ("volume" == key_cc) pCurDef->volume_oncc.add( CC(num_cc, check(key, -144.0f, 100.0f, ToFloat(value))) );
            else if ("volume_curve" == key_cc) pCurDef->volume_curvecc.add( CC(num_cc, 0, check(key, 0, 30000, ToInt(value))) );
            else if ("volume_smooth" == key_cc) pCurDef->volume_smoothcc.add( CC(num_cc, 0, -1, check(key, 0.0f, 100000.0f /* max? */, ToFloat(value))) );
            else if ("volume_step" == key_cc) pCurDef->volume_stepcc.add( CC(num_cc, 0, -1, 0, check(key, -20.0f, 20.0f, ToFloat(value))) );
            else if ("pan" == key_cc) pCurDef->pan_oncc.add( CC(num_cc, check(key, -200.0f, 200.0f, ToFloat(value))) );
            else if ("pan_curve" == key_cc) pCurDef->pan_curvecc.add( CC(num_cc, 0, check(key, 0, 30000, ToInt(value))) );
            else if ("pan_smooth" == key_cc) pCurDef->pan_smoothcc.add( CC(num_cc, 0, -1, check(key, 0.0f, 100000.0f /* max? */, ToFloat(value))) );
            else if ("pan_step" == key_cc) pCurDef->pan_stepcc.add( CC(num_cc, 0, -1, 0, check(key, -100.0f, 100.0f, ToFloat(value))) );
            else std::cerr << "The opcode '" << key << "' is unsupported by libsfz!" << std::endl;
        }

        else {
            std::cerr << "The opcode '" << key << "' is unsupported by libsfz!" << std::endl;
        }
    }

    int File::parseKey(const std::string& s) {
        int i;
        std::istringstream iss(s);
        if (isdigit(iss.peek())) {
            iss >> i;
        } else {
            switch (tolower(iss.get())) {
            case 'c': i = 0; break;
            case 'd': i = 2; break;
            case 'e': i = 4; break;
            case 'f': i = 5; break;
            case 'g': i = 7; break;
            case 'a': i = 9; break;
            case 'b': i = 11; break;
            case '-': if (s == "-1") return -1;
            default:
                std::cerr << "Not a note: " << s << std::endl;
                return 0;
            }
            if (iss.peek() == '#') {
                i++;
                iss.get();
            } else if (tolower(iss.peek()) == 'b') {
                i--;
                iss.get();
            }
            int octave;
            if (!(iss >> octave)) {
                std::cerr << "Not a note: " << s << std::endl;
                return 0;
            }
            i += (octave + 1) * 12;
        }
        return i + note_offset + 12 * octave_offset;
    }

    EGNode::EGNode() : time(0), level(0), shape(0), curve(0) {
    }
    
    void EGNode::Copy(const EGNode& egNode) {
        time  = egNode.time;
        level = egNode.level;
        shape = egNode.shape;
        curve = egNode.curve;
        
        time_oncc = egNode.time_oncc;
        level_oncc = egNode.level_oncc;
    }

    EG::EG() :
        sustain(0), loop(0), loop_count(0), amplitude(0), pan(0), pan_curve(-1),
        cutoff(0), pitch(0), resonance(0), volume(-200) /* less than -144 dB is considered unset */
    { }
    
    void EG::Copy(const EG& eg) {
        EqImpl::Copy(static_cast<const EqImpl>(eg));
        
        sustain    = eg.sustain;
        loop       = eg.loop;
        loop_count = eg.loop_count;
        amplitude  = eg.amplitude;
        volume     = eg.volume;
        cutoff     = eg.cutoff;
        pitch      = eg.pitch;
        resonance  = eg.resonance;
        pan        = eg.pan;
        pan_curve  = eg.pan_curve;
        node       = eg.node;
        
        amplitude_oncc = eg.amplitude_oncc;
        volume_oncc    = eg.volume_oncc;
        cutoff_oncc    = eg.cutoff_oncc;
        pitch_oncc     = eg.pitch_oncc;
        resonance_oncc = eg.resonance_oncc;
        pan_oncc       = eg.pan_oncc;
        pan_curvecc    = eg.pan_curvecc;
    }
    
    LFO::LFO(): freq (-1),/* -1 is used to determine whether the LFO was initialized */
                fade(0), phase(0), wave(0), delay(0), pitch(0), cutoff(0), resonance(0), pan(0), volume(0) {
        
    }
    
    void LFO::Copy(const LFO& lfo) {
        EqSmoothStepImpl::Copy(static_cast<const EqSmoothStepImpl>(lfo));
        
        delay      = lfo.delay;
        freq       = lfo.freq;
        fade       = lfo.fade;
        phase      = lfo.phase;
        wave       = lfo.wave;
        volume     = lfo.volume;
        pitch      = lfo.pitch;
        cutoff     = lfo.cutoff;
        resonance  = lfo.resonance;
        pan        = lfo.pan;
        
        delay_oncc      = lfo.delay_oncc;
        freq_oncc       = lfo.freq_oncc;
        freq_smoothcc   = lfo.freq_smoothcc;
        freq_stepcc     = lfo.freq_stepcc;
        fade_oncc       = lfo.fade_oncc;
        phase_oncc      = lfo.phase_oncc;
        pitch_oncc      = lfo.pitch_oncc;
        pitch_smoothcc  = lfo.pitch_smoothcc;
        pitch_stepcc    = lfo.pitch_stepcc;
        volume_oncc     = lfo.volume_oncc;
        volume_smoothcc = lfo.volume_smoothcc;
        volume_stepcc   = lfo.volume_stepcc;
        pan_oncc        = lfo.pan_oncc;
        pan_smoothcc    = lfo.pan_smoothcc;
        pan_stepcc      = lfo.pan_stepcc;
        cutoff_oncc     = lfo.cutoff_oncc;
        cutoff_smoothcc = lfo.cutoff_smoothcc;
        cutoff_stepcc   = lfo.cutoff_stepcc;
        resonance_oncc     = lfo.resonance_oncc;
        resonance_smoothcc = lfo.resonance_smoothcc;
        resonance_stepcc   = lfo.resonance_stepcc;
    }
    
    EqImpl::EqImpl() {
        eq1freq = eq2freq = eq3freq = 0;
        eq1bw = eq2bw = eq3bw = 0;
        eq1gain = eq2gain = eq3gain = 0;
    }
    
    void EqImpl::Copy(const EqImpl& eq) {
        eq1freq = eq.eq1freq;
        eq2freq = eq.eq2freq;
        eq3freq = eq.eq3freq;
        eq1bw   = eq.eq1bw;
        eq2bw   = eq.eq2bw;
        eq3bw   = eq.eq3bw;
        eq1gain = eq.eq1gain;
        eq2gain = eq.eq2gain;
        eq3gain = eq.eq3gain;
        
        eq1freq_oncc = eq.eq1freq_oncc;
        eq2freq_oncc = eq.eq2freq_oncc;
        eq3freq_oncc = eq.eq3freq_oncc;
        eq1bw_oncc   = eq.eq1bw_oncc;
        eq2bw_oncc   = eq.eq2bw_oncc;
        eq3bw_oncc   = eq.eq3bw_oncc;
        eq1gain_oncc = eq.eq1gain_oncc;
        eq2gain_oncc = eq.eq2gain_oncc;
        eq3gain_oncc = eq.eq3gain_oncc;
    }
    
    bool EqImpl::HasEq() {
        return eq1freq || eq2freq || eq3freq || eq1bw || eq2bw || eq3bw ||
               eq1gain || eq2gain || eq3gain || !eq1gain_oncc.empty() ||
               !eq2gain_oncc.empty() || !eq3gain_oncc.empty() ||
               !eq1freq_oncc.empty() || !eq2freq_oncc.empty() || !eq3freq_oncc.empty() ||
               !eq1bw_oncc.empty() || !eq2bw_oncc.empty() || !eq3bw_oncc.empty();
    }
    
    void EqSmoothStepImpl::Copy(const EqSmoothStepImpl& eq) {
        EqImpl::Copy(eq);
        
        eq1freq_smoothcc = eq.eq1freq_smoothcc;
        eq2freq_smoothcc = eq.eq2freq_smoothcc;
        eq3freq_smoothcc = eq.eq3freq_smoothcc;
        eq1bw_smoothcc   = eq.eq1bw_smoothcc;
        eq2bw_smoothcc   = eq.eq2bw_smoothcc;
        eq3bw_smoothcc   = eq.eq3bw_smoothcc;
        eq1gain_smoothcc = eq.eq1gain_smoothcc;
        eq2gain_smoothcc = eq.eq2gain_smoothcc;
        eq3gain_smoothcc = eq.eq3gain_smoothcc;
        
        eq1freq_stepcc = eq.eq1freq_stepcc;
        eq2freq_stepcc = eq.eq2freq_stepcc;
        eq3freq_stepcc = eq.eq3freq_stepcc;
        eq1bw_stepcc   = eq.eq1bw_stepcc;
        eq2bw_stepcc   = eq.eq2bw_stepcc;
        eq3bw_stepcc   = eq.eq3bw_stepcc;
        eq1gain_stepcc = eq.eq1gain_stepcc;
        eq2gain_stepcc = eq.eq2gain_stepcc;
        eq3gain_stepcc = eq.eq3gain_stepcc;
    }
    
    void EqSmoothStepImpl::copySmoothValues() {
        File::copySmoothValues(eq1freq_smoothcc, eq1freq_oncc);
        eq1freq_smoothcc.clear();
        
        File::copySmoothValues(eq2freq_smoothcc, eq2freq_oncc);
        eq2freq_smoothcc.clear();
        
        File::copySmoothValues(eq3freq_smoothcc, eq3freq_oncc);
        eq3freq_smoothcc.clear();
        
        File::copySmoothValues(eq1bw_smoothcc, eq1bw_oncc);
        eq1bw_smoothcc.clear();
        
        File::copySmoothValues(eq2bw_smoothcc, eq2bw_oncc);
        eq2bw_smoothcc.clear();
        
        File::copySmoothValues(eq3bw_smoothcc, eq3bw_oncc);
        eq3bw_smoothcc.clear();
        
        File::copySmoothValues(eq1gain_smoothcc, eq1gain_oncc);
        eq1gain_smoothcc.clear();
        
        File::copySmoothValues(eq2gain_smoothcc, eq2gain_oncc);
        eq2gain_smoothcc.clear();
        
        File::copySmoothValues(eq3gain_smoothcc, eq3gain_oncc);
        eq3gain_smoothcc.clear();
    }
    
    void EqSmoothStepImpl::copyStepValues() {
        File::copyStepValues(eq1freq_stepcc, eq1freq_oncc);
        eq1freq_stepcc.clear();
        
        File::copyStepValues(eq2freq_stepcc, eq2freq_oncc);
        eq2freq_stepcc.clear();
        
        File::copyStepValues(eq3freq_stepcc, eq3freq_oncc);
        eq3freq_stepcc.clear();
        
        File::copyStepValues(eq1bw_stepcc, eq1bw_oncc);
        eq1bw_stepcc.clear();
        
        File::copyStepValues(eq2bw_stepcc, eq2bw_oncc);
        eq2bw_stepcc.clear();
        
        File::copyStepValues(eq3bw_stepcc, eq3bw_oncc);
        eq3bw_stepcc.clear();
        
        File::copyStepValues(eq1gain_stepcc, eq1gain_oncc);
        eq1gain_stepcc.clear();
        
        File::copyStepValues(eq2gain_stepcc, eq2gain_oncc);
        eq2gain_stepcc.clear();
        
        File::copyStepValues(eq3gain_stepcc, eq3gain_oncc);
        eq3gain_stepcc.clear();
    }

    EG& File::eg(int x) {
        while (pCurDef->eg.size() <= x) {
            pCurDef->eg.add(EG());
        }
        return pCurDef->eg[x];
    }

    EGNode& File::egnode(int x, int y) {
        EG& e = eg(x);
        while (e.node.size() <= y) {
            e.node.add(EGNode());
        }
        return e.node[y];
    }

    LFO& File::lfo(int x) {
        while (pCurDef->lfos.size() <= x) {
            pCurDef->lfos.add(LFO());
        }
        return pCurDef->lfos[x];
    }

} // !namespace sfz
