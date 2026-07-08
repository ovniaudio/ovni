#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"
#include "params/ParameterIDs.h"

// =============================================================================
// [diag][aurora] — DIAGNÓSTICO de audibilidad (TEMPORAL, no es gate). Mide
// CORR/WIDTH/RMS-delta wet-vs-dry ALINEADO por latencia, con DOS materiales:
//   (A) ruido ROSA de banda ancha (decorrelación espectral con todos los bins)
//   (B) acorde Am grave sparse (lo que defeatea la decorrelación)
// y barre SPREAD / MIX / Mono Safe para AISLAR la causa de "no hace nada".
// =============================================================================
namespace {
namespace pid = aurora::params::id;

struct Stats { double corr, width, rmsDeltaOverDry, levelDb; };

// RMS / corr / width sobre un tramo estéreo.
double rms (const float* x, int n) { double s=0; for (int i=0;i<n;++i) s+=(double)x[i]*x[i]; return std::sqrt(s/juce::jmax(1,n)); }

Stats imageStats (const std::vector<float>& wL, const std::vector<float>& wR,
                  const std::vector<float>& dL, const std::vector<float>& dR,
                  int lat)
{
    const int n = (int) wL.size();
    const int a = (int) (n * 0.25), b = (int) (n * 0.85);   // descartar warmup/fade
    // CORR / WIDTH del WET
    double mL=0,mR=0; for (int i=a;i<b;++i){mL+=wL[i];mR+=wR[i];} mL/=(b-a); mR/=(b-a);
    double num=0,sL=0,sR=0,mid=0,side=0;
    for (int i=a;i<b;++i){ double l=wL[i]-mL,r=wR[i]-mR; num+=l*r; sL+=l*l; sR+=r*r;
        double mm=(wL[i]+wR[i])*0.5, ss=(wL[i]-wR[i])*0.5; mid+=mm*mm; side+=ss*ss; }
    Stats st{};
    st.corr  = (sL*sR>0)? num/std::sqrt(sL*sR) : 0.0;
    st.width = (mid>0)? std::sqrt(side/mid) : 0.0;
    // RMS(wet-dry)/RMS(dry) alineado por latencia (canal L)
    double dd=0, dr=0;
    for (int i=a;i<b;++i){ double diff = wL[i] - dL[i-lat]; dd += diff*diff; dr += (double)dL[i-lat]*dL[i-lat]; }
    st.rmsDeltaOverDry = (dr>0)? std::sqrt(dd/dr) : 0.0;
    // cambio de nivel global
    double wrms=0, drms=0; for (int i=a;i<b;++i){ wrms+=(double)wL[i]*wL[i]; drms+=(double)dL[i]*dL[i]; }
    st.levelDb = (drms>0)? 10.0*std::log10(wrms/drms) : 0.0;
    return st;
}

// Cama: pink broadband (band=0) o acorde Am grave sparse (band=1).
void fillBed (std::vector<float>& L, std::vector<float>& R, double SR, int mode)
{
    const int n = (int) L.size();
    uint32_t rng = 0x2bd6a7u;
    auto noise = [&]{ rng = rng*1664525u+1013904223u; return (float)((int32_t)rng)/2.147483648e9f; };
    float pink=0;
    for (int i=0;i<n;++i)
    {
        const double t=(double)i/SR; double s=0;
        if (mode==0) { // pink broadband + transiente agudo
            pink = 0.96f*pink + 0.04f*noise(); s = 0.9*pink;
            s += 0.15*std::sin(juce::MathConstants<double>::twoPi*3000.0*t);
            s += 0.10*std::sin(juce::MathConstants<double>::twoPi*8000.0*t);
        } else {       // acorde Am grave sparse (lo que defeatea la decorrelación)
            for (double f:{110.0,164.81,220.0}) s += std::sin(juce::MathConstants<double>::twoPi*f*t);
            s *= 0.25;
        }
        double g=1.0; const double fade=0.05, dur=(double)n/SR;
        if (t<fade) g=t/fade; else if (t>dur-fade) g=(dur-t)/fade;
        const float v=(float)(s*g*0.7);
        L[i]=v; R[i]=v;
    }
}

Stats run (int mode, float spread, float tilt, float mix, float msafe, float motion, bool inPhase)
{
    const double SR=48000.0; const int N=512; const int total=(int)(SR*6.0);
    std::vector<float> dL(total), dR(total), wL(total), wR(total);
    fillBed (dL, dR, SR, mode);
    wL = dL; wR = dR;

    aurora::AuroraProcessor proc;
    auto set=[&](const char* id, float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
    set(pid::SPREAD, spread); set(pid::TILT, tilt); set(pid::MIX, mix);
    set(pid::MONOSAFEAMT, msafe); set(pid::MOTION, motion);
    set("monoSafe", inPhase?1.0f:0.0f);
    proc.prepareToPlay(SR, N);

    for (int off=0; off<total; off+=N){
        const int len=juce::jmin(N,total-off);
        juce::AudioBuffer<float> blk(2,len);
        for(int i=0;i<len;++i){ blk.setSample(0,i,wL[off+i]); blk.setSample(1,i,wR[off+i]); }
        juce::MidiBuffer m; proc.processBlock(blk,m);
        for(int i=0;i<len;++i){ wL[off+i]=blk.getSample(0,i); wR[off+i]=blk.getSample(1,i); }
    }
    return imageStats (wL, wR, dL, dR, 2048);
}

void prn (const char* tag, Stats s){
    std::printf("DIAG[%-34s] CORR=%+.3f WIDTH=%.3f RMSd=%.3f LVL=%+.1fdB\n",
                tag, s.corr, s.width, s.rmsDeltaOverDry, s.levelDb);
}
} // namespace

TEST_CASE ("AURORA DIAG: aislar la causa de inaudibilidad", "[diag][aurora]")
{
    std::printf("=== BANDA ANCHA (ruido rosa + agudos) — el material que SÍ tiene bins ===\n");
    prn("PINK default(Spr55 Tilt0 MS50 Mix100)", run(0, 0.55f,0.5f,1.0f,0.5f,0.0f,false));
    prn("PINK SPREAD=100 Mix100",                run(0, 1.0f, 0.5f,1.0f,0.5f,0.0f,false));
    prn("PINK SPREAD=100 Mix100 MSafe=0",        run(0, 1.0f, 0.5f,1.0f,0.0f,0.0f,false));
    prn("PINK SPREAD=100 Mix100 Tilt+100",       run(0, 1.0f, 1.0f,1.0f,0.0f,0.0f,false));
    prn("PINK SPREAD=100 Mix75",                 run(0, 1.0f, 0.5f,0.75f,0.5f,0.0f,false));
    prn("PINK firma(Spr60 Tilt+40 Mix75 MS65)",  run(0, 0.60f,0.70f,0.75f,0.65f,0.30f,false));
    prn("PINK SPREAD=100 INPHASE",               run(0, 1.0f, 0.5f,1.0f,0.0f,0.0f,true));

    std::printf("=== ACORDE Am GRAVE sparse — lo que DEFEATEA la decorrelacion ===\n");
    prn("Am default",                            run(1, 0.55f,0.5f,1.0f,0.5f,0.0f,false));
    prn("Am SPREAD=100 Mix100",                  run(1, 1.0f, 0.5f,1.0f,0.5f,0.0f,false));
    prn("Am SPREAD=100 Mix100 MSafe=0",          run(1, 1.0f, 0.5f,1.0f,0.0f,0.0f,false));
    prn("Am firma(Spr60 Tilt+40 Mix75 MS65)",    run(1, 0.60f,0.70f,0.75f,0.65f,0.30f,false));

    REQUIRE(true);
}
