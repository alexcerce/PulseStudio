#pragma once

#include "PulseDevices.h"

/** A linear-phase filter: it changes how loud each frequency is and nothing else, so nothing is smeared in time
    (every frequency comes out late by exactly the same amount, which the engine makes up for).

    The wanted response is given as one real number per frequency, from 0 Hz to half the sample rate. There are four
    of them: left to left, right to right, and the two crossings (which is what mid / side work needs). The sound is
    worked on in blocks of half the transform's length; a new response is faded in over one block. */
namespace pulse
{
    class LinearFir
    {
    public:
        /** order: the transform has 2^order points; the filter is half as long, plus one. */
        void prepare (int order)
        {
            fft = std::make_unique<dsp::FFT> (order);
            size = 1 << order;
            hop = size / 2;
            for (auto& v : { &inL, &inR }) v->assign ((size_t) size, 0.0f);
            for (auto& v : { &outL, &outR }) v->assign ((size_t) hop, 0.0f);
            for (auto& v : { &workL, &workR, &specL, &specR, &build }) v->assign ((size_t) size * 2, 0.0f);
            for (auto& set : { &now, &next })
                for (auto& s : *set) s.assign ((size_t) size + 2, 0.0f);
            window.resize ((size_t) hop + 1);
            for (int i = 0; i <= hop; ++i)   // a raised cosine: the best detail in the lows a filter this long can have
                window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (MathConstants<double>::twoPi * i / hop));
            // what one trip there and back multiplies by (so that nothing here depends on the transform's own scaling)
            std::fill (build.begin(), build.end(), 0.0f);
            build[0] = 1.0f;
            fft->performRealOnlyForwardTransform (build.data(), true);
            fft->performRealOnlyInverseTransform (build.data());
            norm = 1.0f / jmax (1.0e-9f, build[0]);
            reset();
            // straight through until told otherwise
            std::vector<float> flat ((size_t) hop + 1, 1.0f);
            setResponse (flat.data(), flat.data(), nullptr, nullptr);
            now = next;
            nowCross = nextCross;
            pending = false;
            fresh = true;
        }

        void reset()
        {
            for (auto* v : { &inL, &inR, &outL, &outR }) std::fill (v->begin(), v->end(), 0.0f);
            pos = 0;
        }

        int bins() const       { return hop + 1; }
        int blockSize() const  { return hop; }
        /** How late the sound comes out, in samples. */
        int latency() const    { return hop + hop / 2; }
        /** The frequency of point k of a response, at this sample rate. */
        double binHz (int k, double sampleRate) const { return k * sampleRate / size; }

        /** The response to move to: `bins()` numbers each. leftToRight / rightToLeft may be nullptr (no crossing). */
        void setResponse (const float* ll, const float* rr, const float* rightToLeft, const float* leftToRight)
        {
            design (ll, next[0]);
            if (rr == ll) next[1] = next[0]; else design (rr, next[1]);
            nextCross = rightToLeft != nullptr && leftToRight != nullptr;
            if (nextCross) { design (rightToLeft, next[2]); design (leftToRight, next[3]); }
            pending = true;
        }

        /** Runs the sound through. `atBlock` is called each time a block is about to be worked out: the place to
            look whether the settings have changed and to call setResponse. */
        template <typename AtBlock>
        void process (float* l, float* r, int n, AtBlock&& atBlock)
        {
            for (int i = 0; i < n; ++i)
            {
                inL[(size_t) (hop + pos)] = l[i];
                inR[(size_t) (hop + pos)] = r[i];
                l[i] = outL[(size_t) pos];
                r[i] = outR[(size_t) pos];
                if (++pos == hop)
                {
                    atBlock();
                    block();
                    pos = 0;
                }
            }
        }

    private:
        using Spectra = std::array<std::vector<float>, 4>;   // left-left, right-right, right-to-left, left-to-right

        /** From a wanted response to the filter's own spectrum: its impulse is cut to length under a window (which is
            what keeps a block from leaking into the next), and moved to the middle. */
        void design (const float* want, std::vector<float>& into)
        {
            std::fill (build.begin(), build.end(), 0.0f);
            for (int k = 0; k <= hop; ++k) build[(size_t) k * 2] = want[k];
            fft->performRealOnlyInverseTransform (build.data());   // the impulse, centred on 0 and wrapping round
            const int half = hop / 2;
            std::vector<float>& taps = designTaps;
            taps.assign ((size_t) size * 2, 0.0f);
            for (int i = -half; i <= half; ++i)
                taps[(size_t) (i + half)] = build[(size_t) ((i + size) % size)] * norm * window[(size_t) (i + half)];
            fft->performRealOnlyForwardTransform (taps.data(), true);
            std::copy (taps.begin(), taps.begin() + size + 2, into.begin());
        }

        void apply (const Spectra& h, bool cross, std::vector<float>& yl, std::vector<float>& yr)
        {
            for (int k = 0; k <= hop; ++k)
            {
                const size_t re = (size_t) k * 2, im = re + 1;
                const float xlr = specL[re], xli = specL[im], xrr = specR[re], xri = specR[im];
                float lr = xlr * h[0][re] - xli * h[0][im], li = xlr * h[0][im] + xli * h[0][re];
                float rr = xrr * h[1][re] - xri * h[1][im], ri = xrr * h[1][im] + xri * h[1][re];
                if (cross)
                {
                    lr += xrr * h[2][re] - xri * h[2][im]; li += xrr * h[2][im] + xri * h[2][re];
                    rr += xlr * h[3][re] - xli * h[3][im]; ri += xlr * h[3][im] + xli * h[3][re];
                }
                yl[re] = lr; yl[im] = li; yr[re] = rr; yr[im] = ri;
            }
            fft->performRealOnlyInverseTransform (yl.data());
            fft->performRealOnlyInverseTransform (yr.data());
        }

        void block()
        {
            std::copy (inL.begin(), inL.end(), specL.begin());
            std::copy (inR.begin(), inR.end(), specR.begin());
            std::fill (specL.begin() + size, specL.end(), 0.0f);
            std::fill (specR.begin() + size, specR.end(), 0.0f);
            fft->performRealOnlyForwardTransform (specL.data(), true);
            fft->performRealOnlyForwardTransform (specR.data(), true);
            if (pending && fresh) { std::swap (now, next); nowCross = nextCross; pending = false; }   // the first response is simply used
            fresh = false;
            apply (now, nowCross, workL, workR);
            // the second half of what comes back is the part no earlier block reaches into
            for (int i = 0; i < hop; ++i) { outL[(size_t) i] = workL[(size_t) (hop + i)] * norm; outR[(size_t) i] = workR[(size_t) (hop + i)] * norm; }
            if (pending)   // (a block is already out: the new response is faded in)
            {
                apply (next, nextCross, workL, workR);
                for (int i = 0; i < hop; ++i)
                {
                    const float a = (float) (i + 1) / (float) hop;
                    outL[(size_t) i] += (workL[(size_t) (hop + i)] * norm - outL[(size_t) i]) * a;
                    outR[(size_t) i] += (workR[(size_t) (hop + i)] * norm - outR[(size_t) i]) * a;
                }
                std::swap (now, next);
                nowCross = nextCross;
                pending = false;
            }
            std::copy (inL.begin() + hop, inL.end(), inL.begin());
            std::copy (inR.begin() + hop, inR.end(), inR.begin());
        }

        std::unique_ptr<dsp::FFT> fft;
        int size = 0, hop = 0, pos = 0;
        float norm = 1.0f;
        bool pending = false, nowCross = false, nextCross = false, fresh = true;
        std::vector<float> inL, inR, outL, outR, workL, workR, specL, specR, build, designTaps, window;
        Spectra now, next;
    };

    /** A plain stereo delay of a whole number of samples (the untreated sound, kept in step with a LinearFir). */
    struct StepDelay
    {
        void prepare (int samples) { len = jmax (1, samples); for (auto& b : buf) b.assign ((size_t) len, 0.0f); pos = 0; }
        inline void process (float& l, float& r) noexcept
        {
            std::swap (l, buf[0][(size_t) pos]);
            std::swap (r, buf[1][(size_t) pos]);
            if (++pos == len) pos = 0;
        }
        std::array<std::vector<float>, 2> buf;
        int len = 1, pos = 0;
    };
}
