#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <xmmintrin.h>

namespace transform::vocoder
{
inline constexpr std::array<const char*, 4> carrierNames {{ "Saw", "Pulse", "Noise", "Sidechain" }};
inline constexpr std::array<const char*, 6> bandNames {{ "8", "12", "16", "24", "32", "48" }};
inline constexpr std::array<int, 6> bandCounts {{ 8, 12, 16, 24, 32, 48 }};
struct Controls { float attack=5, release=45, formant=0, width=100, noise=0, blend=0; int carrier=0, bands=3; float bandwidth=100, detune=0, unvoiced=65; float shift=0, contour=0, compand=0, modIn=0, carIn=0, harmonic=0, sub=0; };
class Engine
{
    static constexpr int maxBands=48;
    static constexpr double pi=3.14159265358979323846;
    using Bands = std::array<float, maxBands>;
    // Coefficients are identical for modulator/carrier and both channels.
    // Contiguous state arrays allow four independent filters per SSE vector.
    struct Bank {
        Bands modZ1{}, modZ2{}, carZ1{}, carZ2{}, env{}, carrierEnv{};
        void clearCarrier() noexcept { carZ1.fill(0); carZ2.fill(0); carrierEnv.fill(0); }
        void clear() noexcept { modZ1.fill(0); modZ2.fill(0); env.fill(0); clearCarrier(); }
    };
    Bands coefficientB{}, coefficientA1{}, coefficientA2{};
    static __m128 absolute(__m128 x) noexcept { return _mm_andnot_ps(_mm_set1_ps(-0.0f), x); }
    static __m128 select(__m128 mask, __m128 yes, __m128 no) noexcept
    { return _mm_or_ps(_mm_and_ps(mask, yes), _mm_andnot_ps(mask, no)); }
    static __m128 follow(__m128 env, __m128 x, __m128 attack, __m128 release) noexcept
    { return _mm_add_ps(env, _mm_mul_ps(_mm_sub_ps(x, env), select(_mm_cmpgt_ps(x, env), attack, release))); }
    __m128 filter(int i, __m128 x, Bands& z1, Bands& z2) noexcept
    {
        const auto b = _mm_loadu_ps(coefficientB.data() + i);
        const auto a1 = _mm_loadu_ps(coefficientA1.data() + i);
        const auto a2 = _mm_loadu_ps(coefficientA2.data() + i);
        const auto bx = _mm_mul_ps(b, x);
        const auto y = _mm_add_ps(bx, _mm_loadu_ps(z1.data() + i));
        _mm_storeu_ps(z1.data() + i, _mm_sub_ps(_mm_loadu_ps(z2.data() + i), _mm_mul_ps(a1, y)));
        // Preserve the scalar operation (-b*x) - a2*y, including its sign.
        _mm_storeu_ps(z2.data() + i, _mm_sub_ps(_mm_mul_ps(_mm_xor_ps(b, _mm_set1_ps(-0.0f)), x), _mm_mul_ps(a2, y)));
        return y;
    }
    struct Voice { int note=-1, channel=0; bool held=false; float level=0, velocity=0; double phase=0, phaseR=0, subPhase=0, subPhaseR=0, delta=0; unsigned age=0; };
public:
    void prepare(double rate, int channels, int delay) {
        sr=std::max(8000.0,rate); stereo=channels>1;
        delayL.assign(static_cast<std::size_t>(std::max(0,delay)),0.0f); delayR=delayL; delayGate=delayL;
        fadeStep=static_cast<float>(1/(sr*.02)); smooth=coefficient(20); voiceAttack=coefficient(5); voiceRelease=coefficient(35); noiseLP=coefficient(.05f);
        consonantLP=static_cast<float>(-std::expm1(-2*pi*2000.0/sr));
        gateAttackStep=static_cast<float>(1/(sr*.005)); gateReleaseStep=static_cast<float>(1/(sr*.035));
        carrierAttack=coefficient(8); carrierRelease=coefficient(60); headroomRelease=coefficient(80);
        consonantAttack=coefficient(3); consonantRelease=coefficient(25);
        reset();
    }
    void reset() noexcept {
        enabled=false; wet=0; mode=0; count=0; formantNow=0; widthNow=1; noiseNow=0; voices={}; age=0; attackMs=releaseMs=-1; validDelay=0; noiseState=0x243f6a88u; noiseLow={}; delayPos=0;
        shiftNow=contourValue=999; compand=compandNow=harmonic=harmonicNow=sub=subNow=0; modDb=carDb=999; modGain=modGainNow=carGain=carGainNow=1; bandWeights.fill(1); weightsNow.fill(1); contourActive=false;
        bandwidthNow=-1; bandGain=bandGainNow=1; detuneValue=-1; detuneL=detuneR=1; unvoiced=unvoicedNow=0; consonants={}; noteGate=0; lastGate=0; externalEnv=0; headroomGain=1; sleeping=true;
        std::fill(delayL.begin(),delayL.end(),0.0f); std::fill(delayR.begin(),delayR.end(),0.0f); std::fill(delayGate.begin(),delayGate.end(),0.0f);
        for(auto& channel : bank) channel.clear();
    }
    void disable() noexcept { enabled=false; for(auto& v:voices) v.held=false; }
    bool acceptsMidi() const noexcept { return enabled; }
    void midiEvent(const std::uint8_t* d,int n) noexcept {
        if(!enabled || n<3 || d==nullptr) return;
        const int kind=d[0]&0xf0, ch=d[0]&15, note=d[1]&127;
        if(kind==0x90 && d[2]!=0) {
            Voice* v=nullptr;
            for(auto& item:voices) if(item.note==note && item.channel==ch) { v=&item; break; }
            if(!v) for(auto& item:voices) if(item.note<0) { v=&item; break; }
            if(!v) { v=&voices[0]; for(auto& item:voices) if((!item.held && v->held) || (item.held==v->held && item.age<v->age)) v=&item; }
            if(v->note!=note || v->channel!=ch) { v->level=0; v->phase=v->phaseR=v->subPhase=v->subPhaseR=0; }
            v->note=note; v->channel=ch; v->held=true; v->velocity=static_cast<float>(d[2]&127)/127; v->age=++age;
            v->delta=std::min(.45,440.0*std::pow(2.0,(note-69)/12.0)/sr);
        } else if(kind==0x80 || kind==0x90) {
            for(auto& v:voices) if(v.note==note && v.channel==ch) { if(sleeping) v=Voice{}; else v.held=false; }
        } else if(kind==0xb0 && (note==120 || note==123)) {
            for(auto& v:voices) if(v.channel==ch) { v.held=false; if(note==120 || sleeping) v=Voice{}; }
        }
    }
    void setControls(bool active, const Controls& c) noexcept {
        if(!active) { disable(); return; }
        enabled=true;
        const int newMode=std::clamp(c.carrier,0,3), newCount=bandCounts[static_cast<std::size_t>(std::clamp(c.bands,0,5))];
        const float bw=std::clamp(c.bandwidth,50.0f,200.0f);
        const float shift=std::clamp(c.shift,-12.0f,12.0f);
        const bool retune=newCount!=count || shiftNow!=shift;
        if(retune || bandwidthNow!=bw) {
            const bool resetBank=newCount!=count;
            count=newCount; bandwidthNow=bw; shiftNow=shift;
            const double ratio=std::pow(2.0,shift/12.0), bottom=100.0*ratio;
            const double top=std::min(12000.0*ratio,sr*.42), step=std::log(top/bottom)/(count-1);
            const double inverse2q=std::sinh(step*.5)*static_cast<double>(bw)*.01;
            for(int i=0;i<count;++i) {
                if(retune) {
                    const double w=2*pi*bottom*std::exp(step*i)/sr;
                    bandSin[i]=std::sin(w); bandCos[i]=std::cos(w);
                }
                const double alpha=bandSin[i]*inverse2q, norm=1/(1+alpha);
                coefficientB[i]=static_cast<float>(alpha*norm);
                coefficientA1[i]=static_cast<float>(-2*bandCos[i]*norm);
                coefficientA2[i]=static_cast<float>((1-alpha)*norm);
            }
            if(resetBank) for(auto& ch:bank) ch.clear();
            semitonesPerBand=static_cast<float>(12*step/std::log(2.0));
            // Approximate overlap gain compensation as bandwidth changes.
            bandGain=100.0f/bw;
            if(resetBank) bandGainNow=bandGain;
        }
        const float contour=std::clamp(c.contour,-12.0f,12.0f);
        if(retune || contourValue!=contour) {
            contourValue=contour;
            if(contour!=0) contourActive=true;
            for(int i=0;i<count;++i) bandWeights[i]=std::pow(10.0f,contour*(2.0f*i/(count-1)-1.0f)/20.0f);
        }
        compand=std::clamp(c.compand*.01f,0.0f,1.0f);
        harmonic=std::clamp(c.harmonic*.01f,0.0f,1.0f);
        sub=std::clamp(c.sub*.01f,0.0f,1.0f);
        if(modDb!=c.modIn) { modDb=c.modIn; modGain=std::pow(10.0f,std::clamp(modDb,-24.0f,24.0f)/20.0f); }
        if(carDb!=c.carIn) { carDb=c.carIn; carGain=std::pow(10.0f,std::clamp(carDb,-24.0f,24.0f)/20.0f); }
        if(mode!=newMode) { mode=newMode; delayPos=0; validDelay=0; noteGate=lastGate=externalEnv=0; headroomGain=1; consonants={}; noiseLow={}; for(auto& ch:bank) ch.clearCarrier(); }
        if(attackMs!=c.attack) { attackMs=c.attack; attack=coefficient(std::clamp(c.attack,1.0f,100.0f)); }
        if(releaseMs!=c.release) { releaseMs=c.release; release=coefficient(std::clamp(c.release,10.0f,500.0f)); }
        const float dt=std::clamp(c.detune,0.0f,100.0f);
        if(detuneValue!=dt) { detuneValue=dt; detuneL=std::pow(2.0,-dt/2400.0); detuneR=std::pow(2.0,dt/2400.0); }
        unvoiced=std::clamp(c.unvoiced*.01f,0.0f,1.0f);
        formant=std::clamp(c.formant,-12.0f,12.0f)/semitonesPerBand;
        width=std::clamp(c.width*.01f,0.0f,1.0f); noise=std::clamp(c.noise*.01f,0.0f,1.0f); blend=std::clamp(c.blend*.01f,0.0f,1.0f);
    }
    void process(float& l,float& r,float sideL=0,float sideR=0,bool sideAvailable=false) noexcept {
        const float target=enabled && (mode!=3 || sideAvailable)?blend:0;
        wet+=std::clamp(target-wet,-fadeStep,fadeStep);
        if(wet==0 && target==0) {
            // Clear stale filter and carrier state once, not on every idle sample.
            if(!sleeping) {
                for(auto& ch:bank) ch.clear();
                consonants={}; noiseLow={}; unvoicedNow=0;
                validDelay=0; delayPos=0; noteGate=lastGate=externalEnv=0;
                headroomGain=1; sleeping=true;
                for(auto& v:voices) { if(!v.held) v=Voice{}; else { v.level=0; v.phase=v.phaseR=v.subPhase=v.subPhaseR=0; } }
            }
            if(!enabled && count!=0) { count=0; voices={}; }
            return;
        }
        sleeping=false;
        compandNow+=(compand-compandNow)*smooth;
        harmonicNow+=(harmonic-harmonicNow)*smooth;
        subNow+=(sub-subNow)*smooth;
        modGainNow+=(modGain-modGainNow)*smooth;
        carGainNow+=(carGain-carGainNow)*smooth;
        if(contourActive) {
            const auto sm=_mm_set1_ps(smooth);
            for(int i=0;i<count;i+=4) {
                const auto v=_mm_loadu_ps(weightsNow.data()+i);
                _mm_storeu_ps(weightsNow.data()+i,_mm_add_ps(v,_mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(bandWeights.data()+i),v),sm)));
            }
        }
        float carrierL=0,carrierR=0, activity=1;
        if(mode==3) {
            carrierL=stereo?sideL:0.5f*(sideL+sideR); carrierR=stereo?sideR:carrierL;
        } else if(mode==2) { carrierL=random(); carrierR=stereo?random():carrierL; }
        else {
            float sumL=0, sumR=0, levels=0;
            bool anyHeld=false;
            for(auto& v:voices) if(v.note>=0) {
                anyHeld=anyHeld || v.held;
                v.level+=(v.held?v.velocity-v.level:-v.level)*(v.held?voiceAttack:voiceRelease);
                if(!v.held && v.level<1e-5f) { v=Voice{}; continue; }
                const double dl=std::min(.45,v.delta*detuneL), dr=std::min(.45,v.delta*detuneR);
                float oscL=oscillator(v.phase,dl);
                if(subNow>1e-6f) oscL+=subNow*subOscillator(v.subPhase,dl*.5);
                sumL+=oscL*v.level;
                if(detuneValue>0) {
                    float oscR=oscillator(v.phaseR,dr);
                    if(subNow>1e-6f) oscR+=subNow*subOscillator(v.subPhaseR,dr*.5);
                    sumR+=oscR*v.level;
                }
                v.subPhase+=dl*.5; if(v.subPhase>=1) v.subPhase-=1;
                if(detuneValue>0) { v.subPhaseR+=dr*.5; if(v.subPhaseR>=1) v.subPhaseR-=1; } else v.subPhaseR=v.subPhase;
                levels+=v.level;
                v.phase+=dl; if(v.phase>=1) v.phase-=1;
                if(detuneValue>0) { v.phaseR+=dr; if(v.phaseR>=1) v.phaseR-=1; }
                else v.phaseR=v.phase;
            }
            const float norm=std::max(1.0f,levels);
            carrierL=sumL/norm; carrierR=detuneValue>0?sumR/norm:carrierL;
            if(!stereo) carrierL=carrierR=.5f*(carrierL+carrierR);
            noteGate=std::clamp(noteGate+(anyHeld?gateAttackStep:-gateReleaseStep),0.0f,1.0f);
            activity=noteGate;
            if(!anyHeld && noteGate==0) for(auto& v:voices) v=Voice{};
        }
        // Enrich the carrier before filtering; bounded rational saturator plus asymmetric term.
        const auto enrich=[this](float x) {
            x=std::isfinite(x)?std::clamp(x,-16.0f,16.0f):0.0f;
            if(harmonicNow>1e-6f) { const float z=x*3.0f; const float shaped=z/(1.0f+std::abs(z)); x+=harmonicNow*(shaped+.25f*shaped*shaped-x); }
            return inputHeadroom(x*carGainNow);
        };
        if(harmonicNow>1e-6f || carGainNow!=1.0f || std::abs(carrierL)>1.0f || std::abs(carrierR)>1.0f) { carrierL=enrich(carrierL); carrierR=enrich(carrierR); }
        // Align both internal and external carriers with the existing lead delay.
        if(!delayL.empty()) {
            const float dl=validDelay>=delayL.size()?delayL[delayPos]:0, dr=validDelay>=delayR.size()?delayR[delayPos]:0;
            const float dg=validDelay>=delayGate.size()?delayGate[delayPos]:0;
            delayL[delayPos]=carrierL; delayR[delayPos]=carrierR; delayGate[delayPos]=activity; if(++delayPos==delayL.size()) delayPos=0;
            if(validDelay<delayL.size()) ++validDelay;
            carrierL=dl; carrierR=dr; activity=dg;
        }
        if(mode==3) {
            const float a=std::max(std::abs(carrierL),std::abs(carrierR));
            externalEnv+=(a-externalEnv)*(a>externalEnv?carrierAttack:carrierRelease);
            activity*=std::clamp((externalEnv-1e-5f)*500.0f,0.0f,1.0f);
        }
        if(activity==0 && lastGate>0) {
            for(auto& ch:bank) ch.clearCarrier();
            noiseLow={};
        }
        lastGate=activity;
        formantNow+=(formant-formantNow)*smooth;
        widthNow+=(width-widthNow)*smooth;
        noiseNow+=(noise-noiseNow)*smooth;
        unvoicedNow+=(unvoiced-unvoicedNow)*smooth;
        bandGainNow+=(bandGain-bandGainNow)*smooth;
        const float offset=-formantNow;
        const int mapOffset=static_cast<int>(std::floor(offset));
        const float frac=offset-static_cast<float>(mapOffset);
        const auto condition=[this](float x) {
            x=inputHeadroom(x*modGainNow);
            // Parallel soft compression lifts quiet speech while limiting loud peaks.
            if(compandNow>1e-6f) x+=compandNow*(x*3.0f/(1.0f+2.0f*std::abs(x))-x);
            return x;
        };
        const float vl=channelProcess(0,modGainNow==1.0f && compandNow<=1e-6f && std::abs(l)<=1.0f ? l : condition(l),carrierL,mapOffset,frac,activity), vr=stereo?channelProcess(1,modGainNow==1.0f && compandNow<=1e-6f && std::abs(r)<=1.0f ? r : condition(r),carrierR,mapOffset,frac,activity):vl;
        const float mid=(vl+vr)*.5f;
        float outL=mid+(vl-mid)*widthNow, outR=mid+(vr-mid)*widthNow;
        // Stereo-linked wet headroom: instant attenuation, gradual recovery.
        // Apply before the note gate so even a loud carrier fades to silence
        // proportionally. No dry audio is limited or reintroduced.
        const float peak=std::max(std::abs(outL),std::abs(outR));
        const float desired=peak>0.90f ? 0.90f/peak : 1.0f;
        if(desired<headroomGain) headroomGain=desired;
        else headroomGain=std::min(desired,headroomGain+(1.0f-headroomGain)*headroomRelease);
        if(activity==0) headroomGain=1.0f;
        outL*=headroomGain*activity; outR*=headroomGain*activity;
        l+=(outL-l)*wet;
        r+=(outR-r)*wet;
    }
private:
    // Keep nominal input levels linear; compress excessive gain before the
    // filter bank instead of letting both input trims multiply without bound.
    static float inputHeadroom(float x) noexcept {
        if(!std::isfinite(x)) return 0.0f;
        const float a=std::abs(x);
        if(a<=1.0f) return x;
        const float over=a-1.0f;
        return std::copysign(1.0f+over/(1.0f+over),x);
    }
    float coefficient(float ms) const noexcept { return static_cast<float>(-std::expm1(-1/(sr*ms*.001))); }
    static double blep(double t,double dt) noexcept { if(t<dt) { t/=dt; return t+t-t*t-1; } if(t>1-dt) { t=(t-1)/dt; return t*t+t+t+1; } return 0; }
    static float subOscillator(double t,double dt) noexcept { return static_cast<float>((t<.5?1.0:-1.0)+blep(t,dt)-blep(t<.5?t+.5:t-.5,dt)); }
    float oscillator(double t,double dt) const noexcept {
        return static_cast<float>(mode==0?2*t-1-blep(t,dt):(t<.5?1.0:-1.0)+blep(t,dt)-blep(t<.5?t+.5:t-.5,dt));
    }
    struct ConsonantState { float low=0, low2=0, full=0, high=0; };
    float preserveConsonants(int ch,float x) noexcept {
        auto& s=consonants[static_cast<std::size_t>(ch)];
        s.low+=(x-s.low)*consonantLP;
        const float hp=x-s.low;
        s.low2+=(hp-s.low2)*consonantLP;
        const float high=hp-s.low2;
        const auto follow=[this](float& env,float v) { v=std::abs(v); env+=(v-env)*(v>env?consonantAttack:consonantRelease); };
        follow(s.full,x); follow(s.high,high);
        const float ratio=s.high/std::max(1e-5f,s.full);
        const float probability=std::clamp((ratio-.09f)/.22f,0.0f,1.0f);
        // Retain some lower consonant energy as well as the high-frequency detail.
        return (.75f*hp+.25f*x)*probability*unvoicedNow;
    }
    float random() noexcept { noiseState^=noiseState<<13; noiseState^=noiseState>>17; noiseState^=noiseState<<5; return static_cast<float>(noiseState>>8)*(2.0f/16777216.0f)-1.0f; }
    float channelProcess(int ch,float x,float carrier,int mapOffset,float f,float activity) noexcept {
        auto& bands=bank[ch]; x=std::isfinite(x)?std::clamp(x,-16.0f,16.0f):0; carrier=std::isfinite(carrier)?std::clamp(carrier,-16.0f,16.0f):0;
        const auto input = _mm_set1_ps(x), attack4 = _mm_set1_ps(attack), release4 = _mm_set1_ps(release);
        for(int i=0;i<count;i+=4) {
            const auto a=absolute(filter(i,input,bands.modZ1,bands.modZ2));
            _mm_storeu_ps(bands.env.data()+i,follow(_mm_loadu_ps(bands.env.data()+i),a,attack4,release4));
        }
        const float consonant=preserveConsonants(ch,x);
        if(activity<=0) return 0.0f;
        float y=0, high=0;
        const auto inputCarrier = _mm_set1_ps(carrier);
        const auto carrierAttack4 = _mm_set1_ps(carrierAttack), carrierRelease4 = _mm_set1_ps(carrierRelease);
        for(int i=0;i<count;i+=4) {
            const auto signal = filter(i,inputCarrier,bands.carZ1,bands.carZ2);
            auto equalisation = _mm_set1_ps(1.0f);
            if(mode<2) {
                const auto env=follow(_mm_loadu_ps(bands.carrierEnv.data()+i),absolute(signal),carrierAttack4,carrierRelease4);
                _mm_storeu_ps(bands.carrierEnv.data()+i,env);
                equalisation=_mm_min_ps(_mm_set1_ps(4.0f),_mm_max_ps(_mm_set1_ps(.4f),_mm_div_ps(_mm_set1_ps(.16f),_mm_add_ps(_mm_set1_ps(.04f),env))));
            }
            // Keep the existing edge handling and band summation order.
            alignas(16) float weighted[4];
            _mm_store_ps(weighted,_mm_mul_ps(signal,equalisation));
            for(int j=0;j<4;++j) {
                const int k=i+j+mapOffset;
                const float a=k>=0&&k<count?bands.env[k]:0, b=k+1>=0&&k+1<count?bands.env[k+1]:0;
                const float value=weighted[j]*(a+(b-a)*f);
                y+=contourActive ? value*weightsNow[i+j] : value;
                if(i+j>=count*3/4) high=std::max(high,bands.env[i+j]);
            }
        }
        if(noiseNow>1e-5f) { const float n=random(); noiseLow[ch]+=(n-noiseLow[ch])*noiseLP; y+=noiseNow*(n-noiseLow[ch])*high; }
        const float result=y*3.0f*bandGainNow+consonant;
        return std::isfinite(result)?result:0.0f;
    }
    double sr=48000;
    bool enabled=false, stereo=true, sleeping=true;
    int mode=0,count=0; unsigned age=0; std::uint32_t noiseState=0x243f6a88u;
    float wet=0, fadeStep=.001f, blend=0, width=1, noise=0, formant=0, semitonesPerBand=1;
    float attackMs=-1,releaseMs=-1,attack=0,release=0,voiceAttack=0,voiceRelease=0,noiseLP=0;
    std::array<Voice,8> voices{};
    std::array<Bank,2> bank{};
    float smooth=0, formantNow=0, widthNow=1, noiseNow=0;
    std::array<float,2> noiseLow{};
    std::array<double,maxBands> bandSin{}, bandCos{};
    float bandGain=1, bandGainNow=1;
    float bandwidthNow=-1, detuneValue=-1, unvoiced=0, unvoicedNow=0;
    float shiftNow=999, contourValue=999, compand=0, compandNow=0, harmonic=0, harmonicNow=0, sub=0, subNow=0;
    float modDb=999, carDb=999, modGain=1, modGainNow=1, carGain=1, carGainNow=1;
    std::array<float,maxBands> bandWeights{}, weightsNow{};
    bool contourActive=false;
    double detuneL=1, detuneR=1;
    float consonantLP=0, consonantAttack=0, consonantRelease=0;
    std::array<ConsonantState,2> consonants{};
    float noteGate=0, lastGate=0, externalEnv=0, gateAttackStep=0, gateReleaseStep=0, carrierAttack=0, carrierRelease=0;
    float headroomGain=1, headroomRelease=0;
    std::vector<float> delayL,delayR,delayGate; std::size_t delayPos=0,validDelay=0;
};
}
