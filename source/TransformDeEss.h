#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace transform::deess
{
inline constexpr std::array<const char*, 2> modeNames {{ "Split-band", "Broadband" }};
inline constexpr std::array<const char*, 2> monitorNames {{ "Normal", "Removed" }};
struct Controls { float frequency=6500, threshold=-24, range=0, attack=2, release=80; int mode=0, monitor=0; float bandwidth=100, adaptive=0; };
// Stereo-linked peak detector and complementary band subtraction. The dry
// path is never filtered: zero reduction is an exact arithmetic bypass.
class Engine
{
    struct Band
    {
        double s1=0, s2=0;
        float process(float x, double a1, double a2, double a3, double damping) noexcept
        {
            const double v3=x-s2, v1=a1*s1+a2*v3, v2=s2+a2*s1+a3*v3;
            s1=2*v1-s1; s2=2*v2-s2;
            return static_cast<float>(v1*damping); // Unity at centre for every Q
        }
    };
public:
    void prepare(double rate,int channels) noexcept
    {
        sr=std::max(8000.0,rate); stereo=channels>1;
        fadeStep=static_cast<float>(1/(sr*.02)); smooth=coefficient(10);
        bodyAttack=coefficient(15); bodyRelease=coefficient(120); meterRelease=coefficient(250);
        reset();
    }
    void reset() noexcept
    {
        bands={}; env=bodyEnv=meterPeak=0; gain=1; wet=0; enabled=false; running=false;
        frequency=bandwidth=thresholdDb=rangeDb=attackMs=releaseMs=-1; adaptive=0;
        targetRange=0; mode=monitor=modeNow=monitorNow=0;
    }
    void disable() noexcept { enabled=false; }
    void setControls(bool active,const Controls& c) noexcept
    {
        if(!active) { disable(); return; }
        enabled=true;
        if(frequency!=c.frequency || bandwidth!=c.bandwidth) {
            frequency=c.frequency; bandwidth=c.bandwidth;
            damping=std::clamp(static_cast<double>(bandwidth)*.01,.25,2.0);
            const double g=std::tan(3.14159265358979323846*std::clamp(static_cast<double>(frequency),2000.0,std::min(12000.0,sr*.45))/sr);
            a1=1/(1+g*(g+damping)); a2=g*a1; a3=g*a2;
        }
        if(thresholdDb!=c.threshold) { thresholdDb=c.threshold; threshold=std::pow(10.0f,std::clamp(c.threshold,-60.0f,0.0f)/20); }
        if(rangeDb!=c.range) { rangeDb=c.range; targetRange=1-std::pow(10.0f,-std::clamp(c.range,0.0f,24.0f)/20); }
        if(attackMs!=c.attack) { attackMs=c.attack; attack=coefficient(std::clamp(c.attack,.1f,20.0f)); }
        if(releaseMs!=c.release) { releaseMs=c.release; release=coefficient(std::clamp(c.release,10.0f,500.0f)); }
        mode=c.mode!=0?1.0f:0.0f; monitor=c.monitor!=0?1.0f:0.0f;
        adaptive=std::clamp(c.adaptive*.01f,0.0f,1.0f);
    }
    float reductionDb() const noexcept { return -20.0f*std::log10(std::max(1.0f-meterPeak,0.0001f)); }
    void process(float& l,float& r) noexcept
    {
        const float target=enabled && (targetRange>0 || monitor>0)?1.0f:0.0f;
        wet+=std::clamp(target-wet,-fadeStep,fadeStep);
        if(wet==0 && target==0) { if(running) { bands={}; env=bodyEnv=0; gain=1; running=false; } modeNow=mode; monitorNow=monitor; meterPeak=0; return; }
        running=true;
        const float bl=bands[0].process(l,a1,a2,a3,damping), br=stereo?bands[1].process(r,a1,a2,a3,damping):bl;
        const float peak=std::max(std::abs(bl),std::abs(br));
        env+=(peak-env)*(peak>env?attack:release);
        // Compare sibilance with complementary vocal-body energy. The absolute
        // threshold remains a floor; adaptive mode raises it on strong body content.
        const float body=std::max(std::abs(l-bl),std::abs(r-br));
        bodyEnv+=(body-bodyEnv)*(body>bodyEnv?bodyAttack:bodyRelease);
        const float effectiveThreshold=threshold+adaptive*std::max(0.0f,.5f*bodyEnv-threshold);
        const float desired=1-std::min(targetRange,env>effectiveThreshold?1-effectiveThreshold/env:0.0f);
        gain+=(desired-gain)*smooth;
        const float reduction=wet*(1-gain);
        meterPeak=std::max(reduction,meterPeak*(1-meterRelease));
        modeNow+=(mode-modeNow)*smooth; monitorNow+=(monitor-monitorNow)*smooth;
        const float removedL=(bl+(l-bl)*modeNow)*(1-gain);
        const float removedR=(br+(r-br)*modeNow)*(1-gain);
        l+=wet*((removedL-l)*monitorNow-removedL*(1-monitorNow));
        r+=wet*((removedR-r)*monitorNow-removedR*(1-monitorNow));
    }
private:
    float coefficient(float ms) const noexcept { return static_cast<float>(-std::expm1(-1/(sr*.001*ms))); }
    double sr=48000,a1=1,a2=0,a3=0,damping=1;
    bool stereo=true,enabled=false,running=false;
    std::array<Band,2> bands{};
    float frequency=-1,bandwidth=-1,thresholdDb=-1,rangeDb=-1,attackMs=-1,releaseMs=-1;
    float adaptive=0,bodyEnv=0,bodyAttack=0,bodyRelease=0,meterPeak=0,meterRelease=0;
    float threshold=.0631f,targetRange=0,attack=0,release=0,smooth=0,fadeStep=1;
    float env=0,gain=1,wet=0,mode=0,monitor=0,modeNow=0,monitorNow=0;
};
}
