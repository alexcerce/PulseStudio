#pragma once

#include "Common.h"

namespace Mp3EncoderWin { String encode (const File& wav, const File& mp3, int kbps); }

/** Export helpers: MP3 (through Windows' own encoder) and an offline true-peak limiter for
    streaming targets. */
namespace ExportTools
{
    //==============================================================================
    /** Lowers the peaks of an audio file so its true peak (4x oversampled) stays under `ceilingDb`,
        with a short look-ahead and a smooth release. Returns the gain reduction applied (dB). */
    inline float limitTruePeak (te::Engine& engine, const File& f, float ceilingDb)
    {
        std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (r == nullptr) return 0.0f;
        const int n = (int) r->lengthInSamples, chans = (int) jmin (2u, r->numChannels);
        const double sr = r->sampleRate;
        const int bits = (int) r->bitsPerSample;
        AudioBuffer<float> b (chans, n);
        r->read (&b, 0, n, 0, true, chans > 1);
        r.reset();

        // the true peak per sample, from a 4x oversampled copy
        constexpr int chunk = 32768;   // in pieces, so long songs don't need huge buffers
        dsp::Oversampling<float> os ((size_t) chans, 2, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        os.initProcessing ((size_t) chunk);
        std::vector<float> peak ((size_t) n, 0.0f);
        const int latency = (int) std::round (os.getLatencyInSamples());
        AudioBuffer<float> piece (chans, chunk);
        for (int start = 0; start < n; start += chunk)
        {
            const int len = jmin (chunk, n - start);
            piece.clear();
            for (int ch = 0; ch < chans; ++ch) piece.copyFrom (ch, 0, b, ch, start, len);
            dsp::AudioBlock<float> block (piece);
            auto up = os.processSamplesUp (block.getSubBlock (0, (size_t) len));
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                {
                    const int sIdx = jlimit (0, n - 1, start + (int) (i / 4) - latency);
                    peak[(size_t) sIdx] = jmax (peak[(size_t) sIdx], std::abs (up.getSample ((int) ch, (int) i)));
                }
        }
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < n; ++i) peak[(size_t) i] = jmax (peak[(size_t) i], std::abs (b.getSample (ch, i)));

        // gain needed per sample, spread backwards over the look-ahead, smoothed on the way out
        const float ceiling = Decibels::decibelsToGain (ceilingDb) * 0.995f;
        const int look = jmax (1, (int) (0.002 * sr));
        std::vector<float> need ((size_t) n, 1.0f);
        for (int i = 0; i < n; ++i) if (peak[(size_t) i] > ceiling) need[(size_t) i] = ceiling / peak[(size_t) i];
        std::vector<float> g ((size_t) n, 1.0f);
        float minGain = 1.0f;
        {
            std::deque<std::pair<int, float>> window;   // sliding minimum over [i, i + look]
            for (int i = n - 1; i >= 0; --i)
            {
                while (! window.empty() && window.back().second >= need[(size_t) i]) window.pop_back();
                window.push_back ({ i, need[(size_t) i] });
                while (window.front().first > i + look) window.pop_front();
                g[(size_t) i] = window.front().second;
            }
            const float rel = std::exp (-1.0f / (float) (0.08 * sr));
            float env = 1.0f;
            for (int i = 0; i < n; ++i)
            {
                env = g[(size_t) i] < env ? g[(size_t) i] : rel * env + (1.0f - rel) * g[(size_t) i];
                env = jmin (env, g[(size_t) i]);
                g[(size_t) i] = env;
                minGain = jmin (minGain, env);
            }
        }
        if (minGain >= 0.9999f) return 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < n; ++i) b.setSample (ch, i, b.getSample (ch, i) * g[(size_t) i]);

        auto* fmt = engine.getAudioFileFormatManager().getFormatFromFileName (f);
        if (fmt == nullptr) return 0.0f;
        auto tmp = f.getSiblingFile (f.getFileNameWithoutExtension() + "_limited" + f.getFileExtension());
        {
            std::unique_ptr<OutputStream> out (tmp.createOutputStream());
            if (out == nullptr) return 0.0f;
            std::unique_ptr<AudioFormatWriter> w (fmt->createWriterFor (out.get(), sr, (unsigned int) chans, bits, {}, 0));
            if (w == nullptr) return 0.0f;
            out.release();
            w->writeFromAudioSampleBuffer (b, 0, n);
        }
        tmp.moveFileTo (f);
        return -Decibels::gainToDecibels (minGain);
    }
}
