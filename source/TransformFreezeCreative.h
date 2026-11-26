#pragma once
// Included by TransformFreeze.h after Controls and Status are declared.
// Separate state for pitched single-cycle capture and rhythm-driven percussion.
class CreativeEngine
{
    static constexpr int tableSize = 1024;
    struct Voice
    {
        std::array<std::array<float, tableSize>, 2> table {}, previous {}, cycleCache {};
        int cacheBuilt = 0, cacheFade = 0;
        double phase = 0, frequency = 261.625565, playFrequency = 261.625565;
        double capturedPeriod = 1, previousPeriod = 1;
        double gain = 0, velocity = 1, transition = 1;
        double dcInput[2] {}, dcOutput[2] {};
        std::size_t retry = 0;
        int note = 60, channel = 0;
        bool used = false, held = false, captured = false;
        std::uint64_t age = 0;
    };
    std::array<Voice, 8> voices {};
    Voice continuous {};
    struct HeldNote { std::uint64_t order = 0; double velocity = 0; };
    std::array<HeldNote, 16 * 128> heldNotes {};
    double glideCoeff = 1;
    Controls c {};
    double sr = 44100, blend = 1, blendStep = .002, attackStep = .001, releaseStep = .001;
    double env = 0, collision[2] {}, low[2] {}, high[2] {};
    double dcCoefficient = .997, noiseScale = 1;
    double enableFade = 0;
    double attackCoeff = .01, releaseCoeff = .001, collisionDecay = .99, lowCoeff = .1, highCoeff = .3;
    double formantRatio = 1, pitchRatio = 1, collisionProbability = .01, transitionStep = .002;
    double renderedFormant = 1, renderedSmooth = 1, controlCoeff = .002;
    std::size_t write = 0, valid = 0, sinceSignal = 0, sinceCapture = 0, onsetAge = 0;
    std::uint64_t serial = 0;
    std::uint32_t rng = 0x9318c531u;
    bool enabled = false, stereo = true, hasSignal = false;
    static double frequency(int note) noexcept { return 440.0 * std::exp2((note - 69) / 12.0); }
    double random01() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng / 4294967296.0; }
    static double bounded(float v) noexcept { return std::isfinite(v) ? std::clamp(static_cast<double>(v), -1.0, 1.0) : 0.0; }
    using History = std::array<std::vector<float>, 2>;
public:
    void prepare(double rate, bool twoChannels) noexcept { sr = rate; stereo = twoChannels; dcCoefficient = std::exp(-6.283185307179586 * 15.0 / sr); noiseScale = std::sqrt(sr / 48000.0); reset(); }
    void reset() noexcept
    {
        voices = {}; continuous = {}; heldNotes = {}; c = {}; enabled = false; write = valid = sinceSignal = sinceCapture = 0;
        serial = 0; hasSignal = false; onsetAge = 0; renderedFormant = renderedSmooth = 1; blend = 1; env = 0; enableFade = 0; rng = 0x9318c531u;
        for (int i = 0; i < 2; ++i) collision[i] = low[i] = high[i] = 0;
    }
    void setControls(bool active, const Controls& values) noexcept
    {
        if (!active && !enabled) return;
        if (active && !enabled) { reset(); blend = values.blend * .01; }
        const bool modeChanged = values.mode != c.mode || values.algorithm != c.algorithm
            || (values.algorithm == 1 && values.mode == 2 && values.voiceMode != c.voiceMode);
        if (modeChanged) { voices = {}; continuous = {}; heldNotes = {}; sinceCapture = 0; }
        if (values.glide != c.glide || !enabled)
            glideCoeff = values.glide > 0 ? 1 - std::exp(-4.605170186 / (values.glide * .001 * sr)) : 1;
        if (values.attack != c.attack || values.release != c.release || !enabled)
        {
            attackStep = 1 / std::max(1.0, values.attack * .001 * sr);
            releaseStep = 1 / std::max(1.0, values.release * .001 * sr);
            attackCoeff = 1 - std::exp(-attackStep); releaseCoeff = 1 - std::exp(-releaseStep);
            blendStep = 1 / (.010 * sr); transitionStep = 1 / (.020 * sr);
            controlCoeff = 1 - std::exp(-1 / (.010 * sr));
        }
        if (values.pitch != c.pitch || !enabled || modeChanged)
        {
            pitchRatio = std::exp2(values.pitch / 12.0);
            for (auto& v : voices) if (v.used) v.frequency = frequency(v.note) * pitchRatio;
            continuous.frequency = frequency(60) * pitchRatio;
        }
        if (values.formant != c.formant || !enabled) formantRatio = std::exp2(values.formant / 12.0);
        if (!enabled || modeChanged) { renderedFormant = formantRatio; renderedSmooth = values.smooth * .01; continuous.playFrequency = continuous.frequency; }
        if (values.algorithm == 2 && (values.size != c.size || values.pitch != c.pitch || values.smooth != c.smooth || values.density != c.density || !enabled))
        {
            collisionDecay = std::exp(-1 / (sr * values.size * .001 * .22));
            const double base = std::clamp(1500.0 * pitchRatio, 250.0, .18 * sr);
            const double top = std::clamp(base * (4.5 - values.smooth * .025), base * 1.1, .45 * sr);
            lowCoeff = 1 - std::exp(-6.283185307179586 * base / sr);
            highCoeff = 1 - std::exp(-6.283185307179586 * top / sr);
            collisionProbability = (80.0 + (values.density - 2.0) * 100.0) / sr;
        }
        c = values; enabled = active;
        if (!active) { heldNotes = {}; for (auto& v : voices) v.held = false; }
    }
    bool acceptsMidi() const noexcept { return enabled && c.algorithm == 1 && c.mode == 2; }
    void midiEvent(const std::uint8_t* data, int bytes) noexcept
    {
        if (!acceptsMidi() || !data || bytes != 3) return;
        const int type = data[0] & 0xf0, ch = data[0] & 15, note = data[1] & 127;
        if (c.voiceMode != 0)
        {
            monoMidi(type, ch, note, data[2] & 127);
            return;
        }
        if (type == 0x90 && data[2])
        {
            Voice* v = nullptr;
            for (auto& x : voices) if (x.used && x.note == note && x.channel == ch) { v = &x; break; }
            if (!v) for (auto& x : voices) if (!x.used) { v = &x; break; }
            if (!v) for (auto& x : voices) if (!x.held && (!v || x.gain < v->gain)) v = &x;
            if (!v) v = &*std::min_element(voices.begin(), voices.end(), [](const Voice& a, const Voice& b){ return a.age < b.age; });
            *v = {}; v->used = v->held = true; v->note = note; v->channel = ch; v->age = ++serial;
            v->frequency = v->playFrequency = frequency(note) * pitchRatio; v->velocity = std::sqrt((data[2] & 127) / 127.0);
        }
        else if (type == 0x80 || (type == 0x90 && !data[2]))
        { for (auto& v : voices) if (v.used && v.note == note && v.channel == ch) v.held = false; }
        else if (type == 0xb0 && (note == 120 || note == 123))
        { for (auto& v : voices) if (v.channel == ch) v.held = false; }
    }
    Status status() const noexcept
    {
        if (!enabled) return Status::bypassed;
        if (c.algorithm == 2 || c.mode == 0) return Status::live;
        if (c.mode == 1) return continuous.captured ? Status::captured : Status::armed;
        for (const auto& v : voices) if (v.captured) return Status::captured;
        return Status::armed;
    }
    void process(float& left, float& right, History& history) noexcept
    {
        if ((!enabled && enableFade <= 0) || history[0].empty()) return;
        enableFade = std::clamp(enableFade + (enabled ? blendStep : -blendStep), 0.0, 1.0);
        const double target = c.blend * .01;
        blend += std::clamp(target - blend, -blendStep, blendStep);
        const double l = bounded(left), r = stereo ? bounded(right) : l;
        double wetL = 0, wetR = 0;
        if (c.algorithm == 2)
        {
            // Independent synthetic collisions, driven by the input's amplitude contour.
            const double input = std::max(std::abs(l), std::abs(r));
            env += (input > env ? attackCoeff : releaseCoeff) * (input - env);
            if (env < 1e-7 && input < 1e-7) { env = 0; for (int i=0;i<2;++i) collision[i]=low[i]=high[i]=0; }
            if (blend > 0 && env > 0)
            {
                const double common = random01();
                const double noise = random01() * 2 - 1;
                for (int i=0; i<(stereo ? 2 : 1); ++i)
                {
                    const double variation = c.drift * .01;
                    const double event = random01() < variation ? random01() : common;
                    if (event < collisionProbability) collision[i] = std::min(1.0, collision[i] + .35 + .65*random01());
                    collision[i] *= collisionDecay;
                    const double n = noise * (1-variation) + (random01()*2-1)*variation;
                    low[i] += lowCoeff * (n - low[i]); high[i] += highCoeff * (n - high[i]);
                    const double out = std::clamp((high[i]-low[i]) * collision[i] * env * 4.0 * noiseScale, -1.0, 1.0);
                    if (i == 0) wetL = out; else wetR = out;
                }
                if (!stereo) wetR = wetL;
            }
        }
        else
        {
            const auto n = history[0].size();
            history[0][write] = static_cast<float>(l); if (stereo) history[1][write] = static_cast<float>(r);
            if (++write == n) write = 0;
            valid = std::min(valid + 1, n);
            if (std::max(std::abs(l), std::abs(r)) > .001)
            {
                if (!hasSignal || sinceSignal > static_cast<std::size_t>(.025*sr)) onsetAge = 0;
                sinceSignal = 0; hasSignal = true;
            }
            else { sinceSignal = std::min(sinceSignal + 1, n); if (sinceSignal >= valid) hasSignal = false; }
            onsetAge = std::min(onsetAge + 1, n);
            renderedFormant += controlCoeff * (formantRatio - renderedFormant);
            renderedSmooth += controlCoeff * (c.smooth*.01 - renderedSmooth);
            if (std::abs(renderedFormant-formantRatio)<1e-5) renderedFormant=formantRatio;
            if (std::abs(renderedSmooth-c.smooth*.01)<1e-5) renderedSmooth=c.smooth*.01;
            if (blend > 0 && c.mode == 2)
            {
                double weights = 0;
                for (auto& v : voices)
                {
                    if (!v.used) continue;
                    if (!v.held && !v.captured) { v.used = false; continue; }
                    if (!v.captured && v.held) capture(v, history);
                    if (!v.captured) continue;
                    v.gain = std::clamp(v.gain + (v.held ? attackStep : -releaseStep), 0.0, 1.0);
                    if (!v.held && v.gain <= 0) { v.used = false; continue; }
                    double a=0,b=0; render(v,a,b); const double g=v.gain*v.gain*(3-2*v.gain)*v.velocity;
                    wetL += a*g; wetR += b*g; weights += g;
                }
                const double normal = std::max(1.0, weights); wetL /= normal; wetR /= normal;
            }
            else if (blend > 0)
            {
                if (c.mode == 0 && ++sinceCapture >= static_cast<std::size_t>(.04*sr))
                { sinceCapture=0; if (sinceSignal < static_cast<std::size_t>(.03*sr)) capture(continuous,history); }
                if (!continuous.captured) capture(continuous,history);
                if (continuous.captured)
                {
                    const bool audible = c.mode == 1 || sinceSignal < static_cast<std::size_t>(.05*sr);
                    continuous.gain = std::clamp(continuous.gain + (audible ? attackStep : -releaseStep),0.0,1.0);
                    if (continuous.gain > 0) { render(continuous,wetL,wetR); const double g=continuous.gain*continuous.gain*(3-2*continuous.gain); wetL*=g; wetR*=g; }
                }
            }
            else for (auto& v : voices) if (!v.held) v.used = false;
        }
        const double amount = blend * enableFade;
        left = static_cast<float>(left*(1-amount) + wetL*amount);
        right = stereo ? static_cast<float>(right*(1-amount) + wetR*amount) : left;
    }
private:
    // Last-note priority. Legato retains the captured waveform and envelope on
    // overlapping notes; Mono retriggers capture. No allocation on MIDI events.
    void monoMidi(int type, int ch, int note, int velocity) noexcept
    {
        auto& v = voices[0];
        const auto index = static_cast<std::size_t>(ch * 128 + note);
        if (type == 0x90 && velocity != 0)
            heldNotes[index] = { ++serial, std::sqrt(velocity / 127.0) };
        else if (type == 0x80 || (type == 0x90 && velocity == 0))
            heldNotes[index] = {};
        else if (type == 0xb0 && (note == 120 || note == 123))
        {
            for (int i = 0; i < 128; ++i) heldNotes[static_cast<std::size_t>(ch * 128 + i)] = {};
        }
        else return;
        std::size_t best = 0;
        for (std::size_t i = 1; i < heldNotes.size(); ++i)
            if (heldNotes[i].order > heldNotes[best].order) best = i;
        if (heldNotes[best].order == 0) { v.held = false; return; }
        const int nextNote = static_cast<int>(best % 128), nextChannel = static_cast<int>(best / 128);
        if (v.held && v.note == nextNote && v.channel == nextChannel && v.age == heldNotes[best].order) return;
        const bool overlap = v.used && v.held;
        const double oldFrequency = v.playFrequency;
        const bool retain = c.voiceMode == 2 && overlap;
        if (!retain) v = {};
        v.used = v.held = true; v.note = nextNote; v.channel = nextChannel; v.age = heldNotes[best].order;
        v.frequency = frequency(nextNote) * pitchRatio;
        v.playFrequency = overlap && c.glide > 0 ? oldFrequency : v.frequency;
        if (!retain) v.velocity = heldNotes[best].velocity;
    }
    bool capture(Voice& v, const History& h) noexcept
    {
        if (!hasSignal || valid < 8 || sinceSignal >= valid) return false;
        if (v.retry > 0) { --v.retry; return false; }
        const double period = sr / std::clamp(v.frequency, 8.0, sr*.45);
        const std::size_t available = std::min(valid - sinceSignal, static_cast<std::size_t>(c.capture*.001*sr));
        // Do not freeze the first isolated sample of a fresh transient. A held note
        // waits for useful audio; previously recorded material remains playable.
        if (c.position < 1 && sinceSignal < static_cast<std::size_t>(.025*sr)
            && onsetAge < static_cast<std::size_t>(std::min(period, .020*sr))) return false;
        // Extra leading material permits a true overlap seam even at +24 st FORMANT.
        const double length = std::min(5.5*period, static_cast<double>(available));
        if (length < std::min(period, 8.0)) return false;
        const double space = std::max(0.0, static_cast<double>(available)-length);
        const double offset = space * std::clamp(c.position*.01 + (random01()-.5)*c.drift*.01,0.0,1.0);
        const auto n=h[0].size();
        const double anchor = static_cast<double>(n + write) - 1 - static_cast<double>(sinceSignal) - offset;
        const auto read = [&](int ch, double p) noexcept
        {
            const auto whole = static_cast<std::size_t>(std::max(0.0,p));
            const double frac = p-std::floor(p);
            const auto a = whole >= n ? whole-n : whole;
            const auto b = a+1 == n ? 0 : a+1;
            return h[ch][a]+frac*(h[ch][b]-h[ch][a]);
        };
        // Search only a small neighbourhood around POSITION, preferring an energetic
        // cycle with matching endpoints over an arbitrary noisy boundary.
        const double span = std::min(period*formantRatio, length*.80);
        const double search = std::min(period*.75, space-offset);
        double end = anchor, best = -1;
        for (int k=0; k<=8; ++k)
        {
            const double candidate = anchor-search*k/8.0;
            double energy=0, seam=0;
            for (int ch=0; ch<(stereo?2:1); ++ch)
            {
                double mean=0, sq=0;
                for (int i=0;i<16;++i) { const double x=read(ch,candidate-span*(i+.5)/16); mean+=x; sq+=x*x; }
                energy += std::max(0.0,sq/16-mean*mean/256);
                const double delta=read(ch,candidate)-read(ch,candidate-span); seam+=delta*delta;
            }
            const double score = energy/(.00001+energy+seam*.25) * std::sqrt(energy) / (1+.05*k);
            if (score>best) { best=score; end=candidate; }
        }
        if (best < 1e-6) { if (!v.captured) v.retry = static_cast<std::size_t>(.001*sr); return false; }
        if (v.captured) { v.previous = v.table; v.previousPeriod=v.capturedPeriod; v.transition=0; } else v.transition=1;
        for (int ch=0; ch<(stereo?2:1); ++ch)
        {
            for (int i=0;i<tableSize;++i)
            {
                const double p = end - length + 1 + (length-1)*i/(tableSize-1);
                v.table[ch][i]=static_cast<float>(read(ch,p));
            }
        }
        v.capturedPeriod=std::clamp(period/std::max(1.0,length-1), 1.0/tableSize, 1.0);
        v.cacheBuilt=v.cacheFade=0; v.captured=true; return true;
    }
    static double cubicSample(const std::array<float,tableSize>& tab, double p) noexcept
    {
        p=std::clamp(p,0.0,static_cast<double>(tableSize-1));
        const int i=static_cast<int>(p); const double t=p-i;
        const double a=tab[std::max(0,i-1)], b=tab[i], c0=tab[std::min(i+1,tableSize-1)], d=tab[std::min(i+2,tableSize-1)];
        return b+.5*t*(c0-a+t*(2*a-5*b+4*c0-d+t*(3*(b-c0)+d-a)));
    }
    double cycle(const std::array<float,tableSize>& tab,double phase,double span,double overlap) const noexcept
    {
        // Overlap the end with the material immediately before the start. At the
        // wrap both value and slope now follow the same source, without a ramp.
        if (phase < 0) phase += 1; else if (phase >= 1) phase -= 1;
        const double start=(tableSize-1)-span;
        const double main=cubicSample(tab,start+phase*span);
        if (phase <= 1-overlap) return main;
        const double t=(phase-(1-overlap))/overlap;
        const double weight=t*t*(3-2*t);
        return main+(cubicSample(tab,start+(phase-1)*span)-main)*weight;
    }
    void render(Voice& v, double& l, double& r) noexcept
    {
        const double slew = c.mode == 2 && c.voiceMode != 0 ? glideCoeff : controlCoeff;
        v.playFrequency += slew*(v.frequency-v.playFrequency);
        if (std::abs(v.playFrequency-v.frequency)<.001) v.playFrequency=v.frequency;
        const double increment=std::clamp(v.playFrequency,8.0,sr*.45)/sr;
        const double phase=c.direction==0?v.phase:1-v.phase;
        const double overlap=.035+.165*renderedSmooth;
        // Small periodic smoothing reduces brittle upper harmonics at high SMOOTH;
        // widen the kernel at high notes to reduce (not eliminate) foldover.
        const double width=std::max(.018*renderedSmooth,.5*increment);
        const auto get = [&](const auto& tab,double period,double at) noexcept
        {
            const double span=std::clamp(period*renderedFormant,1.0/tableSize,1.0/(1+overlap))*(tableSize-1);
            const double middle=cycle(tab,at,span,overlap);
            const double sides=cycle(tab,at-width,span,overlap)+cycle(tab,at+width,span,overlap);
            const double weight=.12+.13*renderedSmooth;
            return std::clamp(middle+(sides-2*middle)*weight,-1.0,1.0);
        };
        const bool stable = renderedFormant == formantRatio && renderedSmooth == c.smooth*.01
                         && v.playFrequency == v.frequency && v.transition >= 1;
        if (!stable) v.cacheBuilt=v.cacheFade=0;
        else if (v.cacheBuilt < tableSize)
        {
            // Amortise the periodic table construction over 64 samples rather than
            // rebuild every voice in the MIDI note-on audio block. No allocation.
            const int limit=std::min(v.cacheBuilt+16,tableSize);
            for (int i=v.cacheBuilt;i<limit;++i)
                for (int ch=0;ch<(stereo?2:1);++ch)
                    v.cycleCache[ch][i]=static_cast<float>(get(v.table[ch],v.capturedPeriod,static_cast<double>(i)/tableSize));
            v.cacheBuilt=limit;
        }
        const auto cached = [&](int ch) noexcept
        {
            const double p=(phase>=1?phase-1:phase)*tableSize;
            const int whole=static_cast<int>(p); const double t=p-whole; const int i=whole&(tableSize-1);
            const auto& tab=v.cycleCache[ch];
            const double a=tab[(i+tableSize-1)&(tableSize-1)], b=tab[i], cc=tab[(i+1)&(tableSize-1)], d=tab[(i+2)&(tableSize-1)];
            return std::clamp(b+.5*t*(cc-a+t*(2*a-5*b+4*cc-d+t*(3*(b-cc)+d-a))),-1.0,1.0);
        };
        if (v.cacheBuilt == tableSize && v.cacheFade >= 16)
        {
            l=cached(0); r=stereo?cached(1):l;
        }
        else
        {
            l=get(v.table[0],v.capturedPeriod,phase); r=stereo?get(v.table[1],v.capturedPeriod,phase):l;
            if (v.transition < 1)
            {
                const double t=v.transition*v.transition*(3-2*v.transition);
                l=get(v.previous[0],v.previousPeriod,phase)*(1-t)+l*t;
                r=stereo?get(v.previous[1],v.previousPeriod,phase)*(1-t)+r*t:l;
                v.transition=std::min(1.0,v.transition+transitionStep);
            }
            if (v.cacheBuilt == tableSize)
            {
                const double t=++v.cacheFade/16.0;
                l+=(cached(0)-l)*t; r=stereo?r+(cached(1)-r)*t:l;
            }
        }
        // Remove DC before the amplitude envelope. A released note cannot leave
        // a residual DC-blocker tail or an abrupt shared-filter reset.
        const auto block = [&](int ch,double x) noexcept
        {
            const double y=x-v.dcInput[ch]+dcCoefficient*v.dcOutput[ch];
            v.dcInput[ch]=x; v.dcOutput[ch]=y;
            return std::clamp(y,-1.0,1.0);
        };
        l=block(0,l); r=stereo?block(1,r):l;
        v.phase += increment; if (v.phase>=1) v.phase-=1;
    }
};
