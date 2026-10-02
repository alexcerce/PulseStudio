// MP3 export through Windows Media Foundation (the MP3 encoder that ships with Windows 8 and
// later), so no extra encoder has to be installed. Kept out of the JUCE headers because it needs
// <windows.h> and the Media Foundation headers.
#include <JuceHeader.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfreadwrite.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "ole32.lib")

namespace Mp3EncoderWin
{
    template <typename T> struct Com
    {
        T* p = nullptr;
        ~Com() { if (p != nullptr) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        operator T*() const { return p; }
    };

    /** Encodes a WAV file to MP3 at `kbps` (128, 160, 192, 256 or 320; MP3 needs 32, 44.1 or 48 kHz).
        Returns an empty string on success, or what went wrong. */
    juce::String encode (const juce::File& wav, const juce::File& mp3, int kbps)
    {
        const HRESULT co = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        if (FAILED (MFStartup (MF_VERSION)))
            return "Media Foundation is not available on this computer.";

        juce::String error;
        {
            Com<IMFSourceReader> reader;
            if (FAILED (MFCreateSourceReaderFromURL (wav.getFullPathName().toWideCharPointer(), nullptr, &reader)))
                error = "Could not read the rendered audio.";

            // the reader hands out 16-bit PCM (what the MP3 encoder takes)
            Com<IMFMediaType> pcm;
            UINT32 rate = 0, channels = 0;
            if (error.isEmpty())
            {
                MFCreateMediaType (&pcm);
                pcm->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                pcm->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
                pcm->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                if (FAILED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcm)))
                    error = "The rendered audio could not be converted for MP3.";
            }
            Com<IMFMediaType> actual;
            if (error.isEmpty())
            {
                reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual);
                actual->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
                actual->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &channels);
                if (rate != 32000 && rate != 44100 && rate != 48000)
                    error = "MP3 needs a sample rate of 44.1 or 48 kHz.";
            }

            Com<IMFSinkWriter> writer;
            DWORD stream = 0;
            if (error.isEmpty())
            {
                mp3.deleteFile();
                if (FAILED (MFCreateSinkWriterFromURL (mp3.getFullPathName().toWideCharPointer(), nullptr, nullptr, &writer)))
                    error = "Could not create the MP3 file.";
            }
            if (error.isEmpty())
            {
                Com<IMFMediaType> out;
                MFCreateMediaType (&out);
                out->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                out->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_MP3);
                out->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
                out->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, channels);
                out->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (kbps * 1000 / 8));
                if (FAILED (writer->AddStream (out, &stream)))
                    error = "This version of Windows has no MP3 encoder (it needs Windows 8 or later).";
                else if (FAILED (writer->SetInputMediaType (stream, actual, nullptr)))
                    error = "The MP3 encoder does not accept this audio (" + juce::String ((int) rate) + " Hz, " + juce::String ((int) kbps) + " kbps).";
                else if (FAILED (writer->BeginWriting()))
                    error = "The MP3 encoder could not start.";
            }
            if (error.isEmpty())
            {
                for (;;)
                {
                    DWORD flags = 0;
                    LONGLONG ts = 0;
                    Com<IMFSample> sample;
                    if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &ts, &sample))) { error = "Reading the audio failed."; break; }
                    if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) break;
                    if (sample.p != nullptr && FAILED (writer->WriteSample (stream, sample))) { error = "Writing the MP3 failed."; break; }
                }
                if (FAILED (writer->Finalize()) && error.isEmpty()) error = "Finishing the MP3 failed.";
            }
        }
        MFShutdown();
        if (SUCCEEDED (co)) CoUninitialize();
        return error;
    }
}
